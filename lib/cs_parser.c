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
    P->saw_yield = 0;
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
static ast* parse_comp_expr(parser* P);
static ast* parse_fn(parser* P, int is_async);
static ast* parse_fn_expr(parser* P, int is_async);
static ast* parse_interpolated_string(parser* P);
static ast* parse_match_expr(parser* P);
static ast* parse_struct(parser* P);
static ast* parse_enum(parser* P);
static ast* parse_list_pattern(parser* P);
static ast* parse_map_pattern(parser* P);
static ast* parse_match_pattern(parser* P);
static ast* make_quoted_str_lit(parser* P, const char* s);
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
    ast** case_patterns = (ast**)malloc(sizeof(ast*) * cap);
    ast** case_blocks = (ast**)malloc(sizeof(ast*) * cap);
    unsigned char* case_kinds = (unsigned char*)malloc(sizeof(unsigned char) * cap);
    int default_seen = 0;

    if (!case_exprs || !case_patterns || !case_blocks || !case_kinds) {
        free(case_exprs);
        free(case_patterns);
        free(case_blocks);
        free(case_kinds);
        if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
        // best-effort parse recovery: consume until end of switch block
        while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF) next(P);
        (void)expect(P, TK_RBRACE, "expected '}' after switch");
        n->as.switch_stmt.case_exprs = NULL;
        n->as.switch_stmt.case_patterns = NULL;
        n->as.switch_stmt.case_blocks = NULL;
        n->as.switch_stmt.case_kinds = NULL;
        n->as.switch_stmt.case_count = 0;
        return n;
    }

    while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF && !P->error) {
        if (accept(P, TK_CASE)) {
            if (cnt == cap) {
                cap *= 2;
                ast** ne = (ast**)realloc(case_exprs, sizeof(ast*) * cap);
                if (!ne) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); break; }
                case_exprs = ne;
                ast** np = (ast**)realloc(case_patterns, sizeof(ast*) * cap);
                if (!np) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); break; }
                case_patterns = np;
                ast** nb = (ast**)realloc(case_blocks, sizeof(ast*) * cap);
                if (!nb) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); break; }
                case_blocks = nb;
                unsigned char* nk = (unsigned char*)realloc(case_kinds, sizeof(unsigned char) * cap);
                if (!nk) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); break; }
                case_kinds = nk;
            }
            int is_pattern = 0;
            if (P->tok.type == TK_LBRACKET || P->tok.type == TK_LBRACE || P->tok.type == TK_PLACEHOLDER) {
                is_pattern = 1;
            } else if (P->tok.type == TK_IDENT) {
                lexer saveL = P->L;
                token saveT = P->tok;
                next(P);
                if (P->tok.type == TK_LPAREN) is_pattern = 1;
                P->L = saveL;
                P->tok = saveT;
            }
            if (is_pattern) {
                case_patterns[cnt] = parse_match_pattern(P);
                case_exprs[cnt] = NULL;
                case_kinds[cnt] = 1;
            } else {
                case_exprs[cnt] = parse_expr(P);
                case_patterns[cnt] = NULL;
                case_kinds[cnt] = 0;
            }
            case_blocks[cnt] = parse_block(P);
            cnt++;
            continue;
        }

        if (accept(P, TK_DEFAULT)) {
            if (default_seen) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "duplicate default in switch");
                break;
            }
            if (cnt == cap) {
                cap *= 2;
                ast** ne = (ast**)realloc(case_exprs, sizeof(ast*) * cap);
                if (!ne) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); break; }
                case_exprs = ne;
                ast** np = (ast**)realloc(case_patterns, sizeof(ast*) * cap);
                if (!np) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); break; }
                case_patterns = np;
                ast** nb = (ast**)realloc(case_blocks, sizeof(ast*) * cap);
                if (!nb) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); break; }
                case_blocks = nb;
                unsigned char* nk = (unsigned char*)realloc(case_kinds, sizeof(unsigned char) * cap);
                if (!nk) { if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory"); break; }
                case_kinds = nk;
            }
            case_exprs[cnt] = NULL;
            case_patterns[cnt] = NULL;
            case_blocks[cnt] = parse_block(P);
            case_kinds[cnt] = 2;
            cnt++;
            default_seen = 1;
            continue;
        }

        if (!P->error) P->error = fmt_err(P, &P->tok, "expected 'case' or 'default' in switch");
        break;
    }

    expect(P, TK_RBRACE, "expected '}' after switch");

    n->as.switch_stmt.case_exprs = case_exprs;
    n->as.switch_stmt.case_patterns = case_patterns;
    n->as.switch_stmt.case_blocks = case_blocks;
    n->as.switch_stmt.case_kinds = case_kinds;
    n->as.switch_stmt.case_count = cnt;
    return n;
}

static ast* parse_fn_expr(parser* P, int is_async) {
    ast* n = node(P, N_FUNCLIT);
    expect(P, TK_LPAREN, "expected '(' after fn");

    size_t cap = 4, cnt = 0;
    char** params = NULL;
    ast** defaults = NULL;
    int seen_default = 0;
    char* rest_param = NULL;

    if (P->tok.type != TK_RPAREN) {
        params = (char**)malloc(sizeof(char*) * cap);
        defaults = (ast**)calloc(cap, sizeof(ast*));
        while (1) {
            if (accept(P, TK_DOTDOTDOT)) {
                if (rest_param) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "duplicate rest parameter");
                    break;
                }
                if (P->tok.type != TK_IDENT) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected rest parameter name");
                    break;
                }
                rest_param = cs_strndup2(P->tok.start, P->tok.len);
                next(P);
                if (accept(P, TK_COMMA)) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "rest parameter must be last");
                }
                break;
            }
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected parameter name");
                break;
            }
            if (cnt == cap) {
                cap *= 2;
                params = (char**)realloc(params, sizeof(char*) * cap);
                defaults = (ast**)realloc(defaults, sizeof(ast*) * cap);
                for (size_t i = cnt; i < cap; i++) defaults[i] = NULL;
            }
            params[cnt] = cs_strndup2(P->tok.start, P->tok.len);
            next(P);

            if (accept(P, TK_ASSIGN)) {
                defaults[cnt] = parse_expr(P);
                seen_default = 1;
            } else if (seen_default) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "non-default parameter after default parameter");
                break;
            }
            cnt++;

            if (accept(P, TK_COMMA)) continue;
            break;
        }
    }
    expect(P, TK_RPAREN, "expected ')' after parameters");

    n->as.funclit.params = params;
    n->as.funclit.defaults = defaults;
    n->as.funclit.param_count = cnt;
    n->as.funclit.rest_param = rest_param;
    n->as.funclit.is_async = is_async;
    int prev_yield = P->saw_yield;
    P->saw_yield = 0;

    if (accept(P, TK_ARROW)) {
        if (P->tok.type == TK_LBRACE) {
            n->as.funclit.body = parse_block(P);
        } else {
            ast* expr = parse_expr(P);
            ast* block = node(P, N_BLOCK);
            ast** items = (ast**)malloc(sizeof(ast*));
            ast* ret = node(P, N_RETURN);
            ret->as.ret_stmt.value = expr;
            items[0] = ret;
            block->as.block.items = items;
            block->as.block.count = 1;
            n->as.funclit.body = block;
        }
    } else {
        n->as.funclit.body = parse_block(P);
    }
    n->as.funclit.is_generator = P->saw_yield ? 1 : 0;
    P->saw_yield = prev_yield;
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
    if (P->tok.type == TK_RAW_STR) {
        // token includes backticks; store raw token for VM to handle
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
        // Could be list literal or list comprehension
        // Lookahead: if we see expr followed by 'for', it's a comprehension
        if (P->tok.type == TK_RBRACKET) {
            // Empty list
            ast* n = node(P, N_LISTLIT);
            next(P);  // consume ]
            n->as.listlit.items = NULL;
            n->as.listlit.count = 0;
            return n;
        }
        
        // Parse first expression (handle spread)
        ast* first_expr = NULL;
        if (accept(P, TK_DOTDOTDOT)) {
            ast* sp = node(P, N_SPREAD);
            sp->as.spread.expr = parse_expr(P);
            first_expr = sp;
        } else {
            first_expr = parse_comp_expr(P);  // Use parse_comp_expr to support Python-style if-else
        }

        // Check if this is a comprehension
        if (first_expr && first_expr->type != N_SPREAD && P->tok.type == TK_FOR) {
            // List comprehension: [expr for var in iterable] or [expr for var1 in iter1 for var2 in iter2]
            ast* n = node(P, N_LISTCOMP);
            n->as.listcomp.expr = first_expr;
            n->as.listcomp.vars = NULL;
            n->as.listcomp.vars2 = NULL;
            n->as.listcomp.iterables = NULL;
            n->as.listcomp.iter_count = 0;
            n->as.listcomp.filter = NULL;

            // Allocate arrays for iteration clauses (start with capacity of 4)
            size_t capacity = 4;
            n->as.listcomp.vars = (char**)malloc(sizeof(char*) * capacity);
            n->as.listcomp.vars2 = (char**)malloc(sizeof(char*) * capacity);
            n->as.listcomp.iterables = (ast**)malloc(sizeof(ast*) * capacity);
            if (!n->as.listcomp.vars || !n->as.listcomp.vars2 || !n->as.listcomp.iterables) {
                ast_free(n);
                if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
                return NULL;
            }

            // Parse all 'for var in iterable' clauses
            while (accept(P, TK_FOR)) {
                int is_destructuring = 0;
                char* var1 = NULL;
                char* var2 = NULL;

                // Check for list destructuring pattern: for [a, b] in ...
                if (P->tok.type == TK_LBRACKET) {
                    is_destructuring = 1;
                    next(P); // consume '['
                    
                    if (P->tok.type != TK_IDENT) {
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier in destructuring pattern");
                        return NULL;
                    }
                    
                    var1 = cs_strndup2(P->tok.start, P->tok.len);
                    next(P);
                    
                    if (!accept(P, TK_COMMA)) {
                        free(var1);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected ',' in destructuring pattern");
                        return NULL;
                    }
                    
                    if (P->tok.type != TK_IDENT) {
                        free(var1);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected second identifier in destructuring pattern");
                        return NULL;
                    }
                    
                    var2 = cs_strndup2(P->tok.start, P->tok.len);
                    next(P);
                    
                    if (!accept(P, TK_RBRACKET)) {
                        free(var1);
                        free(var2);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected ']' after destructuring pattern");
                        return NULL;
                    }
                } else if (P->tok.type == TK_IDENT) {
                    // Regular identifier or comma-separated pair
                    var1 = cs_strndup2(P->tok.start, P->tok.len);
                    next(P);
                    
                    // Check for second variable
                    if (accept(P, TK_COMMA)) {
                        if (P->tok.type != TK_IDENT) {
                            free(var1);
                            ast_free(n);
                            if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier after ','");
                            return NULL;
                        }
                        var2 = cs_strndup2(P->tok.start, P->tok.len);
                        next(P);
                    }
                } else {
                    ast_free(n);
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier or '[' after 'for'");
                    return NULL;
                }

                // Grow arrays if needed
                if (n->as.listcomp.iter_count >= capacity) {
                    capacity *= 2;
                    char** new_vars = (char**)realloc(n->as.listcomp.vars, sizeof(char*) * capacity);
                    char** new_vars2 = (char**)realloc(n->as.listcomp.vars2, sizeof(char*) * capacity);
                    ast** new_iterables = (ast**)realloc(n->as.listcomp.iterables, sizeof(ast*) * capacity);
                    if (!new_vars || !new_vars2 || !new_iterables) {
                        free(var1);
                        free(var2);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
                        return NULL;
                    }
                    n->as.listcomp.vars = new_vars;
                    n->as.listcomp.vars2 = new_vars2;
                    n->as.listcomp.iterables = new_iterables;
                }

                // If destructuring, prefix var1 with '@' to mark it
                if (is_destructuring) {
                    char* prefixed = (char*)malloc(strlen(var1) + 2);
                    if (!prefixed) {
                        free(var1);
                        free(var2);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
                        return NULL;
                    }
                    prefixed[0] = '@';
                    strcpy(prefixed + 1, var1);
                    free(var1);
                    var1 = prefixed;
                }

                n->as.listcomp.vars[n->as.listcomp.iter_count] = var1;
                n->as.listcomp.vars2[n->as.listcomp.iter_count] = var2;

                if (!accept(P, TK_IN)) {
                    ast_free(n);
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected 'in' in comprehension");
                    return NULL;
                }

                n->as.listcomp.iterables[n->as.listcomp.iter_count] = parse_expr(P);
                if (!n->as.listcomp.iterables[n->as.listcomp.iter_count]) {
                    ast_free(n);
                    return NULL;
                }

                n->as.listcomp.iter_count++;
            }

            // Optional filter: if condition
            if (accept(P, TK_IF)) {
                n->as.listcomp.filter = parse_expr(P);
                if (!n->as.listcomp.filter) {
                    ast_free(n);
                    return NULL;
                }
            }
            
            expect(P, TK_RBRACKET, "expected ']'");
            return n;
        }
        
        // Regular list literal
        ast* n = node(P, N_LISTLIT);
        size_t cap = 8, cnt = 1;
        ast** items = (ast**)malloc(sizeof(ast*) * cap);
        items[0] = first_expr;
        
        if (accept(P, TK_COMMA)) {
            if (P->tok.type != TK_RBRACKET) {
                while (1) {
                    if (cnt == cap) { cap *= 2; items = (ast**)realloc(items, sizeof(ast*) * cap); }
                    if (accept(P, TK_DOTDOTDOT)) {
                        ast* sp = node(P, N_SPREAD);
                        sp->as.spread.expr = parse_expr(P);
                        items[cnt++] = sp;
                    } else {
                        items[cnt++] = parse_expr(P);
                    }
                    if (accept(P, TK_COMMA)) {
                        if (P->tok.type == TK_RBRACKET) break;
                        continue;
                    }
                    break;
                }
            }
        }
        
        expect(P, TK_RBRACKET, "expected ']'");
        n->as.listlit.items = items;
        n->as.listlit.count = cnt;
        return n;
    }

    if (accept(P, TK_LBRACE)) {
        // Could be map literal or map comprehension
        if (P->tok.type == TK_RBRACE) {
            // Empty map
            ast* n = node(P, N_MAPLIT);
            next(P);  // consume }
            n->as.maplit.keys = NULL;
            n->as.maplit.vals = NULL;
            n->as.maplit.count = 0;
            return n;
        }

        // Parse first key expression (handle spread)
        ast* first_key = NULL;
        if (accept(P, TK_DOTDOTDOT)) {
            ast* sp = node(P, N_SPREAD);
            sp->as.spread.expr = parse_expr(P);
            first_key = sp;
        } else {
            first_key = parse_comp_expr(P);  // Use parse_comp_expr to support Python-style if-else
        }

        // Declare shared variables for map literal parsing
        // (needed because of goto between different code paths)
        ast* n = NULL;
        size_t cap = 0, cnt = 0;
        ast** keys = NULL;
        ast** vals = NULL;

        // Check for set comprehension: {expr for var in iter} (no colon)
        if (first_key && first_key->type != N_SPREAD && P->tok.type == TK_FOR) {
            // Set comprehension: {expr for var in iterable} or multiple for clauses
            ast* n = node(P, N_SETCOMP);
            n->as.setcomp.expr = first_key;
            n->as.setcomp.vars = NULL;
            n->as.setcomp.vars2 = NULL;
            n->as.setcomp.iterables = NULL;
            n->as.setcomp.iter_count = 0;
            n->as.setcomp.filter = NULL;

            // Allocate arrays for iteration clauses (start with capacity of 4)
            size_t capacity = 4;
            n->as.setcomp.vars = (char**)malloc(sizeof(char*) * capacity);
            n->as.setcomp.vars2 = (char**)malloc(sizeof(char*) * capacity);
            n->as.setcomp.iterables = (ast**)malloc(sizeof(ast*) * capacity);
            if (!n->as.setcomp.vars || !n->as.setcomp.vars2 || !n->as.setcomp.iterables) {
                ast_free(n);
                if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
                return NULL;
            }

            // Parse all 'for var in iterable' clauses
            while (accept(P, TK_FOR)) {
                int is_destructuring = 0;

                // Check for list destructuring pattern: for [a, b] in ...
                if (P->tok.type == TK_LBRACKET) {
                    is_destructuring = 1;
                    next(P); // consume '['
                }

                if (P->tok.type != TK_IDENT) {
                    ast_free(n);
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier after 'for'");
                    return NULL;
                }

                // Grow arrays if needed
                if (n->as.setcomp.iter_count >= capacity) {
                    capacity *= 2;
                    char** new_vars = (char**)realloc(n->as.setcomp.vars, sizeof(char*) * capacity);
                    char** new_vars2 = (char**)realloc(n->as.setcomp.vars2, sizeof(char*) * capacity);
                    ast** new_iterables = (ast**)realloc(n->as.setcomp.iterables, sizeof(ast*) * capacity);
                    if (!new_vars || !new_vars2 || !new_iterables) {
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
                        return NULL;
                    }
                    n->as.setcomp.vars = new_vars;
                    n->as.setcomp.vars2 = new_vars2;
                    n->as.setcomp.iterables = new_iterables;
                }

                // Parse first variable
                char* var1 = cs_strndup2(P->tok.start, P->tok.len);
                next(P);

                // Check for second variable
                char* var2 = NULL;
                if (accept(P, TK_COMMA)) {
                    if (P->tok.type != TK_IDENT) {
                        free(var1);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier after ','");
                        return NULL;
                    }
                    var2 = cs_strndup2(P->tok.start, P->tok.len);
                    next(P);
                }

                // If destructuring, expect closing bracket
                if (is_destructuring) {
                    if (!accept(P, TK_RBRACKET)) {
                        free(var1);
                        free(var2);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected ']' after destructuring pattern");
                        return NULL;
                    }
                    // For destructuring, we need to mark var1 specially
                    // Prefix with '@' to indicate destructuring mode
                    char* prefixed = (char*)malloc(strlen(var1) + 2);
                    prefixed[0] = '@';
                    strcpy(prefixed + 1, var1);
                    free(var1);
                    var1 = prefixed;
                }

                n->as.setcomp.vars[n->as.setcomp.iter_count] = var1;
                n->as.setcomp.vars2[n->as.setcomp.iter_count] = var2;

                if (!accept(P, TK_IN)) {
                    ast_free(n);
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected 'in' in comprehension");
                    return NULL;
                }

                n->as.setcomp.iterables[n->as.setcomp.iter_count] = parse_expr(P);
                if (!n->as.setcomp.iterables[n->as.setcomp.iter_count]) {
                    ast_free(n);
                    return NULL;
                }

                n->as.setcomp.iter_count++;
            }

            // Optional filter: if condition
            if (accept(P, TK_IF)) {
                n->as.setcomp.filter = parse_expr(P);
                if (!n->as.setcomp.filter) {
                    ast_free(n);
                    return NULL;
                }
            }
            
            expect(P, TK_RBRACE, "expected '}'");
            return n;
        }

        // Check for comprehension: {k: v for k, v in iter}
        // Now accepts arbitrary expressions as keys, not just identifiers
        if (first_key && P->tok.type == TK_COLON) {
            // Could be either map literal or map comprehension
            // Save identifier name if key is an identifier (for shorthand syntax)
            char* key_ident = (first_key->type == N_IDENT) ? strdup(first_key->as.ident.name) : NULL;
            next(P);  // consume ':'

            ast* first_val = parse_comp_expr(P);  // Use parse_comp_expr to support Python-style if-else

            if (P->tok.type == TK_FOR) {
                // Map comprehension: {key_expr: val_expr for k, v in iterable} or multiple for clauses
                ast* n = node(P, N_MAPCOMP);
                n->as.mapcomp.key_expr = first_key;
                n->as.mapcomp.val_expr = first_val;
                n->as.mapcomp.key_vars = NULL;
                n->as.mapcomp.val_vars = NULL;
                n->as.mapcomp.iterables = NULL;
                n->as.mapcomp.iter_count = 0;
                n->as.mapcomp.filter = NULL;

                // Allocate arrays for iteration clauses (start with capacity of 4)
                size_t capacity = 4;
                n->as.mapcomp.key_vars = (char**)malloc(sizeof(char*) * capacity);
                n->as.mapcomp.val_vars = (char**)malloc(sizeof(char*) * capacity);
                n->as.mapcomp.iterables = (ast**)malloc(sizeof(ast*) * capacity);
                if (!n->as.mapcomp.key_vars || !n->as.mapcomp.val_vars || !n->as.mapcomp.iterables) {
                    ast_free(n);
                    if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
                    return NULL;
                }

                // Parse all 'for var in iterable' clauses
                while (accept(P, TK_FOR)) {
                    int is_destructuring = 0;

                    // Check for list destructuring pattern: for [a, b] in ...
                    if (P->tok.type == TK_LBRACKET) {
                        is_destructuring = 1;
                        next(P); // consume '['
                    }

                    if (P->tok.type != TK_IDENT) {
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier after 'for'");
                        return NULL;
                    }

                    // Grow arrays if needed
                    if (n->as.mapcomp.iter_count >= capacity) {
                        capacity *= 2;
                        char** new_key_vars = (char**)realloc(n->as.mapcomp.key_vars, sizeof(char*) * capacity);
                        char** new_val_vars = (char**)realloc(n->as.mapcomp.val_vars, sizeof(char*) * capacity);
                        ast** new_iterables = (ast**)realloc(n->as.mapcomp.iterables, sizeof(ast*) * capacity);
                        if (!new_key_vars || !new_val_vars || !new_iterables) {
                            ast_free(n);
                            if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
                            return NULL;
                        }
                        n->as.mapcomp.key_vars = new_key_vars;
                        n->as.mapcomp.val_vars = new_val_vars;
                        n->as.mapcomp.iterables = new_iterables;
                    }

                    // Parse first variable
                    char* var1 = cs_strndup2(P->tok.start, P->tok.len);
                    next(P);

                    // Check for second variable
                    char* var2 = NULL;
                    if (accept(P, TK_COMMA)) {
                        if (P->tok.type != TK_IDENT) {
                            free(var1);
                            ast_free(n);
                            if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier after ','");
                            return NULL;
                        }
                        var2 = cs_strndup2(P->tok.start, P->tok.len);
                        next(P);
                    }

                    // If destructuring, expect closing bracket
                    if (is_destructuring) {
                        if (!accept(P, TK_RBRACKET)) {
                            free(var1);
                            free(var2);
                            ast_free(n);
                            if (!P->error) P->error = fmt_err(P, &P->tok, "expected ']' after destructuring pattern");
                            return NULL;
                        }
                        // For destructuring, we need to mark var1 specially
                        // Prefix with '@' to indicate destructuring mode
                        char* prefixed = (char*)malloc(strlen(var1) + 2);
                        prefixed[0] = '@';
                        strcpy(prefixed + 1, var1);
                        free(var1);
                        var1 = prefixed;
                    }

                    n->as.mapcomp.key_vars[n->as.mapcomp.iter_count] = var1;
                    n->as.mapcomp.val_vars[n->as.mapcomp.iter_count] = var2;

                    if (!accept(P, TK_IN)) {
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected 'in' in comprehension");
                        return NULL;
                    }

                    n->as.mapcomp.iterables[n->as.mapcomp.iter_count] = parse_expr(P);
                    if (!n->as.mapcomp.iterables[n->as.mapcomp.iter_count]) {
                        ast_free(n);
                        return NULL;
                    }

                    n->as.mapcomp.iter_count++;
                }

                // Optional filter
                if (accept(P, TK_IF)) {
                    n->as.mapcomp.filter = parse_expr(P);
                    if (!n->as.mapcomp.filter) {
                        ast_free(n);
                        return NULL;
                    }
                }
                
                expect(P, TK_RBRACE, "expected '}'");
                if (key_ident) free(key_ident);
                return n;
            }

            // Regular map literal
            // Only apply shorthand key conversion if key was an identifier
            if (key_ident) {
                ast* key_lit = make_quoted_str_lit(P, first_key->as.ident.name);
                ast_free(first_key);
                first_key = key_lit;
                free(key_ident);
            }

            n = node(P, N_MAPLIT);
            cap = 8;
            cnt = 1;
            keys = (ast**)malloc(sizeof(ast*) * cap);
            vals = (ast**)malloc(sizeof(ast*) * cap);
            keys[0] = first_key;
            vals[0] = first_val;
            
            if (accept(P, TK_COMMA)) {
                if (P->tok.type != TK_RBRACE) {
                    goto continue_map_literal;
                }
            }
            
            expect(P, TK_RBRACE, "expected '}'");
            n->as.maplit.keys = keys;
            n->as.maplit.vals = vals;
            n->as.maplit.count = cnt;
            return n;
        }

        // Regular map literal
        n = node(P, N_MAPLIT);
        cap = 8;
        cnt = 1;
        keys = (ast**)malloc(sizeof(ast*) * cap);
        vals = (ast**)malloc(sizeof(ast*) * cap);

        if (first_key && first_key->type == N_SPREAD) {
            keys[0] = first_key;
            vals[0] = NULL;
        } else {
            keys[0] = first_key;
            expect(P, TK_COLON, "expected ':' in map literal");
            vals[0] = parse_expr(P);
        }
        
        if (accept(P, TK_COMMA)) {
            if (P->tok.type != TK_RBRACE) {
                continue_map_literal:
                while (1) {
                    if (cnt == cap) {
                        cap *= 2;
                        keys = (ast**)realloc(keys, sizeof(ast*) * cap);
                        vals = (ast**)realloc(vals, sizeof(ast*) * cap);
                    }
                    if (accept(P, TK_DOTDOTDOT)) {
                        ast* sp = node(P, N_SPREAD);
                        sp->as.spread.expr = parse_expr(P);
                        keys[cnt] = sp;
                        vals[cnt] = NULL;
                    } else {
                        keys[cnt] = parse_expr(P);
                        if (keys[cnt] && keys[cnt]->type == N_IDENT && P->tok.type == TK_COLON) {
                            ast* key_lit = make_quoted_str_lit(P, keys[cnt]->as.ident.name);
                            ast_free(keys[cnt]);
                            keys[cnt] = key_lit;
                        }
                        expect(P, TK_COLON, "expected ':' in map literal");
                        vals[cnt] = parse_expr(P);
                    }
                    cnt++;
                    if (accept(P, TK_COMMA)) {
                        if (P->tok.type == TK_RBRACE) break;
                        continue;
                    }
                    break;
                }
            }
        }
        
        expect(P, TK_RBRACE, "expected '}'");
        n->as.maplit.keys = keys;
        n->as.maplit.vals = vals;
        n->as.maplit.count = cnt;
        return n;
    }

    if (accept(P, TK_HASH_LBRACE)) {
        // Set literal: #{expr, expr, ...} or set comprehension: #{expr for var in iterable}
        if (P->tok.type == TK_RBRACE) {
            // Empty set
            ast* n = node(P, N_SETLIT);
            next(P);  // consume }
            n->as.setlit.items = NULL;
            n->as.setlit.count = 0;
            return n;
        }

        // Parse first expression (handle spread)
        ast* first_expr = NULL;
        if (accept(P, TK_DOTDOTDOT)) {
            ast* sp = node(P, N_SPREAD);
            sp->as.spread.expr = parse_expr(P);
            first_expr = sp;
        } else {
            first_expr = parse_comp_expr(P);
        }

        // Check if this is a set comprehension
        if (first_expr && first_expr->type != N_SPREAD && P->tok.type == TK_FOR) {
            // Set comprehension: #{expr for var in iterable}
            ast* n = node(P, N_SETCOMP);
            n->as.setcomp.expr = first_expr;
            n->as.setcomp.vars = NULL;
            n->as.setcomp.vars2 = NULL;
            n->as.setcomp.iterables = NULL;
            n->as.setcomp.iter_count = 0;
            n->as.setcomp.filter = NULL;

            // Allocate arrays for iteration clauses
            size_t capacity = 4;
            n->as.setcomp.vars = (char**)malloc(sizeof(char*) * capacity);
            n->as.setcomp.vars2 = (char**)malloc(sizeof(char*) * capacity);
            n->as.setcomp.iterables = (ast**)malloc(sizeof(ast*) * capacity);
            if (!n->as.setcomp.vars || !n->as.setcomp.vars2 || !n->as.setcomp.iterables) {
                ast_free(n);
                if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
                return NULL;
            }

            // Parse all 'for var in iterable' clauses
            while (accept(P, TK_FOR)) {
                int is_destructuring = 0;
                char* var1 = NULL;
                char* var2 = NULL;

                if (P->tok.type == TK_LBRACKET) {
                    is_destructuring = 1;
                    next(P);
                    
                    if (P->tok.type != TK_IDENT) {
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier in destructuring pattern");
                        return NULL;
                    }
                    
                    var1 = cs_strndup2(P->tok.start, P->tok.len);
                    next(P);
                    
                    if (!accept(P, TK_COMMA)) {
                        free(var1);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected ',' in destructuring pattern");
                        return NULL;
                    }
                    
                    if (P->tok.type != TK_IDENT) {
                        free(var1);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected second identifier in destructuring pattern");
                        return NULL;
                    }
                    
                    var2 = cs_strndup2(P->tok.start, P->tok.len);
                    next(P);
                    
                    if (!accept(P, TK_RBRACKET)) {
                        free(var1);
                        free(var2);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected ']' after destructuring pattern");
                        return NULL;
                    }
                } else if (P->tok.type == TK_IDENT) {
                    var1 = cs_strndup2(P->tok.start, P->tok.len);
                    next(P);
                    
                    if (accept(P, TK_COMMA)) {
                        if (P->tok.type != TK_IDENT) {
                            free(var1);
                            ast_free(n);
                            if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier after ','");
                            return NULL;
                        }
                        var2 = cs_strndup2(P->tok.start, P->tok.len);
                        next(P);
                    }
                } else {
                    ast_free(n);
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected identifier or '[' after 'for'");
                    return NULL;
                }

                // Grow arrays if needed
                if (n->as.setcomp.iter_count >= capacity) {
                    capacity *= 2;
                    char** new_vars = (char**)realloc(n->as.setcomp.vars, sizeof(char*) * capacity);
                    char** new_vars2 = (char**)realloc(n->as.setcomp.vars2, sizeof(char*) * capacity);
                    ast** new_iterables = (ast**)realloc(n->as.setcomp.iterables, sizeof(ast*) * capacity);
                    if (!new_vars || !new_vars2 || !new_iterables) {
                        free(var1);
                        free(var2);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
                        return NULL;
                    }
                    n->as.setcomp.vars = new_vars;
                    n->as.setcomp.vars2 = new_vars2;
                    n->as.setcomp.iterables = new_iterables;
                }

                if (is_destructuring) {
                    char* prefixed = (char*)malloc(strlen(var1) + 2);
                    if (!prefixed) {
                        free(var1);
                        free(var2);
                        ast_free(n);
                        if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
                        return NULL;
                    }
                    prefixed[0] = '@';
                    strcpy(prefixed + 1, var1);
                    free(var1);
                    var1 = prefixed;
                }

                n->as.setcomp.vars[n->as.setcomp.iter_count] = var1;
                n->as.setcomp.vars2[n->as.setcomp.iter_count] = var2;

                if (!accept(P, TK_IN)) {
                    ast_free(n);
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected 'in' in comprehension");
                    return NULL;
                }

                n->as.setcomp.iterables[n->as.setcomp.iter_count] = parse_expr(P);
                if (!n->as.setcomp.iterables[n->as.setcomp.iter_count]) {
                    ast_free(n);
                    return NULL;
                }

                n->as.setcomp.iter_count++;
            }

            // Optional filter
            if (accept(P, TK_IF)) {
                n->as.setcomp.filter = parse_expr(P);
                if (!n->as.setcomp.filter) {
                    ast_free(n);
                    return NULL;
                }
            }
            
            expect(P, TK_RBRACE, "expected '}'");
            return n;
        }

        // Regular set literal
        ast* n = node(P, N_SETLIT);
        size_t cap = 8, cnt = 1;
        ast** items = (ast**)malloc(sizeof(ast*) * cap);
        items[0] = first_expr;
        
        if (accept(P, TK_COMMA)) {
            if (P->tok.type != TK_RBRACE) {
                while (1) {
                    if (cnt == cap) { cap *= 2; items = (ast**)realloc(items, sizeof(ast*) * cap); }
                    if (accept(P, TK_DOTDOTDOT)) {
                        ast* sp = node(P, N_SPREAD);
                        sp->as.spread.expr = parse_expr(P);
                        items[cnt++] = sp;
                    } else {
                        items[cnt++] = parse_expr(P);
                    }
                    if (accept(P, TK_COMMA)) {
                        if (P->tok.type == TK_RBRACE) break;
                        continue;
                    }
                    break;
                }
            }
        }
        
        expect(P, TK_RBRACE, "expected '}'");
        n->as.setlit.items = items;
        n->as.setlit.count = cnt;
        return n;
    }

    if (accept(P, TK_ASYNC)) {
        if (!accept(P, TK_FN)) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "expected 'fn' after async");
            return node(P, N_LIT_NIL);
        }
        return parse_fn_expr(P, 1);
    }

    if (accept(P, TK_FN)) {
        return parse_fn_expr(P, 0);
    }

    if (accept(P, TK_LPAREN)) {
        // Could be grouped expression or tuple literal
        // Tuple patterns:
        //   - Empty: ()
        //   - Named: (name: value, ...)
        //   - Positional with comma: (value, value, ...)
        //   - Single with trailing comma: (value,)
        // Grouped: (expr)
        
        if (P->tok.type == TK_RPAREN) {
            // Empty tuple: ()
            expect(P, TK_RPAREN, "expected ')'");
            ast* n = node(P, N_TUPLELIT);
            n->as.tuplelit.field_names = NULL;
            n->as.tuplelit.field_values = NULL;
            n->as.tuplelit.count = 0;
            return n;
        }
        
        // Check for named tuple: identifier followed by colon
        int is_named_tuple = 0;
        if (P->tok.type == TK_IDENT) {
            // Save position to potentially backtrack
            token saved_tok = P->tok;
            size_t saved_pos = P->L.pos;
            int saved_line = P->L.line;
            int saved_col = P->L.col;
            
            next(P); // consume identifier
            if (P->tok.type == TK_COLON) {
                is_named_tuple = 1;
            }
            
            // Restore position
            P->tok = saved_tok;
            P->L.pos = saved_pos;
            P->L.line = saved_line;
            P->L.col = saved_col;
        }
        
        if (is_named_tuple) {
            // Parse named tuple: (name: value, name: value, ...)
            ast* n = node(P, N_TUPLELIT);
            size_t cap = 4, cnt = 0;
            char** names = (char**)malloc(sizeof(char*) * cap);
            ast** vals = (ast**)malloc(sizeof(ast*) * cap);
            
            while (P->tok.type != TK_RPAREN) {
                if (cnt == cap) {
                    cap *= 2;
                    names = (char**)realloc(names, sizeof(char*) * cap);
                    vals = (ast**)realloc(vals, sizeof(ast*) * cap);
                }
                
                // Expect identifier
                if (P->tok.type != TK_IDENT) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected field name");
                    free(names);
                    free(vals);
                    return NULL;
                }
                
                names[cnt] = cs_strndup2(P->tok.start, P->tok.len);
                next(P);
                
                expect(P, TK_COLON, "expected ':' after field name");
                vals[cnt] = parse_expr(P);
                cnt++;
                
                if (!accept(P, TK_COMMA)) break;
            }
            
            expect(P, TK_RPAREN, "expected ')'");
            n->as.tuplelit.field_names = names;
            n->as.tuplelit.field_values = vals;
            n->as.tuplelit.count = cnt;
            return n;
        }
        
        // Parse first element/expression for positional tuple or grouped expression
        ast* first = parse_expr(P);
        
        if (accept(P, TK_COMMA)) {
            // Could be tuple: (expr, ...) or (expr,)
            // Check for trailing comma case: (expr,)
            if (P->tok.type == TK_RPAREN) {
                // Single element tuple with trailing comma
                ast* n = node(P, N_TUPLELIT);
                char** names = (char**)calloc(1, sizeof(char*));
                ast** vals = (ast**)malloc(sizeof(ast*));
                names[0] = NULL;
                vals[0] = first;
                expect(P, TK_RPAREN, "expected ')'");
                n->as.tuplelit.field_names = names;
                n->as.tuplelit.field_values = vals;
                n->as.tuplelit.count = 1;
                return n;
            }
            
            // Multi-element positional tuple: (expr, expr, ...)
            ast* n = node(P, N_TUPLELIT);
            size_t cap = 4, cnt = 1;
            char** names = (char**)calloc(cap, sizeof(char*));
            ast** vals = (ast**)malloc(sizeof(ast*) * cap);
            names[0] = NULL;
            vals[0] = first;
            
            // Parse remaining elements
            while (P->tok.type != TK_RPAREN) {
                if (cnt == cap) {
                    cap *= 2;
                    names = (char**)realloc(names, sizeof(char*) * cap);
                    vals = (ast**)realloc(vals, sizeof(ast*) * cap);
                }
                names[cnt] = NULL;
                vals[cnt] = parse_expr(P);
                cnt++;
                if (!accept(P, TK_COMMA)) break;
            }
            
            expect(P, TK_RPAREN, "expected ')'");
            n->as.tuplelit.field_names = names;
            n->as.tuplelit.field_values = vals;
            n->as.tuplelit.count = cnt;
            return n;
        } else {
            // No comma after first expr: grouped expression (expr)
            expect(P, TK_RPAREN, "expected ')'");
            return first;
        }
    }

    if (P->tok.type == TK_PLACEHOLDER) {
        ast* expr = node(P, N_PLACEHOLDER);
        next(P);
        return expr;
    }

    if (P->tok.type == TK_IDENT || P->tok.type == TK_SELF || P->tok.type == TK_SUPER) {
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
                    if (accept(P, TK_DOTDOTDOT)) {
                        ast* sp = node(P, N_SPREAD);
                        sp->as.spread.expr = parse_expr(P);
                        args[cnt++] = sp;
                    } else {
                        args[cnt++] = parse_expr(P);
                    }
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
    if (accept(P, TK_AWAIT)) {
        ast* n = node(P, N_AWAIT);
        n->as.await_expr.expr = parse_unary(P);
        return n;
    }
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

static ast* parse_bitwise_and(parser* P) {
    ast* left = parse_eq(P);
    while (P->tok.type == TK_AMP) {
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

static ast* parse_bitwise_xor(parser* P) {
    ast* left = parse_bitwise_and(P);
    while (P->tok.type == TK_CARET) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_BINOP);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = parse_bitwise_and(P);
        left = n;
    }
    return left;
}

static ast* parse_bitwise_or(parser* P) {
    ast* left = parse_bitwise_xor(P);
    while (P->tok.type == TK_BAR) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_BINOP);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = parse_bitwise_xor(P);
        left = n;
    }
    return left;
}

static ast* parse_and(parser* P) {
    ast* left = parse_bitwise_or(P);
    while (P->tok.type == TK_ANDAND) {
        int op = P->tok.type;
        next(P);
        ast* n = node(P, N_BINOP);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = parse_bitwise_or(P);
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

static ast* parse_pipe(parser* P) {
    ast* left = parse_nullish(P);
    while (accept(P, TK_PIPE)) {
        ast* n = node(P, N_PIPE);
        n->as.pipe.left = left;
        n->as.pipe.right = parse_nullish(P);
        left = n;
    }
    return left;
}

static ast* parse_expr(parser* P) {
    ast* cond = parse_pipe(P);
    if (!cond) return NULL;  // Guard against parse errors

    // Check for walrus operator (:=)
    if (accept(P, TK_WALRUS)) {
        if (cond->type != N_IDENT) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "walrus operator requires identifier on left side");
            ast_free(cond);
            return NULL;
        }
        ast* n = node(P, N_WALRUS);
        n->as.walrus.name = cond->as.ident.name;
        cond->as.ident.name = NULL; // transfer ownership
        n->as.walrus.value = parse_expr(P);
        ast_free(cond);
        return n;
    }
    
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

// Parse comprehension expression with support for Python-style if-else
// Supports both: expr ? a : b  and  a if expr else b
static ast* parse_comp_expr(parser* P) {
    ast* first = parse_pipe(P);
    
    // Check for Python-style: then_val if condition else else_val
    if (P->tok.type == TK_IF && first) {
        next(P);  // consume 'if'
        ast* condition = parse_pipe(P);
        
        if (accept(P, TK_ELSE)) {
            ast* else_val = parse_comp_expr(P);  // Recursive for right-associativity
            
            // Create ternary node with reordered parts
            ast* n = node(P, N_TERNARY);
            n->as.ternary.cond = condition;
            n->as.ternary.then_e = first;
            n->as.ternary.else_e = else_val;
            return n;
        } else {
            // Just 'if' without 'else' - this is an error in this context
            if (!P->error) {
                P->error = fmt_err(P, &P->tok, "expected 'else' after 'if' in conditional expression");
            }
            ast_free(first);
            ast_free(condition);
            return NULL;
        }
    }
    
    // Check for traditional ternary: condition ? then_val : else_val
    if (accept(P, TK_QMARK)) {
        ast* n = node(P, N_TERNARY);
        n->as.ternary.cond = first;
        n->as.ternary.then_e = parse_comp_expr(P);
        expect(P, TK_COLON, "expected ':' in ternary");
        n->as.ternary.else_e = parse_comp_expr(P);
        return n;
    }
    
    return first;
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
    char* rest_name = NULL;

    if (P->tok.type != TK_RBRACKET) {
        names = (char**)malloc(sizeof(char*) * cap);
        while (1) {
            if (accept(P, TK_DOTDOTDOT)) {
                if (rest_name) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "duplicate rest pattern");
                    break;
                }
                if (P->tok.type != TK_IDENT) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected rest name in list pattern");
                    break;
                }
                rest_name = cs_strndup2(P->tok.start, P->tok.len);
                next(P);
                if (accept(P, TK_COMMA)) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "rest pattern must be last");
                }
                break;
            }
            if (P->tok.type != TK_IDENT && P->tok.type != TK_PLACEHOLDER) {
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
    n->as.list_pattern.rest_name = rest_name;
    return n;
}

static ast* parse_map_pattern(parser* P) {
    ast* n = node(P, N_PATTERN_MAP);
    size_t cap = 4, cnt = 0;
    char** keys = NULL;
    char** names = NULL;
    char* rest_name = NULL;

    if (P->tok.type != TK_RBRACE) {
        keys = (char**)malloc(sizeof(char*) * cap);
        names = (char**)malloc(sizeof(char*) * cap);
        while (1) {
            if (accept(P, TK_DOTDOTDOT)) {
                if (rest_name) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "duplicate rest pattern");
                    break;
                }
                if (P->tok.type != TK_IDENT) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected rest name in map pattern");
                    break;
                }
                rest_name = cs_strndup2(P->tok.start, P->tok.len);
                next(P);
                if (accept(P, TK_COMMA)) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "rest pattern must be last");
                }
                break;
            }
            if (P->tok.type != TK_IDENT && P->tok.type != TK_PLACEHOLDER) {
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
    n->as.map_pattern.rest_name = rest_name;
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
    ast** case_patterns = (ast**)malloc(sizeof(ast*) * cap);
    ast** case_guards = (ast**)calloc(cap, sizeof(ast*));
    ast** case_values = (ast**)malloc(sizeof(ast*) * cap);
    ast* default_expr = NULL;

    if (!case_patterns || !case_guards || !case_values) {
        free(case_patterns);
        free(case_guards);
        free(case_values);
        if (!P->error) P->error = fmt_err(P, &P->tok, "out of memory");
        while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF) next(P);
        (void)expect(P, TK_RBRACE, "expected '}' after match");
        n->as.match_expr.case_patterns = NULL;
        n->as.match_expr.case_guards = NULL;
        n->as.match_expr.case_values = NULL;
        n->as.match_expr.case_count = 0;
        n->as.match_expr.default_expr = NULL;
        return n;
    }

    while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF && !P->error) {
        if (accept(P, TK_CASE)) {
            if (cnt == cap) {
                cap *= 2;
                case_patterns = (ast**)realloc(case_patterns, sizeof(ast*) * cap);
                case_guards = (ast**)realloc(case_guards, sizeof(ast*) * cap);
                case_values = (ast**)realloc(case_values, sizeof(ast*) * cap);
            }
            case_patterns[cnt] = parse_match_pattern(P);
            case_guards[cnt] = NULL;
            if (accept(P, TK_IF)) {
                case_guards[cnt] = parse_expr(P);
            }
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

    n->as.match_expr.case_patterns = case_patterns;
    n->as.match_expr.case_guards = case_guards;
    n->as.match_expr.case_values = case_values;
    n->as.match_expr.case_count = cnt;
    n->as.match_expr.default_expr = default_expr;
    return n;
}

static ast* parse_match_pattern(parser* P) {
    if (accept(P, TK_LBRACKET)) return parse_list_pattern(P);
    if (accept(P, TK_LBRACE)) return parse_map_pattern(P);

    if (P->tok.type == TK_PLACEHOLDER) {
        ast* n = node(P, N_PATTERN_WILDCARD);
        next(P);
        return n;
    }
    if (P->tok.type == TK_IDENT || P->tok.type == TK_SELF || P->tok.type == TK_SUPER) {
        char* name = cs_strndup2(P->tok.start, P->tok.len);
        next(P);
        if (accept(P, TK_LPAREN)) {
            ast* inner = parse_match_pattern(P);
            expect(P, TK_RPAREN, "expected ')' after type pattern");
            ast* n = node(P, N_PATTERN_TYPE);
            n->as.type_pattern.type_name = name;
            n->as.type_pattern.inner = inner;
            return n;
        }
        ast* n = node(P, N_IDENT);
        n->as.ident.name = name;
        return n;
    }

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
        ast* n = make_quoted_str_lit_from_tok(P, P->tok.start, P->tok.len);
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

    if (!P->error) P->error = fmt_err(P, &P->tok, "expected pattern in match case");
    return node(P, N_LIT_NIL);
}

static ast* parse_class(parser* P) {
    ast* n = node(P, N_CLASS);
    if (P->tok.type != TK_IDENT) {
        if (!P->error) P->error = fmt_err(P, &P->tok, "expected class name");
        return n;
    }
    n->as.class_stmt.name = cs_strndup2(P->tok.start, P->tok.len);
    next(P);

    if (accept(P, TK_COLON)) {
        if (P->tok.type != TK_IDENT) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "expected parent class name");
        } else {
            n->as.class_stmt.parent = cs_strndup2(P->tok.start, P->tok.len);
            next(P);
        }
    }

    expect(P, TK_LBRACE, "expected '{' after class name");

    size_t cap = 4, cnt = 0;
    ast** methods = NULL;
    if (P->tok.type != TK_RBRACE) {
        methods = (ast**)malloc(sizeof(ast*) * cap);
        while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF && !P->error) {
            int is_async = 0;
            if (accept(P, TK_ASYNC)) is_async = 1;
            if (!accept(P, TK_FN)) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected 'fn' in class body");
                break;
            }
            if (cnt == cap) { cap *= 2; methods = (ast**)realloc(methods, sizeof(ast*) * cap); }
            methods[cnt++] = parse_fn(P, is_async);
            if (P->tok.type == TK_SEMI) maybe_semi(P);
        }
    }
    expect(P, TK_RBRACE, "expected '}' after class body");

    n->as.class_stmt.methods = methods;
    n->as.class_stmt.method_count = cnt;
    return n;
}

static ast* parse_struct(parser* P) {
    ast* n = node(P, N_STRUCT);
    if (P->tok.type != TK_IDENT) {
        if (!P->error) P->error = fmt_err(P, &P->tok, "expected struct name");
        return n;
    }
    n->as.struct_stmt.name = cs_strndup2(P->tok.start, P->tok.len);
    next(P);

    expect(P, TK_LBRACE, "expected '{' after struct name");

    size_t cap = 4, cnt = 0;
    char** fields = NULL;
    ast** defaults = NULL;
    if (P->tok.type != TK_RBRACE) {
        fields = (char**)malloc(sizeof(char*) * cap);
        defaults = (ast**)calloc(cap, sizeof(ast*));
        while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF && !P->error) {
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected field name in struct");
                break;
            }
            if (cnt == cap) {
                cap *= 2;
                fields = (char**)realloc(fields, sizeof(char*) * cap);
                defaults = (ast**)realloc(defaults, sizeof(ast*) * cap);
                for (size_t i = cnt; i < cap; i++) defaults[i] = NULL;
            }
            fields[cnt] = cs_strndup2(P->tok.start, P->tok.len);
            next(P);

            if (accept(P, TK_ASSIGN)) {
                defaults[cnt] = parse_expr(P);
            }
            cnt++;

            if (accept(P, TK_COMMA) || accept(P, TK_SEMI)) {
                if (P->tok.type == TK_RBRACE) break;
                continue;
            }
            break;
        }
    }
    expect(P, TK_RBRACE, "expected '}' after struct body");

    n->as.struct_stmt.field_names = fields;
    n->as.struct_stmt.field_defaults = defaults;
    n->as.struct_stmt.field_count = cnt;
    return n;
}

static ast* parse_enum(parser* P) {
    ast* n = node(P, N_ENUM);
    if (P->tok.type != TK_IDENT) {
        if (!P->error) P->error = fmt_err(P, &P->tok, "expected enum name");
        return n;
    }
    n->as.enum_stmt.name = cs_strndup2(P->tok.start, P->tok.len);
    next(P);

    expect(P, TK_LBRACE, "expected '{' after enum name");

    size_t cap = 4, cnt = 0;
    char** names = NULL;
    ast** values = NULL;
    if (P->tok.type != TK_RBRACE) {
        names = (char**)malloc(sizeof(char*) * cap);
        values = (ast**)calloc(cap, sizeof(ast*));
        while (P->tok.type != TK_RBRACE && P->tok.type != TK_EOF && !P->error) {
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected enum member name");
                break;
            }
            if (cnt == cap) {
                cap *= 2;
                names = (char**)realloc(names, sizeof(char*) * cap);
                values = (ast**)realloc(values, sizeof(ast*) * cap);
                for (size_t i = cnt; i < cap; i++) values[i] = NULL;
            }
            names[cnt] = cs_strndup2(P->tok.start, P->tok.len);
            next(P);

            if (accept(P, TK_ASSIGN)) {
                values[cnt] = parse_expr(P);
            }
            cnt++;

            if (accept(P, TK_COMMA) || accept(P, TK_SEMI)) {
                if (P->tok.type == TK_RBRACE) break;
                continue;
            }
            break;
        }
    }
    expect(P, TK_RBRACE, "expected '}' after enum body");

    n->as.enum_stmt.names = names;
    n->as.enum_stmt.values = values;
    n->as.enum_stmt.count = cnt;
    return n;
}

static ast* parse_fn(parser* P, int is_async) {
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
    ast** defaults = NULL;
    int seen_default = 0;
    char* rest_param = NULL;

    if (P->tok.type != TK_RPAREN) {
        params = (char**)malloc(sizeof(char*) * cap);
        defaults = (ast**)calloc(cap, sizeof(ast*));
        while (1) {
            if (accept(P, TK_DOTDOTDOT)) {
                if (rest_param) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "duplicate rest parameter");
                    break;
                }
                if (P->tok.type != TK_IDENT) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected rest parameter name");
                    break;
                }
                rest_param = cs_strndup2(P->tok.start, P->tok.len);
                next(P);
                if (accept(P, TK_COMMA)) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "rest parameter must be last");
                }
                break;
            }
            if (P->tok.type != TK_IDENT) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "expected parameter name");
                break;
            }
            if (cnt == cap) {
                cap *= 2;
                params = (char**)realloc(params, sizeof(char*) * cap);
                defaults = (ast**)realloc(defaults, sizeof(ast*) * cap);
                for (size_t i = cnt; i < cap; i++) defaults[i] = NULL;
            }
            params[cnt] = cs_strndup2(P->tok.start, P->tok.len);
            next(P);

            if (accept(P, TK_ASSIGN)) {
                defaults[cnt] = parse_expr(P);
                seen_default = 1;
            } else if (seen_default) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "non-default parameter after default parameter");
                break;
            }
            cnt++;

            if (accept(P, TK_COMMA)) continue;
            break;
        }
    }
    expect(P, TK_RPAREN, "expected ')' after parameters");

    n->as.fndef.params = params;
    n->as.fndef.defaults = defaults;
    n->as.fndef.param_count = cnt;
    n->as.fndef.rest_param = rest_param;
    n->as.fndef.is_async = is_async;

    int prev_yield = P->saw_yield;
    P->saw_yield = 0;

    if (accept(P, TK_ARROW)) {
        if (P->tok.type == TK_LBRACE) {
            n->as.fndef.body = parse_block(P);
        } else {
            ast* expr = parse_expr(P);
            ast* block = node(P, N_BLOCK);
            ast** items = (ast**)malloc(sizeof(ast*));
            ast* ret = node(P, N_RETURN);
            ret->as.ret_stmt.value = expr;
            items[0] = ret;
            block->as.block.items = items;
            block->as.block.count = 1;
            n->as.fndef.body = block;
        }
    } else {
        n->as.fndef.body = parse_block(P);
    }
    n->as.fndef.is_generator = P->saw_yield ? 1 : 0;
    P->saw_yield = prev_yield;
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

    if (accept(P, TK_STRUCT)) {
        return parse_struct(P);
    }

    if (accept(P, TK_ENUM)) {
        return parse_enum(P);
    }

    if (accept(P, TK_CLASS)) {
        return parse_class(P);
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
                } else if (P->tok.type == TK_IDENT) {
                    // Check if this is an assignment (lookahead)
                    lexer saveL = P->L;
                    token saveT = P->tok;

                    (void)parse_lvalue(P);
                    token_type assign_op = P->tok.type;
                    int is_assign = (assign_op == TK_ASSIGN || assign_op == TK_PLUSEQ || assign_op == TK_MINUSEQ ||
                                     assign_op == TK_STAREQ || assign_op == TK_SLASHEQ);

                    // Rewind
                    P->L = saveL;
                    P->tok = saveT;

                    if (is_assign) {
                        // Parse as assignment
                        ast* lv = parse_lvalue(P);
                        token_type op = P->tok.type;
                        next(P);
                        ast* rhs = parse_expr(P);
                        n->as.for_c_style_stmt.init = build_assignment(P, lv, op, rhs);
                    } else {
                        // Parse as expression
                        n->as.for_c_style_stmt.init = parse_expr(P);
                    }
                } else {
                    // Parse as expression (handles walrus operator)
                    n->as.for_c_style_stmt.init = parse_expr(P);
                }
            } else {
                n->as.for_c_style_stmt.init = NULL;
            }
            expect(P, TK_SEMI, "expected ';' after for loop init");
            
            // Parse condition (can be empty) - use parse_expr to support walrus
            if (P->tok.type != TK_SEMI) {
                n->as.for_c_style_stmt.cond = parse_expr(P);
            } else {
                n->as.for_c_style_stmt.cond = NULL;
            }
            expect(P, TK_SEMI, "expected ';' after for loop condition");
            
            // Parse increment (can be empty) - can be assignment or expression
            if (P->tok.type != TK_RPAREN) {
                if (P->tok.type == TK_IDENT) {
                    // Check if this is an assignment (lookahead)
                    lexer saveL = P->L;
                    token saveT = P->tok;

                    (void)parse_lvalue(P);
                    token_type assign_op = P->tok.type;
                    int is_assign = (assign_op == TK_ASSIGN || assign_op == TK_PLUSEQ || assign_op == TK_MINUSEQ ||
                                     assign_op == TK_STAREQ || assign_op == TK_SLASHEQ);

                    // Rewind
                    P->L = saveL;
                    P->tok = saveT;

                    if (is_assign) {
                        // Parse as assignment
                        ast* lv = parse_lvalue(P);
                        token_type op = P->tok.type;
                        next(P);
                        ast* rhs = parse_expr(P);
                        n->as.for_c_style_stmt.incr = build_assignment(P, lv, op, rhs);
                    } else {
                        // Parse as expression
                        n->as.for_c_style_stmt.incr = parse_expr(P);
                    }
                } else {
                    // Parse as expression (handles walrus operator)
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
            // Also supports: for name, name2 in iterable { body }
            // Also supports destructuring: for [name, name2] in iterable { body }
            ast* n = node(P, N_FORIN);

            // Check for destructuring syntax
            if (accept(P, TK_LBRACKET)) {
                // Parse [name, name2] destructuring pattern
                if (P->tok.type != TK_IDENT) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected first variable name in destructuring pattern");
                    return n;
                }
                n->as.forin_stmt.name = cs_strndup2(P->tok.start, P->tok.len);
                next(P);

                if (!accept(P, TK_COMMA)) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected ',' in destructuring pattern");
                    return n;
                }

                if (P->tok.type != TK_IDENT) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected second variable name in destructuring pattern");
                    return n;
                }
                n->as.forin_stmt.name2 = cs_strndup2(P->tok.start, P->tok.len);
                next(P);

                if (!accept(P, TK_RBRACKET)) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected ']' after destructuring pattern");
                    return n;
                }
            } else {
                // Original syntax: name or name, name2
                if (P->tok.type != TK_IDENT) {
                    if (!P->error) P->error = fmt_err(P, &P->tok, "expected loop variable name");
                    return n;
                }
                n->as.forin_stmt.name = cs_strndup2(P->tok.start, P->tok.len);
                n->as.forin_stmt.name2 = NULL;
                next(P);

                // Check for second variable
                if (accept(P, TK_COMMA)) {
                    if (P->tok.type != TK_IDENT) {
                        if (!P->error) P->error = fmt_err(P, &P->tok, "expected second loop variable name");
                        return n;
                    }
                    n->as.forin_stmt.name2 = cs_strndup2(P->tok.start, P->tok.len);
                    next(P);
                }
            }

            // Check for walrus operator and provide helpful error
            if (P->tok.type == TK_WALRUS) {
                if (!P->error) P->error = fmt_err(P, &P->tok, "walrus operator not allowed in for-in loop (use C-style for loop with parentheses instead)");
                return n;
            }

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
        ast* n = parse_fn(P, 0);
        maybe_semi(P);
        return n;
    }

    if (accept(P, TK_ASYNC)) {
        if (!accept(P, TK_FN)) {
            if (!P->error) P->error = fmt_err(P, &P->tok, "expected 'fn' after async");
            return node(P, N_LIT_NIL);
        }
        ast* n = parse_fn(P, 1);
        maybe_semi(P);
        return n;
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

    if (accept(P, TK_YIELD)) {
        ast* n = node(P, N_YIELD);
        P->saw_yield = 1;
        if (P->tok.type != TK_SEMI && P->tok.type != TK_RBRACE) n->as.yield_stmt.value = parse_expr(P);
        else n->as.yield_stmt.value = NULL;
        maybe_semi(P);
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
    if (P->tok.type == TK_IDENT || P->tok.type == TK_SELF) {
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
                ast_free(node->as.switch_stmt.case_patterns[i]);
                ast_free(node->as.switch_stmt.case_blocks[i]);
            }
            free(node->as.switch_stmt.case_exprs);
            free(node->as.switch_stmt.case_patterns);
            free(node->as.switch_stmt.case_blocks);
            free(node->as.switch_stmt.case_kinds);
            break;
        case N_MATCH:
            ast_free(node->as.match_expr.expr);
            for (size_t i = 0; i < node->as.match_expr.case_count; i++) {
                ast_free(node->as.match_expr.case_patterns[i]);
                ast_free(node->as.match_expr.case_guards[i]);
                ast_free(node->as.match_expr.case_values[i]);
            }
            free(node->as.match_expr.case_patterns);
            free(node->as.match_expr.case_guards);
            free(node->as.match_expr.case_values);
            ast_free(node->as.match_expr.default_expr);
            break;
        case N_DEFER:
            ast_free(node->as.defer_stmt.stmt);
            break;
        case N_FORIN:
            free(node->as.forin_stmt.name);
            free(node->as.forin_stmt.name2);
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
        case N_CLASS:
            free(node->as.class_stmt.name);
            free(node->as.class_stmt.parent);
            for (size_t i = 0; i < node->as.class_stmt.method_count; i++) {
                ast_free(node->as.class_stmt.methods[i]);
            }
            free(node->as.class_stmt.methods);
            break;
        case N_STRUCT:
            free(node->as.struct_stmt.name);
            for (size_t i = 0; i < node->as.struct_stmt.field_count; i++) {
                free(node->as.struct_stmt.field_names[i]);
                ast_free(node->as.struct_stmt.field_defaults[i]);
            }
            free(node->as.struct_stmt.field_names);
            free(node->as.struct_stmt.field_defaults);
            break;
        case N_ENUM:
            free(node->as.enum_stmt.name);
            for (size_t i = 0; i < node->as.enum_stmt.count; i++) {
                free(node->as.enum_stmt.names[i]);
                ast_free(node->as.enum_stmt.values[i]);
            }
            free(node->as.enum_stmt.names);
            free(node->as.enum_stmt.values);
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
        case N_YIELD:
            ast_free(node->as.yield_stmt.value);
            break;
        case N_EXPR_STMT:
            ast_free(node->as.expr_stmt.expr);
            break;
        case N_FNDEF:
            free(node->as.fndef.name);
            for (size_t i = 0; i < node->as.fndef.param_count; i++) {
                free(node->as.fndef.params[i]);
                if (node->as.fndef.defaults) ast_free(node->as.fndef.defaults[i]);
            }
            free(node->as.fndef.params);
            free(node->as.fndef.defaults);
            free(node->as.fndef.rest_param);
            ast_free(node->as.fndef.body);
            break;
        case N_BINOP:
            ast_free(node->as.binop.left);
            ast_free(node->as.binop.right);
            break;
        case N_UNOP:
            ast_free(node->as.unop.expr);
            break;
        case N_AWAIT:
            ast_free(node->as.await_expr.expr);
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
        case N_PIPE:
            ast_free(node->as.pipe.left);
            ast_free(node->as.pipe.right);
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
                if (node->as.funclit.defaults) ast_free(node->as.funclit.defaults[i]);
            }
            free(node->as.funclit.params);
            free(node->as.funclit.defaults);
            free(node->as.funclit.rest_param);
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
        case N_TUPLELIT:
            for (size_t i = 0; i < node->as.tuplelit.count; i++) {
                free(node->as.tuplelit.field_names[i]);
                ast_free(node->as.tuplelit.field_values[i]);
            }
            free(node->as.tuplelit.field_names);
            free(node->as.tuplelit.field_values);
            break;
        case N_LISTCOMP:
            ast_free(node->as.listcomp.expr);
            for (size_t i = 0; i < node->as.listcomp.iter_count; i++) {
                free(node->as.listcomp.vars[i]);
                if (node->as.listcomp.vars2[i]) free(node->as.listcomp.vars2[i]);
                ast_free(node->as.listcomp.iterables[i]);
            }
            free(node->as.listcomp.vars);
            free(node->as.listcomp.vars2);
            free(node->as.listcomp.iterables);
            ast_free(node->as.listcomp.filter);
            break;
        case N_MAPCOMP:
            ast_free(node->as.mapcomp.key_expr);
            ast_free(node->as.mapcomp.val_expr);
            for (size_t i = 0; i < node->as.mapcomp.iter_count; i++) {
                free(node->as.mapcomp.key_vars[i]);
                if (node->as.mapcomp.val_vars[i]) free(node->as.mapcomp.val_vars[i]);
                ast_free(node->as.mapcomp.iterables[i]);
            }
            free(node->as.mapcomp.key_vars);
            free(node->as.mapcomp.val_vars);
            free(node->as.mapcomp.iterables);
            ast_free(node->as.mapcomp.filter);
            break;
        case N_SETCOMP:
            ast_free(node->as.setcomp.expr);
            for (size_t i = 0; i < node->as.setcomp.iter_count; i++) {
                free(node->as.setcomp.vars[i]);
                if (node->as.setcomp.vars2[i]) free(node->as.setcomp.vars2[i]);
                ast_free(node->as.setcomp.iterables[i]);
            }
            free(node->as.setcomp.vars);
            free(node->as.setcomp.vars2);
            free(node->as.setcomp.iterables);
            ast_free(node->as.setcomp.filter);
            break;
        case N_PATTERN_LIST:
            for (size_t i = 0; i < node->as.list_pattern.count; i++) {
                free(node->as.list_pattern.names[i]);
            }
            free(node->as.list_pattern.names);
            free(node->as.list_pattern.rest_name);
            break;
        case N_PATTERN_MAP:
            for (size_t i = 0; i < node->as.map_pattern.count; i++) {
                free(node->as.map_pattern.keys[i]);
                free(node->as.map_pattern.names[i]);
            }
            free(node->as.map_pattern.keys);
            free(node->as.map_pattern.names);
            free(node->as.map_pattern.rest_name);
            break;
        case N_PATTERN_TYPE:
            free(node->as.type_pattern.type_name);
            ast_free(node->as.type_pattern.inner);
            break;
        case N_SPREAD:
            ast_free(node->as.spread.expr);
            break;
        case N_IDENT:
            free(node->as.ident.name);
            break;
        case N_PLACEHOLDER:
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
        case N_PATTERN_WILDCARD:
        case N_BREAK:
        case N_CONTINUE:
            // No dynamic allocations
            break;
        
        default:
            // Handle any other cases
            break;
    }
    
    free(node);
}
