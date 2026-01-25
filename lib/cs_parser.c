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
    if (P->tok.type == TK_STR) {
        // token includes quotes; store raw token for now and unescape later in VM
        ast* n = node(P, N_LIT_STR);
        n->as.lit_str.s = cs_strndup2(P->tok.start, P->tok.len);
        next(P);
        return n;
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

        while (accept(P, TK_DOT)) {
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier after '.'");
                break;
            }
            ast* gf = node(P, N_GETFIELD);
            gf->as.getfield.target = expr;
            gf->as.getfield.field = cs_strndup2(P->tok.start, P->tok.len);
            next(P);
            expr = gf;
        }
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

static ast* parse_cmp(parser* P) {
    ast* left = parse_add(P);
    while (P->tok.type == TK_LT || P->tok.type == TK_LE || P->tok.type == TK_GT || P->tok.type == TK_GE) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_BINOP);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = parse_add(P);
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

static ast* parse_expr(parser* P) {
    return parse_or(P);
}

static void maybe_semi(parser* P) {
    (void)accept(P, TK_SEMI);
}

static ast* parse_lvalue(parser* P) {
    ast* expr = parse_primary(P);
    while (accept(P, TK_LBRACKET)) {
        ast* idx = node(P, N_INDEX);
        idx->as.index.target = expr;
        idx->as.index.index = parse_expr(P);
        expect(P, TK_RBRACKET, "expected ']'");
        expr = idx;
    }
    return expr;
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

static ast* parse_stmt(parser* P) {
    if (accept(P, TK_LET)) {
        ast* n = node(P, N_LET);
        if (P->tok.type != TK_IDENT) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "expected name after let");
            return n;
        }
        n->as.let_stmt.name = cs_strndup2(P->tok.start, P->tok.len);
        next(P);
        if (accept(P, TK_ASSIGN)) {
            n->as.let_stmt.init = parse_expr(P);
        } else {
            n->as.let_stmt.init = NULL;
        }
        maybe_semi(P);
        return n;
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
            n->as.if_stmt.else_b = parse_block(P);
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

    // assignment: lvalue (= expr) where lvalue is IDENT or INDEX
    if (P->tok.type == TK_IDENT) {
        // lookahead with a copy
        lexer saveL = P->L;
        token saveT = P->tok;

        (void)parse_lvalue(P);
        int is_assign = (P->tok.type == TK_ASSIGN);

        // rewind and parse
        P->L = saveL;
        P->tok = saveT;

        if (is_assign) {
            ast* lv = parse_lvalue(P);
            (void)accept(P, TK_ASSIGN);

            ast* rhs = parse_expr(P);
            maybe_semi(P);

            if (lv && lv->type == N_IDENT) {
                ast* n = node(P, N_ASSIGN);
                n->as.assign_stmt.name = lv->as.ident.name;
                lv->as.ident.name = NULL; // transfer ownership
                n->as.assign_stmt.value = rhs;
                return n;
            }
            if (lv && lv->type == N_INDEX) {
                ast* n = node(P, N_SETINDEX);
                n->as.setindex_stmt.target = lv->as.index.target;
                n->as.setindex_stmt.index = lv->as.index.index;
                n->as.setindex_stmt.value = rhs;
                return n;
            }
            if (!P->error) P->error = fmt_err(P, &P->tok, "invalid assignment target");
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
