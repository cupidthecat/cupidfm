#include "cs_parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char* cs_strndup2(const char* s, size_t n) {
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static char* fmt_err(const parser* P, const token* t, const char* msg) {
    char buf[256];
    const char* src = (P && P->source_name) ? P->source_name : "<input>";
    snprintf(buf, sizeof(buf), "Parse error at %s:%d:%d: %s", src, t->line, t->col, msg);
    return cs_strndup2(buf, strlen(buf));
}

static void next(parser* P) { P->tok = lex_next(&P->L); }

void parser_init(parser* P, const char* src, const char* source_name) {
    memset(P, 0, sizeof(*P));
    lex_init(&P->L, src);
    P->source_name = source_name ? source_name : "<input>";
    next(P);
}

void parse_free_error(parser* P) {
    free(P->error);
    P->error = NULL;
}

static ast* node(parser* P, node_type t) {
    ast* n = (ast*)calloc(1, sizeof(ast));
    if (!n) return NULL;
    n->type = t;
    n->source_name = P->source_name;
    n->line = P->tok.line;
    n->col  = P->tok.col;
    return n;
}

static int accept(parser* P, token_type t) {
    if (P->tok.type == t) { next(P); return 1; }
    return 0;
}

static int expect(parser* P, token_type t, const char* msg) {
    if (P->tok.type == t) { next(P); return 1; }
    if (!P->error) P->error = fmt_err(P, &P->tok, msg);
    return 0;
}

// Forward decls
static ast* parse_stmt(parser* P);
static ast* parse_block(parser* P);
static ast* parse_expr(parser* P);
static ast* parse_interpolated_string(parser* P);
static ast* parse_match_expr(parser* P);
static ast* parse_list_pattern(parser* P);
static ast* parse_map_pattern(parser* P);
static ast* parse_let_stmt(parser* P, int want_semi, int is_const);
static int parse_import_names(parser* P, char*** import_names, char*** local_names, size_t* out_count);
static int parse_export_names(parser* P, char*** local_names, char*** export_names, size_t* out_count);

static ast* parse_switch(parser* P) {
    ast* n = node(P, N_SWITCH);

    expect(P, TK_LPAREN, "expected '(' after switch");
    n->as.switch_stmt.expr = parse_expr(P);
    expect(P, TK_RPAREN, "expected ')' after switch expression");

    expect(P, TK_LBRACE, "expected '{' after switch(...)");

    size_t cap = 4, cnt = 0;
    ast** case_exprs = (ast**)malloc(sizeof(ast*) * cap);
    ast** case_blocks = (ast**)malloc(sizeof(ast*) * cap);
    ast* default_block = NULL;

    if (!case_exprs || !case_blocks) {
        free(case_exprs);
        free(case_blocks);
        if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
        // best-effort parse recovery: consume until end of switch block
        while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF) next(P);
        (void)expect(P, TK_RBRACE, "expected '}' after switch");
        n->as.switch_stmt.case_exprs = NULL;
        n->as.switch_stmt.case_blocks = NULL;
        n->as.switch_stmt.case_count = 0;
        n->as.switch_stmt.default_block = NULL;
        return n;
    }

    while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF && !P->error) {
        if (accept(P, TK_CASE)) {
            if (cnt == cap) {
                cap *= 2;
                ast** ne = (ast**)realloc(case_exprs, sizeof(ast*) * cap);
                if (!ne) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); break; }
                case_exprs = ne;
                ast** nb = (ast**)realloc(case_blocks, sizeof(ast*) * cap);
                if (!nb) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); break; }
                case_blocks = nb;
            }
            case_exprs[cnt] = parse_expr(P);
            case_blocks[cnt] = parse_block(P);
            cnt++;
            continue;
        }

        if (accept(P, TK_DEFAULT)) {
            if (default_block) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "duplicate default in switch");
                break;
            }
            default_block = parse_block(P);
            continue;
        }

        if (!P->error) P->error = fmt_err(P, &P->tok, "expected 'case' or 'default' in switch");
        break;
    }

    expect(P, TK_RBRACE, "expected '}' after switch");

    n->as.switch_stmt.case_exprs = case_exprs;
    n->as.switch_stmt.case_blocks = case_blocks;
    n->as.switch_stmt.case_count = cnt;
    n->as.switch_stmt.default_block = default_block;
    return n;
}

static ast* parse_fn_expr(parser* P) {
    ast* n = node(P, N_FUNCLIT);
    expect(P, TK_LPAREN, "expected '(' after fn");

    size_t cap = 4, cnt = 0;
    char** params = NULL;

    if (P->tok.type != TK_RPAREN) {
        params = (char**)malloc(sizeof(char*) * cap);
        while (1) {
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected parameter name");
                break;
            }
            if (cnt == cap) { cap *= 2; params = (char**)realloc(params, sizeof(char*) * cap); }
            params[cnt++] = cs_strndup2(P->tok.start, P->tok.len);
            next(P);

            if (accept(P, TK_COMMA)) continue;
            break;
        }
    }
    expect(P, TK_RPAREN, "expected ')' after parameters");

    n->as.funclit.params = params;
    n->as.funclit.param_count = cnt;
    n->as.funclit.body = parse_block(P);
    return n;
}

static ast* parse_primary(parser* P) {
    if (P->tok.type == TK_INT) {
        ast* n = node(P, N_LIT_INT);
        n->as.lit_int.v = P->tok.int_val;
        next(P);
        return n;
    }
    if (P->tok.type == TK_FLOAT) {
        ast* n = node(P, N_LIT_FLOAT);
        n->as.lit_float.v = P->tok.float_val;
        next(P);
        return n;
    }
    if (P->tok.type == TK_STR) {
        // token includes quotes; store raw token for now and unescape later in VM
        ast* n = node(P, N_LIT_STR);
        n->as.lit_str.s = cs_strndup2(P->tok.start, P->tok.len);
        next(P);
        return n;
    }
    if (P->tok.type == TK_STR_PART) {
        return parse_interpolated_string(P);
    }
    if (accept(P, TK_MATCH)) {
        return parse_match_expr(P);
    }
    if (P->tok.type == TK_TRUE || P->tok.type == TK_FALSE) {
        ast* n = node(P, N_LIT_BOOL);
        n->as.lit_bool.v = (P->tok.type == TK_TRUE) ? 1 : 0;
        next(P);
        return n;
    }
    if (P->tok.type == TK_NIL) {
        ast* n = node(P, N_LIT_NIL);
        next(P);
        return n;
    }

    if (accept(P, TK_LBRACKET)) {
        ast* n = node(P, N_LISTLIT);
        size_t cap = 8, cnt = 0;
        ast** items = NULL;
        if (P->tok.type != TK_RBRACKET) {
            items = (ast**)malloc(sizeof(ast*) * cap);
            while (1) {
                if (cnt == cap) { cap *= 2; items = (ast**)realloc(items, sizeof(ast*) * cap); }
                items[cnt++] = parse_expr(P);
                if (accept(P, TK_COMMA)) {
                    if (P->tok.type == TK_RBRACKET) break;
                    continue;
                }
                break;
            }
        }
        expect(P, TK_RBRACKET, "expected ']'");
        n->as.listlit.items = items;
        n->as.listlit.count = cnt;
        return n;
    }

    if (accept(P, TK_LBRACE)) {
        ast* n = node(P, N_MAPLIT);
        size_t cap = 8, cnt = 0;
        ast** keys = NULL;
        ast** vals = NULL;
        if (P->tok.type != TK_RBRACE) {
            keys = (ast**)malloc(sizeof(ast*) * cap);
            vals = (ast**)malloc(sizeof(ast*) * cap);
            while (1) {
                if (cnt == cap) {
                    cap *= 2;
                    keys = (ast**)realloc(keys, sizeof(ast*) * cap);
                    vals = (ast**)realloc(vals, sizeof(ast*) * cap);
                }
                keys[cnt] = parse_expr(P);
                expect(P, TK_COLON, "expected ':' in map literal");
                vals[cnt] = parse_expr(P);
                cnt++;
                if (accept(P, TK_COMMA)) {
                    if (P->tok.type == TK_RBRACE) break;
                    continue;
                }
                break;
            }
        }
        expect(P, TK_RBRACE, "expected '}'");
        n->as.maplit.keys = keys;
        n->as.maplit.vals = vals;
        n->as.maplit.count = cnt;
        return n;
    }

    if (accept(P, TK_FN)) {
        return parse_fn_expr(P);
    }

    if (accept(P, TK_LPAREN)) {
        ast* e = parse_expr(P);
        expect(P, TK_RPAREN, "expected ')'");
        return e;
    }

    if (P->tok.type == TK_IDENT) {
        ast* expr = node(P, N_IDENT);
        expr->as.ident.name = cs_strndup2(P->tok.start, P->tok.len);
        next(P);
        return expr;
    }

    if (!P->error) P->error = fmt_err(P, &P->tok, "expected expression");
    return NULL;
}

static ast* parse_call(parser* P) {
    ast* expr = parse_primary(P);
    for (;;) {
        if (accept(P, TK_LPAREN)) {
            ast* call = node(P, N_CALL);
            call->as.call.callee = expr;

            size_t cap = 4;
            size_t cnt = 0;
            ast** args = NULL;
            if (P->tok.type != TK_RPAREN) {
                args = (ast**)malloc(sizeof(ast*) * cap);
                while (1) {
                    if (cnt == cap) {
                        cap *= 2;
                        args = (ast**)realloc(args, sizeof(ast*) * cap);
                    }
                    args[cnt++] = parse_expr(P);
                    if (accept(P, TK_COMMA)) continue;
                    break;
                }
            }
            expect(P, TK_RPAREN, "expected ')' after arguments");

            call->as.call.args = args;
            call->as.call.argc = cnt;
            expr = call;
            continue;
        }
        if (accept(P, TK_LBRACKET)) {
            ast* idx = node(P, N_INDEX);
            idx->as.index.target = expr;
            idx->as.index.index = parse_expr(P);
            expect(P, TK_RBRACKET, "expected ']'");
            expr = idx;
            continue;
        }
        if (accept(P, TK_DOT)) {
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier after '.'");
                break;
            }
            ast* gf = node(P, N_GETFIELD);
            gf->as.getfield.target = expr;
            gf->as.getfield.field = cs_strndup2(P->tok.start, P->tok.len);
            next(P);
            expr = gf;
            continue;
        }
        if (accept(P, TK_QDOT)) {
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier after '?.'");
                break;
            }
            ast* gf = node(P, N_OPTGETFIELD);
            gf->as.getfield.target = expr;
            gf->as.getfield.field = cs_strndup2(P->tok.start, P->tok.len);
            next(P);
            expr = gf;
            continue;
        }
        break;
    }
    return expr;
}

static ast* parse_unary(parser* P) {
    if (P->tok.type == TK_BANG || P->tok.type == TK_MINUS) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_UNOP);
        n->as.unop.op = op;
        n->as.unop.expr = parse_unary(P);
        return n;
    }
    return parse_call(P);
}

static int parse_import_names(parser* P, char*** import_names, char*** local_names, size_t* out_count) {
    size_t cap = 4, cnt = 0;
    char** inames = (char**)malloc(sizeof(char*) * cap);
    char** lnames = (char**)malloc(sizeof(char*) * cap);
    if (!inames || !lnames) {
        free(inames);
        free(lnames);
        if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
        return 0;
    }

    if (P->tok.type != TK_RBRACE) {
        while (1) {
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected import name");
                break;
            }
            if (cnt == cap) {
                cap *= 2;
                inames = (char**)realloc(inames, sizeof(char*) * cap);
                lnames = (char**)realloc(lnames, sizeof(char*) * cap);
            }
            inames[cnt] = cs_strndup2(P->tok.start, P->tok.len);
            lnames[cnt] = cs_strndup2(P->tok.start, P->tok.len);
            next(P);

            if (accept(P, TK_AS)) {
                if (P->tok.type != TK_IDENT) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected local name after 'as'");
                    break;
                }
                free(lnames[cnt]);
                lnames[cnt] = cs_strndup2(P->tok.start, P->tok.len);
                next(P);
            }

            cnt++;
            if (accept(P, TK_COMMA)) continue;
            break;
        }
    }

    expect(P, TK_RBRACE, "expected '}' after import list");
    *import_names = inames;
    *local_names = lnames;
    *out_count = cnt;
    return 1;
}

static int parse_export_names(parser* P, char*** local_names, char*** export_names, size_t* out_count) {
    size_t cap = 4, cnt = 0;
    char** lnames = (char**)malloc(sizeof(char*) * cap);
    char** enames = (char**)malloc(sizeof(char*) * cap);
    if (!lnames || !enames) {
        free(lnames);
        free(enames);
        if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
        return 0;
    }

    if (P->tok.type != TK_RBRACE) {
        while (1) {
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected export name");
                break;
            }
            if (cnt == cap) {
                cap *= 2;
                lnames = (char**)realloc(lnames, sizeof(char*) * cap);
                enames = (char**)realloc(enames, sizeof(char*) * cap);
            }
            lnames[cnt] = cs_strndup2(P->tok.start, P->tok.len);
            enames[cnt] = cs_strndup2(P->tok.start, P->tok.len);
            next(P);

            if (accept(P, TK_AS)) {
                if (P->tok.type != TK_IDENT) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected export name after 'as'");
                    break;
                }
                free(enames[cnt]);
                enames[cnt] = cs_strndup2(P->tok.start, P->tok.len);
                next(P);
            }

            cnt++;
            if (accept(P, TK_COMMA)) continue;
            break;
        }
    }

    expect(P, TK_RBRACE, "expected '}' after export list");
    *local_names = lnames;
    *export_names = enames;
    *out_count = cnt;
    return 1;
}
static ast* parse_mul(parser* P) {
    ast* left = parse_unary(P);
    while (P->tok.type == TK_STAR || P->tok.type == TK_SLASH || P->tok.type == TK_PERCENT) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_BINOP);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = parse_unary(P);
        left = n;
    }
    return left;
}

static ast* parse_add(parser* P) {
    ast* left = parse_mul(P);
    while (P->tok.type == TK_PLUS || P->tok.type == TK_MINUS) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_BINOP);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = parse_mul(P);
        left = n;
    }
    return left;
}

static ast* parse_range(parser* P) {
    ast* left = parse_add(P);
    if (P->tok.type == TK_RANGE || P->tok.type == TK_RANGE_INC) {
        int inclusive = (P->tok.type == TK_RANGE_INC);
        next(P);
        ast* n = node(P, N_RANGE);
        n->as.range.left = left;
        n->as.range.right = parse_add(P);
        n->as.range.inclusive = inclusive;
        return n;
    }
    return left;
}

static ast* parse_cmp(parser* P) {
    ast* left = parse_range(P);
    while (P->tok.type == TK_LT || P->tok.type == TK_LE || P->tok.type == TK_GT || P->tok.type == TK_GE) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_BINOP);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = parse_range(P);
        left = n;
    }
    return left;
}

static ast* parse_eq(parser* P) {
    ast* left = parse_cmp(P);
    while (P->tok.type == TK_EQ || P->tok.type == TK_NE) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_BINOP);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = parse_cmp(P);
        left = n;
    }
    return left;
}

static ast* parse_and(parser* P) {
    ast* left = parse_eq(P);
    while (P->tok.type == TK_ANDAND) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_BINOP);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = parse_eq(P);
        left = n;
    }
    return left;
}

static ast* parse_or(parser* P) {
    ast* left = parse_and(P);
    while (P->tok.type == TK_OROR) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_BINOP);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = parse_and(P);
        left = n;
    }
    return left;
}

static ast* parse_nullish(parser* P) {
    ast* left = parse_or(P);
    while (P->tok.type == TK_QQ) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_BINOP);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = parse_or(P);
        left = n;
    }
    return left;
}

static ast* parse_expr(parser* P) {
    ast* cond = parse_nullish(P);
    if (accept(P, TK_QMARK)) {
        ast* n = node(P, N_TERNARY);
        n->as.ternary.cond = cond;
        n->as.ternary.then_e = parse_expr(P);
        expect(P, TK_COLON, "expected ':' in ternary");
        n->as.ternary.else_e = parse_expr(P);
        return n;
    }
    return cond;
}

static void maybe_semi(parser* P) {
    (void)accept(P, TK_SEMI);
}

static ast* make_quoted_str_lit(parser* P, const char* s) {
    size_t n = strlen(s ? s : "");
    char* buf = (char*)malloc(n + 3);
    if (!buf) return NULL;
    buf[0] = '"';
    if (n) memcpy(buf + 1, s, n);
    buf[n + 1] = '"';
    buf[n + 2] = 0;
    ast* lit = node(P, N_LIT_STR);
    lit->as.lit_str.s = buf;
    return lit;
}

static ast* parse_list_pattern(parser* P) {
    ast* n = node(P, N_PATTERN_LIST);
    size_t cap = 4, cnt = 0;
    char** names = NULL;

    if (P->tok.type != TK_RBRACKET) {
        names = (char**)malloc(sizeof(char*) * cap);
        while (1) {
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier in list pattern");
                break;
            }
            if (cnt == cap) { cap *= 2; names = (char**)realloc(names, sizeof(char*) * cap); }
            names[cnt++] = cs_strndup2(P->tok.start, P->tok.len);
            next(P);

            if (accept(P, TK_COMMA)) {
                if (P->tok.type == TK_RBRACKET) break;
                continue;
            }
            break;
        }
    }

    expect(P, TK_RBRACKET, "expected ']' after list pattern");
    n->as.list_pattern.names = names;
    n->as.list_pattern.count = cnt;
    return n;
}

static ast* parse_map_pattern(parser* P) {
    ast* n = node(P, N_PATTERN_MAP);
    size_t cap = 4, cnt = 0;
    char** keys = NULL;
    char** names = NULL;

    if (P->tok.type != TK_RBRACE) {
        keys = (char**)malloc(sizeof(char*) * cap);
        names = (char**)malloc(sizeof(char*) * cap);
        while (1) {
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier in map pattern");
                break;
            }
            if (cnt == cap) {
                cap *= 2;
                keys = (char**)realloc(keys, sizeof(char*) * cap);
                names = (char**)realloc(names, sizeof(char*) * cap);
            }
            keys[cnt] = cs_strndup2(P->tok.start, P->tok.len);
            names[cnt] = cs_strndup2(P->tok.start, P->tok.len);
            next(P);

            if (accept(P, TK_COLON)) {
                if (P->tok.type != TK_IDENT) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier after ':' in map pattern");
                    break;
                }
                free(names[cnt]);
                names[cnt] = cs_strndup2(P->tok.start, P->tok.len);
                next(P);
            }

            cnt++;

            if (accept(P, TK_COMMA)) {
                if (P->tok.type == TK_RBRACE) break;
                continue;
            }
            break;
        }
    }

    expect(P, TK_RBRACE, "expected '}' after map pattern");
    n->as.map_pattern.keys = keys;
    n->as.map_pattern.names = names;
    n->as.map_pattern.count = cnt;
    return n;
}

static ast* parse_let_stmt(parser* P, int want_semi, int is_const) {
    ast* n = node(P, N_LET);
    n->as.let_stmt.name = NULL;
    n->as.let_stmt.pattern = NULL;
    n->as.let_stmt.is_const = is_const;

    if (accept(P, TK_LBRACKET)) {
        n->as.let_stmt.pattern = parse_list_pattern(P);
    } else if (accept(P, TK_LBRACE)) {
        n->as.let_stmt.pattern = parse_map_pattern(P);
    } else {
        if (P->tok.type != TK_IDENT) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "expected name after let");
            return n;
        }
        n->as.let_stmt.name = cs_strndup2(P->tok.start, P->tok.len);
        next(P);
    }

    if (accept(P, TK_ASSIGN)) {
        n->as.let_stmt.init = parse_expr(P);
    } else {
        if (n->as.let_stmt.pattern) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "destructuring let requires initializer");
        } else if (is_const) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "const requires initializer");
        }
        n->as.let_stmt.init = NULL;
    }

    if (want_semi) maybe_semi(P);
    return n;
}

static ast* make_quoted_str_lit_from_tok(parser* P, const char* s, size_t n) {
    char* buf = (char*)malloc(n + 3);
    if (!buf) return NULL;
    buf[0] = '"';
    if (n) memcpy(buf + 1, s, n);
    buf[n + 1] = '"';
    buf[n + 2] = 0;
    ast* lit = node(P, N_LIT_STR);
    lit->as.lit_str.s = buf;
    return lit;
}

static ast* parse_interpolated_string(parser* P) {
    ast* n = node(P, N_STR_INTERP);
    size_t cap = 4, cnt = 0;
    ast** parts = (ast**)malloc(sizeof(ast*) * cap);
    if (!parts) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); return n; }

    while (1) {
        if (P->tok.type != TK_STR_PART) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "expected string part");
            break;
        }

        ast* lit = make_quoted_str_lit_from_tok(P, P->tok.start, P->tok.len);
        next(P);
        if (cnt == cap) { cap *= 2; parts = (ast**)realloc(parts, sizeof(ast*) * cap); }
        parts[cnt++] = lit;

        if (accept(P, TK_STR_END)) break;

        if (!accept(P, TK_INTERP_START)) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "expected '${' in string interpolation");
            break;
        }

        ast* expr = parse_expr(P);
        if (cnt == cap) { cap *= 2; parts = (ast**)realloc(parts, sizeof(ast*) * cap); }
        parts[cnt++] = expr;

        expect(P, TK_INTERP_END, "expected '}' after interpolation");

        if (P->tok.type != TK_STR_PART) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "expected string part after interpolation");
            break;
        }
    }

    n->as.str_interp.parts = parts;
    n->as.str_interp.count = cnt;
    return n;
}

static ast* parse_lvalue(parser* P) {
    return parse_call(P);
}

static ast* parse_match_expr(parser* P) {
    ast* n = node(P, N_MATCH);

    expect(P, TK_LPAREN, "expected '(' after match");
    n->as.match_expr.expr = parse_expr(P);
    expect(P, TK_RPAREN, "expected ')' after match expression");
    expect(P, TK_LBRACE, "expected '{' after match(...)");

    size_t cap = 4, cnt = 0;
    ast** case_exprs = (ast**)malloc(sizeof(ast*) * cap);
    ast** case_values = (ast**)malloc(sizeof(ast*) * cap);
    ast* default_expr = NULL;

    if (!case_exprs || !case_values) {
        free(case_exprs);
        free(case_values);
        if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
        while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF) next(P);
        (void)expect(P, TK_RBRACE, "expected '}' after match");
        n->as.match_expr.case_exprs = NULL;
        n->as.match_expr.case_values = NULL;
        n->as.match_expr.case_count = 0;
        n->as.match_expr.default_expr = NULL;
        return n;
    }

    while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF && !P->error) {
        if (accept(P, TK_CASE)) {
            if (cnt == cap) {
                cap *= 2;
                case_exprs = (ast**)realloc(case_exprs, sizeof(ast*) * cap);
                case_values = (ast**)realloc(case_values, sizeof(ast*) * cap);
            }
            case_exprs[cnt] = parse_expr(P);
            expect(P, TK_COLON, "expected ':' after case expression");
            case_values[cnt] = parse_expr(P);
            cnt++;
            maybe_semi(P);
            continue;
        }

        if (accept(P, TK_DEFAULT)) {
            if (default_expr) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "duplicate default in match");
                break;
            }
            expect(P, TK_COLON, "expected ':' after default");
            default_expr = parse_expr(P);
            maybe_semi(P);
            continue;
        }

        if (!P->error) P->error = fmt_err(P, &P->tok, "expected 'case' or 'default' in match");
        break;
    }

    expect(P, TK_RBRACE, "expected '}' after match");

    n->as.match_expr.case_exprs = case_exprs;
    n->as.match_expr.case_values = case_values;
    n->as.match_expr.case_count = cnt;
    n->as.match_expr.default_expr = default_expr;
    return n;
}

static ast* parse_fn(parser* P) {
    ast* n = node(P, N_FNDEF);

    if (P->tok.type != TK_IDENT) {
        if (!P->error) P->error = fmt_err(P, &P->tok, "expected function name");
        return n;
    }
    n->as.fndef.name = cs_strndup2(P->tok.start, P->tok.len);
    next(P);

    expect(P, TK_LPAREN, "expected '(' after function name");

    size_t cap = 4, cnt = 0;
    char** params = NULL;

    if (P->tok.type != TK_RPAREN) {
        params = (char**)malloc(sizeof(char*) * cap);
        while (1) {
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected parameter name");
                break;
            }
            if (cnt == cap) { cap *= 2; params = (char**)realloc(params, sizeof(char*) * cap); }
            params[cnt++] = cs_strndup2(P->tok.start, P->tok.len);
            next(P);

            if (accept(P, TK_COMMA)) continue;
            break;
        }
    }
    expect(P, TK_RPAREN, "expected ')' after parameters");

    n->as.fndef.params = params;
    n->as.fndef.param_count = cnt;
    n->as.fndef.body = parse_block(P);
    return n;
}

static ast* parse_block(parser* P) {
    expect(P, TK_LBRACE, "expected '{'");
    ast* b = node(P, N_BLOCK);

    size_t cap = 8, cnt = 0;
    ast** items = (ast**)malloc(sizeof(ast*) * cap);

    while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF && !P->error) {
        if (cnt == cap) { cap *= 2; items = (ast**)realloc(items, sizeof(ast*) * cap); }
        items[cnt++] = parse_stmt(P);
    }

    expect(P, TK_RBRACE, "expected '}'");
    b->as.block.items = items;
    b->as.block.count = cnt;
    return b;
}

static ast* build_assignment(parser* P, ast* lv, token_type op, ast* rhs) {
    if (lv && lv->type == N_IDENT) {
        ast* n = node(P, N_ASSIGN);
        n->as.assign_stmt.name = lv->as.ident.name;
        lv->as.ident.name = NULL; // transfer ownership
        if (op == TK_ASSIGN) n->as.assign_stmt.value = rhs;
        else {
            int bop = TK_PLUS;
            if (op == TK_MINUSEQ) bop = TK_MINUS;
            else if (op == TK_STAREQ) bop = TK_STAR;
            else if (op == TK_SLASHEQ) bop = TK_SLASH;
            ast* left = node(P, N_IDENT);
            left->as.ident.name = cs_strndup2(n->as.assign_stmt.name, strlen(n->as.assign_stmt.name));
            ast* bin = node(P, N_BINOP);
            bin->as.binop.op = bop;
            bin->as.binop.left = left;
            bin->as.binop.right = rhs;
            n->as.assign_stmt.value = bin;
        }
        return n;
    }
    if (lv && lv->type == N_INDEX) {
        ast* n = node(P, N_SETINDEX);
        n->as.setindex_stmt.target = lv->as.index.target;
        n->as.setindex_stmt.index = lv->as.index.index;
        n->as.setindex_stmt.value = rhs;
        n->as.setindex_stmt.op = op;
        return n;
    }
    if (lv && lv->type == N_GETFIELD) {
        ast* idx = make_quoted_str_lit(P, lv->as.getfield.field);
        if (!idx) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); return lv; }
        ast* n = node(P, N_SETINDEX);
        n->as.setindex_stmt.target = lv->as.getfield.target;
        n->as.setindex_stmt.index = idx;
        n->as.setindex_stmt.value = rhs;
        n->as.setindex_stmt.op = op;
        return n;
    }
    if (!P->error) P->error = fmt_err(P, &P->tok, "invalid assignment target");
    return lv;
}

static ast* parse_stmt(parser* P) {
    if (accept(P, TK_BREAK)) {
        ast* n = node(P, N_BREAK);
        maybe_semi(P);
        return n;
    }

    if (accept(P, TK_CONTINUE)) {
        ast* n = node(P, N_CONTINUE);
        maybe_semi(P);
        return n;
    }

    if (accept(P, TK_IMPORT)) {
        ast* n = node(P, N_IMPORT);
        n->as.import_stmt.path = NULL;
        n->as.import_stmt.default_name = NULL;
        n->as.import_stmt.import_names = NULL;
        n->as.import_stmt.local_names = NULL;
        n->as.import_stmt.count = 0;

        if (accept(P, TK_LBRACE)) {
            (void)parse_import_names(P, &n->as.import_stmt.import_names, &n->as.import_stmt.local_names, &n->as.import_stmt.count);
            expect(P, TK_FROM, "expected 'from' after import list");
            n->as.import_stmt.path = parse_expr(P);
            maybe_semi(P);
            return n;
        }

        if (P->tok.type == TK_IDENT) {
            n->as.import_stmt.default_name = cs_strndup2(P->tok.start, P->tok.len);
            next(P);
            if (accept(P, TK_COMMA)) {
                expect(P, TK_LBRACE, "expected '{' after ',' in import");
                (void)parse_import_names(P, &n->as.import_stmt.import_names, &n->as.import_stmt.local_names, &n->as.import_stmt.count);
            }
            expect(P, TK_FROM, "expected 'from' after import name");
            n->as.import_stmt.path = parse_expr(P);
            maybe_semi(P);
            return n;
        }

        n->as.import_stmt.path = parse_expr(P);
        maybe_semi(P);
        return n;
    }

    if (accept(P, TK_DEFER)) {
        ast* n = node(P, N_DEFER);
        if (P->tok.type == TK_LBRACE) {
            n->as.defer_stmt.stmt = parse_block(P);
        } else {
            ast* expr = parse_expr(P);
            maybe_semi(P);
            ast* es = node(P, N_EXPR_STMT);
            es->as.expr_stmt.expr = expr;
            n->as.defer_stmt.stmt = es;
        }
        return n;
    }

    if (accept(P, TK_SWITCH)) {
        return parse_switch(P);
    }

    if (accept(P, TK_THROW)) {
        ast* n = node(P, N_THROW);
        n->as.throw_stmt.value = parse_expr(P);
        maybe_semi(P);
        return n;
    }

    if (accept(P, TK_EXPORT)) {
        if (accept(P, TK_LBRACE)) {
            ast* n = node(P, N_EXPORT_LIST);
            n->as.export_list.local_names = NULL;
            n->as.export_list.export_names = NULL;
            n->as.export_list.count = 0;
            (void)parse_export_names(P, &n->as.export_list.local_names, &n->as.export_list.export_names, &n->as.export_list.count);
            maybe_semi(P);
            return n;
        }

        ast* n = node(P, N_EXPORT);
        if (P->tok.type != TK_IDENT) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "expected export name");
            return n;
        }
        n->as.export_stmt.name = cs_strndup2(P->tok.start, P->tok.len);
        next(P);
        expect(P, TK_ASSIGN, "expected '=' after export name");
        n->as.export_stmt.value = parse_expr(P);
        maybe_semi(P);
        return n;
    }

    if (accept(P, TK_TRY)) {
        ast* n = node(P, N_TRY);
        n->as.try_stmt.try_b = parse_block(P);
        expect(P, TK_CATCH, "expected catch after try block");
        expect(P, TK_LPAREN, "expected '(' after catch");
        if (P->tok.type != TK_IDENT) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "expected catch variable name");
            return n;
        }
        n->as.try_stmt.catch_name = cs_strndup2(P->tok.start, P->tok.len);
        next(P);
        expect(P, TK_RPAREN, "expected ')'");
        n->as.try_stmt.catch_b = parse_block(P);
        if (accept(P, TK_FINALLY)) {
            if (P->tok.type == TK_LBRACE) {
                n->as.try_stmt.finally_b = parse_block(P);
            } else {
                ast* expr = parse_expr(P);
                maybe_semi(P);
                ast* es = node(P, N_EXPR_STMT);
                es->as.expr_stmt.expr = expr;
                n->as.try_stmt.finally_b = es;
            }
        } else {
            n->as.try_stmt.finally_b = NULL;
        }
        return n;
    }

    if (accept(P, TK_FOR)) {
        // Check if it's C-style for loop by looking for '('
        if (P->tok.type == TK_LPAREN) {
            // C-style: for (init; cond; incr) { body }
            next(P); // consume '('
            ast* n = node(P, N_FOR_C_STYLE);
            
            // Parse init (can be empty, or an assignment statement)
            if (P->tok.type != TK_SEMI) {
                if (accept(P, TK_LET)) {
                    n->as.for_c_style_stmt.init = parse_let_stmt(P, 0, 0);
                } else if (accept(P, TK_CONST)) {
                    n->as.for_c_style_stmt.init = parse_let_stmt(P, 0, 1);
                } else {
                // Try to parse as assignment (check for identifier followed by =)
                if (P->tok.type == TK_IDENT) {
                    lexer saveL = P->L;
                    token saveT = P->tok;
                    next(P); // skip identifier
                    if (P->tok.type == TK_ASSIGN || P->tok.type == TK_PLUSEQ || 
                        P->tok.type == TK_MINUSEQ || P->tok.type == TK_STAREQ || 
                        P->tok.type == TK_SLASHEQ) {
                        // It's an assignment, rewind and parse it
                        P->L = saveL;
                        P->tok = saveT;
                        
                        ast* lv = parse_lvalue(P);
                        token_type op = P->tok.type;
                        next(P);
                        ast* rhs = parse_expr(P);
                        n->as.for_c_style_stmt.init = build_assignment(P, lv, op, rhs);
                    } else {
                        // Not an assignment, rewind and parse as expression
                        P->L = saveL;
                        P->tok = saveT;
                        n->as.for_c_style_stmt.init = parse_expr(P);
                    }
                } else {
                    n->as.for_c_style_stmt.init = parse_expr(P);
                }
                }
            } else {
                n->as.for_c_style_stmt.init = NULL;
            }
            expect(P, TK_SEMI, "expected ';' after for loop init");
            
            // Parse condition (can be empty)
            if (P->tok.type != TK_SEMI) {
                n->as.for_c_style_stmt.cond = parse_expr(P);
            } else {
                n->as.for_c_style_stmt.cond = NULL;
            }
            expect(P, TK_SEMI, "expected ';' after for loop condition");
            
            // Parse increment (can be empty, or an assignment statement)
            if (P->tok.type != TK_RPAREN) {
                // Try to parse as assignment (check for identifier followed by =)
                if (P->tok.type == TK_IDENT) {
                    lexer saveL = P->L;
                    token saveT = P->tok;
                    next(P); // skip identifier
                    if (P->tok.type == TK_ASSIGN || P->tok.type == TK_PLUSEQ || 
                        P->tok.type == TK_MINUSEQ || P->tok.type == TK_STAREQ || 
                        P->tok.type == TK_SLASHEQ) {
                        // It's an assignment, rewind and parse it
                        P->L = saveL;
                        P->tok = saveT;
                        
                        ast* lv = parse_lvalue(P);
                        token_type op = P->tok.type;
                        next(P);
                        ast* rhs = parse_expr(P);
                        n->as.for_c_style_stmt.incr = build_assignment(P, lv, op, rhs);
                    } else {
                        // Not an assignment, rewind and parse as expression
                        P->L = saveL;
                        P->tok = saveT;
                        n->as.for_c_style_stmt.incr = parse_expr(P);
                    }
                } else {
                    n->as.for_c_style_stmt.incr = parse_expr(P);
                }
            } else {
                n->as.for_c_style_stmt.incr = NULL;
            }
            expect(P, TK_RPAREN, "expected ')' after for loop");
            
            n->as.for_c_style_stmt.body = parse_block(P);
            return n;
        } else {
            // for-in style: for name in iterable { body }
            ast* n = node(P, N_FORIN);
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected loop variable name");
                return n;
            }
            n->as.forin_stmt.name = cs_strndup2(P->tok.start, P->tok.len);
            next(P);
            expect(P, TK_IN, "expected 'in' in for loop");
            n->as.forin_stmt.iterable = parse_expr(P);
            n->as.forin_stmt.body = parse_block(P);
            return n;
        }
    }

    if (accept(P, TK_LET)) {
        return parse_let_stmt(P, 1, 0);
    }

    if (accept(P, TK_CONST)) {
        return parse_let_stmt(P, 1, 1);
    }

    if (accept(P, TK_FN)) {
        return parse_fn(P);
    }

    if (accept(P, TK_IF)) {
        ast* n = node(P, N_IF);
        expect(P, TK_LPAREN, "expected '(' after if");
        n->as.if_stmt.cond = parse_expr(P);
        expect(P, TK_RPAREN, "expected ')'");
        n->as.if_stmt.then_b = parse_block(P);
        n->as.if_stmt.else_b = NULL;
        if (accept(P, TK_ELSE)) {
            if (P->tok.type == TK_IF) {
                n->as.if_stmt.else_b = parse_stmt(P);
            } else {
                n->as.if_stmt.else_b = parse_block(P);
            }
        }
        return n;
    }

    if (accept(P, TK_WHILE)) {
        ast* n = node(P, N_WHILE);
        expect(P, TK_LPAREN, "expected '(' after while");
        n->as.while_stmt.cond = parse_expr(P);
        expect(P, TK_RPAREN, "expected ')'");
        n->as.while_stmt.body = parse_block(P);
        return n;
    }

    if (accept(P, TK_RETURN)) {
        ast* n = node(P, N_RETURN);
        if (P->tok.type != TK_SEMI && P->tok.type != TK_RBRACE) n->as.ret_stmt.value = parse_expr(P);
        else n->as.ret_stmt.value = NULL;
        maybe_semi(P);
        return n;
    }

    // assignment: lvalue (= expr) where lvalue is IDENT / GETFIELD / INDEX
    if (P->tok.type == TK_IDENT) {
        // lookahead with a copy
        lexer saveL = P->L;
        token saveT = P->tok;

        (void)parse_lvalue(P);
        token_type assign_op = P->tok.type;
        int is_assign = (assign_op == TK_ASSIGN || assign_op == TK_PLUSEQ || assign_op == TK_MINUSEQ ||
                         assign_op == TK_STAREQ || assign_op == TK_SLASHEQ);

        // rewind and parse
        P->L = saveL;
        P->tok = saveT;

        if (is_assign) {
            ast* lv = parse_lvalue(P);
            token_type op = P->tok.type;
            next(P);

            ast* rhs = parse_expr(P);
            maybe_semi(P);

            return build_assignment(P, lv, op, rhs);
        }
    }

    // expression statement
    ast* n = node(P, N_EXPR_STMT);
    n->as.expr_stmt.expr = parse_expr(P);
    maybe_semi(P);
    return n;
}

ast* parse_program(parser* P) {
    ast* b = node(P, N_BLOCK);

    size_t cap = 16, cnt = 0;
    ast** items = (ast**)malloc(sizeof(ast*) * cap);

    while (P->tok.type != TK_EOF && !P->error) {
        if (cnt == cap) { cap *= 2; items = (ast**)realloc(items, sizeof(ast*) * cap); }
        items[cnt++] = parse_stmt(P);
    }

    b->as.block.items = items;
    b->as.block.count = cnt;
    return b;
}

void ast_free(ast* node) {
    if (!node) return;
    
    switch (node->type) {
        case N_BLOCK:
            for (size_t i = 0; i < node->as.block.count; i++) {
                ast_free(node->as.block.items[i]);
            }
            free(node->as.block.items);
            break;
        case N_LET:
            free(node->as.let_stmt.name);
            ast_free(node->as.let_stmt.init);
            ast_free(node->as.let_stmt.pattern);
            break;
        case N_ASSIGN:
            free(node->as.assign_stmt.name);
            ast_free(node->as.assign_stmt.value);
            break;
        case N_SETINDEX:
            ast_free(node->as.setindex_stmt.target);
            ast_free(node->as.setindex_stmt.index);
            ast_free(node->as.setindex_stmt.value);
            break;
        case N_SWITCH:
            ast_free(node->as.switch_stmt.expr);
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                ast_free(node->as.switch_stmt.case_exprs[i]);
                ast_free(node->as.switch_stmt.case_blocks[i]);
            }
            free(node->as.switch_stmt.case_exprs);
            free(node->as.switch_stmt.case_blocks);
            ast_free(node->as.switch_stmt.default_block);
            break;
        case N_MATCH:
            ast_free(node->as.match_expr.expr);
            for (size_t i = 0; i < node->as.match_expr.case_count; i++) {
                ast_free(node->as.match_expr.case_exprs[i]);
                ast_free(node->as.match_expr.case_values[i]);
            }
            free(node->as.match_expr.case_exprs);
            free(node->as.match_expr.case_values);
            ast_free(node->as.match_expr.default_expr);
            break;
        case N_DEFER:
            ast_free(node->as.defer_stmt.stmt);
            break;
        case N_FORIN:
            free(node->as.forin_stmt.name);
            ast_free(node->as.forin_stmt.iterable);
            ast_free(node->as.forin_stmt.body);
            break;
        case N_FOR_C_STYLE:
            ast_free(node->as.for_c_style_stmt.init);
            ast_free(node->as.for_c_style_stmt.cond);
            ast_free(node->as.for_c_style_stmt.incr);
            ast_free(node->as.for_c_style_stmt.body);
            break;
        case N_THROW:
            ast_free(node->as.throw_stmt.value);
            break;
        case N_TRY:
            ast_free(node->as.try_stmt.try_b);
            free(node->as.try_stmt.catch_name);
            ast_free(node->as.try_stmt.catch_b);
            ast_free(node->as.try_stmt.finally_b);
            break;
        case N_EXPORT:
            free(node->as.export_stmt.name);
            ast_free(node->as.export_stmt.value);
            break;
        case N_EXPORT_LIST:
            for (size_t i = 0; i < node->as.export_list.count; i++) {
                free(node->as.export_list.local_names[i]);
                free(node->as.export_list.export_names[i]);
            }
            free(node->as.export_list.local_names);
            free(node->as.export_list.export_names);
            break;
        case N_IMPORT:
            ast_free(node->as.import_stmt.path);
            free(node->as.import_stmt.default_name);
            for (size_t i = 0; i < node->as.import_stmt.count; i++) {
                free(node->as.import_stmt.import_names[i]);
                free(node->as.import_stmt.local_names[i]);
            }
            free(node->as.import_stmt.import_names);
            free(node->as.import_stmt.local_names);
            break;
        case N_IF:
            ast_free(node->as.if_stmt.cond);
            ast_free(node->as.if_stmt.then_b);
            ast_free(node->as.if_stmt.else_b);
            break;
        case N_WHILE:
            ast_free(node->as.while_stmt.cond);
            ast_free(node->as.while_stmt.body);
            break;
        case N_RETURN:
            ast_free(node->as.ret_stmt.value);
            break;
        case N_EXPR_STMT:
            ast_free(node->as.expr_stmt.expr);
            break;
        case N_FNDEF:
            free(node->as.fndef.name);
            for (size_t i = 0; i < node->as.fndef.param_count; i++) {
                free(node->as.fndef.params[i]);
            }
            free(node->as.fndef.params);
            ast_free(node->as.fndef.body);
            break;
        case N_BINOP:
            ast_free(node->as.binop.left);
            ast_free(node->as.binop.right);
            break;
        case N_UNOP:
            ast_free(node->as.unop.expr);
            break;
        case N_RANGE:
            ast_free(node->as.range.left);
            ast_free(node->as.range.right);
            break;
        case N_TERNARY:
            ast_free(node->as.ternary.cond);
            ast_free(node->as.ternary.then_e);
            ast_free(node->as.ternary.else_e);
            break;
        case N_CALL:
            ast_free(node->as.call.callee);
            for (size_t i = 0; i < node->as.call.argc; i++) {
                ast_free(node->as.call.args[i]);
            }
            free(node->as.call.args);
            break;
        case N_INDEX:
            ast_free(node->as.index.target);
            ast_free(node->as.index.index);
            break;
        case N_GETFIELD:
            ast_free(node->as.getfield.target);
            free(node->as.getfield.field);
            break;
        case N_OPTGETFIELD:
            ast_free(node->as.getfield.target);
            free(node->as.getfield.field);
            break;
        case N_FUNCLIT:
            for (size_t i = 0; i < node->as.funclit.param_count; i++) {
                free(node->as.funclit.params[i]);
            }
            free(node->as.funclit.params);
            ast_free(node->as.funclit.body);
            break;
        case N_LISTLIT:
            for (size_t i = 0; i < node->as.listlit.count; i++) {
                ast_free(node->as.listlit.items[i]);
            }
            free(node->as.listlit.items);
            break;
        case N_MAPLIT:
            for (size_t i = 0; i < node->as.maplit.count; i++) {
                ast_free(node->as.maplit.keys[i]);
                ast_free(node->as.maplit.vals[i]);
            }
            free(node->as.maplit.keys);
            free(node->as.maplit.vals);
            break;
        case N_PATTERN_LIST:
            for (size_t i = 0; i < node->as.list_pattern.count; i++) {
                free(node->as.list_pattern.names[i]);
            }
            free(node->as.list_pattern.names);
            break;
        case N_PATTERN_MAP:
            for (size_t i = 0; i < node->as.map_pattern.count; i++) {
                free(node->as.map_pattern.keys[i]);
                free(node->as.map_pattern.names[i]);
            }
            free(node->as.map_pattern.keys);
            free(node->as.map_pattern.names);
            break;
        case N_IDENT:
            free(node->as.ident.name);
            break;
        case N_LIT_STR:
            free(node->as.lit_str.s);
            break;
        case N_STR_INTERP:
            for (size_t i = 0; i < node->as.str_interp.count; i++) {
                ast_free(node->as.str_interp.parts[i]);
            }
            free(node->as.str_interp.parts);
            break;
        case N_LIT_INT:
        case N_LIT_FLOAT:
        case N_LIT_BOOL:
        case N_LIT_NIL:
        case N_BREAK:
        case N_CONTINUE:
            // No dynamic allocations
            break;
    }
    
    free(node);
}
