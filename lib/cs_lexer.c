#include "cs_lexer.h"
#include <ctype.h>
#include <string.h>

static char peek(lexer* L) { return L->src[L->pos]; }
static char peek2(lexer* L) { return L->src[L->pos + 1]; }

static token make_tok(lexer* L, token_type t, const char* start, size_t len, long long iv, int line, int col);

static char advance(lexer* L) {
    char c = L->src[L->pos++];
    if (c == '\n') { L->line++; L->col = 1; }
    else L->col++;
    return c;
}

static int string_has_interpolation(const lexer* L) {
    if (!L || !L->src) return 0;
    size_t i = L->pos + 1; // after opening quote
    while (L->src[i]) {
        char c = L->src[i];
        if (c == '\\' && L->src[i + 1]) { i += 2; continue; }
        if (c == '"') return 0; // end of string, no interpolation found
        if (c == '$' && L->src[i + 1] == '{') return 1;
        i++;
    }
    return 0;
}

static token lex_string_part(lexer* L) {
    const char* start = &L->src[L->pos];
    int line = L->line;
    int col  = L->col;

    while (peek(L)) {
        char c = peek(L);
        if (c == '"') {
            L->mode = 3; // string_end
            break;
        }
        if (c == '$' && peek2(L) == '{') {
            L->mode = 4; // interp_start
            break;
        }
        if (c == '\\' && peek2(L)) { advance(L); advance(L); continue; }
        advance(L);
    }

    if (!peek(L) && L->mode != 3 && L->mode != 4) {
        return make_tok(L, TK_ERR, start, (size_t)(&L->src[L->pos] - start), 0, line, col);
    }

    size_t len = (size_t)(&L->src[L->pos] - start);
    return make_tok(L, TK_STR_PART, start, len, 0, line, col);

    // unreachable, but keeps compiler happy
    return make_tok(L, TK_ERR, start, 0, 0, line, col);
}

static int skip_ws_and_comments(lexer* L, const char** err_start, size_t* err_len, int* err_line, int* err_col) {
    for (;;) {
        char c = peek(L);
        // whitespace
        while (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(L);
            c = peek(L);
        }
        // // comment
        if (c == '/' && peek2(L) == '/') {
            while (peek(L) && peek(L) != '\n') advance(L);
            continue;
        }
        // /* comment */
        if (c == '/' && peek2(L) == '*') {
            const char* start = &L->src[L->pos];
            int line = L->line;
            int col = L->col;
            advance(L); advance(L);
            int closed = 0;
            while (peek(L)) {
                if (peek(L) == '*' && peek2(L) == '/') { advance(L); advance(L); closed = 1; break; }
                advance(L);
            }
            if (!closed) {
                if (err_start) *err_start = start;
                if (err_len) *err_len = (size_t)(&L->src[L->pos] - start);
                if (err_line) *err_line = line;
                if (err_col) *err_col = col;
                return 0;
            }
            continue;
        }
        break;
    }

    (void)err_start; (void)err_len; (void)err_line; (void)err_col;
    return 1;
}

static token make_tok(lexer* L, token_type t, const char* start, size_t len, long long iv, int line, int col) {
    (void)L;
    token tok;
    tok.type = t;
    tok.start = start;
    tok.len = len;
    tok.int_val = iv;
    tok.line = line;
    tok.col = col;
    return tok;
}

static int is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_ident_char(char c)  { return isalnum((unsigned char)c) || c == '_'; }

static token_type keyword_type(const char* s, size_t n) {
    if (n == 3 && memcmp(s, "let", 3) == 0) return TK_LET;
    if (n == 5 && memcmp(s, "const", 5) == 0) return TK_CONST;
    if (n == 5 && memcmp(s, "match", 5) == 0) return TK_MATCH;
    if (n == 5 && memcmp(s, "defer", 5) == 0) return TK_DEFER;
    if (n == 6 && memcmp(s, "import", 6) == 0) return TK_IMPORT;
    if (n == 4 && memcmp(s, "from", 4) == 0) return TK_FROM;
    if (n == 2 && memcmp(s, "as", 2) == 0) return TK_AS;
    if (n == 2 && memcmp(s, "fn", 2) == 0) return TK_FN;
    if (n == 2 && memcmp(s, "if", 2) == 0) return TK_IF;
    if (n == 4 && memcmp(s, "else", 4) == 0) return TK_ELSE;
    if (n == 5 && memcmp(s, "while", 5) == 0) return TK_WHILE;
    if (n == 6 && memcmp(s, "switch", 6) == 0) return TK_SWITCH;
    if (n == 4 && memcmp(s, "case", 4) == 0) return TK_CASE;
    if (n == 7 && memcmp(s, "default", 7) == 0) return TK_DEFAULT;
    if (n == 3 && memcmp(s, "for", 3) == 0) return TK_FOR;
    if (n == 2 && memcmp(s, "in", 2) == 0) return TK_IN;
    if (n == 6 && memcmp(s, "return", 6) == 0) return TK_RETURN;
    if (n == 5 && memcmp(s, "break", 5) == 0) return TK_BREAK;
    if (n == 8 && memcmp(s, "continue", 8) == 0) return TK_CONTINUE;
    if (n == 5 && memcmp(s, "throw", 5) == 0) return TK_THROW;
    if (n == 3 && memcmp(s, "try", 3) == 0) return TK_TRY;
    if (n == 5 && memcmp(s, "catch", 5) == 0) return TK_CATCH;
    if (n == 7 && memcmp(s, "finally", 7) == 0) return TK_FINALLY;
    if (n == 6 && memcmp(s, "export", 6) == 0) return TK_EXPORT;
    if (n == 4 && memcmp(s, "true", 4) == 0) return TK_TRUE;
    if (n == 5 && memcmp(s, "false", 5) == 0) return TK_FALSE;
    if (n == 3 && memcmp(s, "nil", 3) == 0) return TK_NIL;
    return TK_IDENT;
}

void lex_init(lexer* L, const char* src) {
    L->src = src ? src : "";
    L->pos = 0;
    L->line = 1;
    L->col = 1;
    L->current.type = TK_EOF;
    L->mode = 0;
    L->interp_depth = 0;
}

token lex_next(lexer* L) {
    if (L->mode == 3) { // string_end
        const char* start = &L->src[L->pos];
        int line = L->line;
        int col  = L->col;
        if (peek(L) == '"') advance(L);
        L->mode = 0;
        return make_tok(L, TK_STR_END, start, 0, 0, line, col);
    }

    if (L->mode == 4) { // interp_start
        const char* start = &L->src[L->pos];
        int line = L->line;
        int col  = L->col;
        if (peek(L) == '$' && peek2(L) == '{') {
            advance(L); advance(L);
            L->mode = 2;
            L->interp_depth = 0;
            return make_tok(L, TK_INTERP_START, start, 2, 0, line, col);
        }
        return make_tok(L, TK_ERR, start, 0, 0, line, col);
    }

    if (L->mode == 1) { // string
        return lex_string_part(L);
    }

    const char* err_start = NULL;
    size_t err_len = 0;
    int err_line = 0;
    int err_col = 0;
    if (!skip_ws_and_comments(L, &err_start, &err_len, &err_line, &err_col)) {
        return make_tok(L, TK_ERR, err_start ? err_start : &L->src[L->pos], err_len, 0, err_line, err_col);
    }
    const char* start = &L->src[L->pos];
    int line = L->line;
    int col  = L->col;

    char c = peek(L);
    if (!c) return make_tok(L, TK_EOF, start, 0, 0, line, col);

    if (L->mode == 2) { // interp
        if (c == '}' && L->interp_depth == 0) {
            advance(L);
            L->mode = 1; // back to string
            return make_tok(L, TK_INTERP_END, start, 1, 0, line, col);
        }
    }

    // numbers (decimal with underscores, hex 0xFF, or floats 3.14)
    if (isdigit((unsigned char)c)) {
        long long v = 0;
        double fv = 0.0;
        int is_hex = 0;
        int is_float = 0;

        if (c == '0' && (peek2(L) == 'x' || peek2(L) == 'X')) {
            is_hex = 1;
            advance(L); // 0
            advance(L); // x
            while (1) {
                char h = peek(L);
                if (h == '_') { advance(L); continue; }
                if (isdigit((unsigned char)h)) { v = (v * 16) + (advance(L) - '0'); continue; }
                if (h >= 'a' && h <= 'f') { v = (v * 16) + (advance(L) - 'a' + 10); continue; }
                if (h >= 'A' && h <= 'F') { v = (v * 16) + (advance(L) - 'A' + 10); continue; }
                break;
            }
        } else {
            // Parse integer part
            while (1) {
                char d = peek(L);
                if (d == '_') { advance(L); continue; }
                if (!isdigit((unsigned char)d)) break;
                v = (v * 10) + (advance(L) - '0');
            }
            
            // Check for decimal point
            if (peek(L) == '.' && isdigit((unsigned char)peek2(L))) {
                is_float = 1;
                fv = (double)v;
                advance(L); // consume '.'
                
                double frac = 0.0;
                double divisor = 10.0;
                while (1) {
                    char d = peek(L);
                    if (d == '_') { advance(L); continue; }
                    if (!isdigit((unsigned char)d)) break;
                    frac += (advance(L) - '0') / divisor;
                    divisor *= 10.0;
                }
                fv += frac;
            }
            
            // Check for exponent (works with or without decimal point)
            if (peek(L) == 'e' || peek(L) == 'E') {
                if (!is_float) {
                    is_float = 1;
                    fv = (double)v;
                }
                advance(L);
                int exp_sign = 1;
                if (peek(L) == '+') advance(L);
                else if (peek(L) == '-') { exp_sign = -1; advance(L); }
                
                int exp = 0;
                while (isdigit((unsigned char)peek(L))) {
                    exp = exp * 10 + (advance(L) - '0');
                }
                
                double multiplier = 1.0;
                for (int i = 0; i < exp; i++) {
                    multiplier *= 10.0;
                }
                if (exp_sign < 0) fv /= multiplier;
                else fv *= multiplier;
            }
        }

        (void)is_hex;
        size_t len = (size_t)(&L->src[L->pos] - start);
        if (is_float) {
            token tok = make_tok(L, TK_FLOAT, start, len, 0, line, col);
            tok.float_val = fv;
            return tok;
        }
        return make_tok(L, TK_INT, start, len, v, line, col);
    }

    // identifiers / keywords
    if (is_ident_start(c)) {
        advance(L);
        while (is_ident_char(peek(L))) advance(L);
        size_t len = (size_t)(&L->src[L->pos] - start);
        token_type kt = keyword_type(start, len);
        return make_tok(L, kt, start, len, 0, line, col);
    }

    // strings (with optional interpolation)
    if (c == '"') {
        if (string_has_interpolation(L)) {
            advance(L); // opening
            L->mode = 1; // string
            return lex_string_part(L);
        }
        advance(L); // opening
        while (peek(L) && peek(L) != '"') {
            if (peek(L) == '\\' && peek2(L)) { advance(L); advance(L); continue; }
            advance(L);
        }
        if (peek(L) != '"') {
            return make_tok(L, TK_ERR, start, (size_t)(&L->src[L->pos] - start), 0, line, col);
        }
        advance(L); // closing
        size_t len = (size_t)(&L->src[L->pos] - start);
        return make_tok(L, TK_STR, start, len, 0, line, col);
    }

    // operators / punctuation
    advance(L);
    token_type t = TK_ERR;
    switch (c) {
        case '(': t = TK_LPAREN; break;
        case ')': t = TK_RPAREN; break;
        case '[': t = TK_LBRACKET; break;
        case ']': t = TK_RBRACKET; break;
        case '{':
            t = TK_LBRACE;
            if (L->mode == 2) L->interp_depth++;
            break;
        case '}':
            t = TK_RBRACE;
            if (L->mode == 2 && L->interp_depth > 0) L->interp_depth--;
            break;
        case ',': t = TK_COMMA; break;
        case ';': t = TK_SEMI; break;
        case '.':
            // Check for .. or ..=
            if (peek(L) == '.') {
                advance(L);
                if (peek(L) == '=') {
                    advance(L);
                    t = TK_RANGE_INC;
                } else {
                    t = TK_RANGE;
                }
            } else {
                t = TK_DOT;
            }
            break;
        case ':': t = TK_COLON; break;
        case '?':
            if (peek(L) == '?') { advance(L); t = TK_QQ; }
            else if (peek(L) == '.') { advance(L); t = TK_QDOT; }
            else t = TK_QMARK;
            break;

        case '+':
            if (peek(L) == '=') { advance(L); t = TK_PLUSEQ; }
            else t = TK_PLUS;
            break;
        case '-':
            if (peek(L) == '=') { advance(L); t = TK_MINUSEQ; }
            else t = TK_MINUS;
            break;
        case '*':
            if (peek(L) == '=') { advance(L); t = TK_STAREQ; }
            else t = TK_STAR;
            break;
        case '/':
            if (peek(L) == '=') { advance(L); t = TK_SLASHEQ; }
            else t = TK_SLASH;
            break;
        case '%': t = TK_PERCENT; break;

        case '!':
            if (peek(L) == '=') { advance(L); t = TK_NE; }
            else t = TK_BANG;
            break;

        case '=':
            if (peek(L) == '=') { advance(L); t = TK_EQ; }
            else t = TK_ASSIGN;
            break;

        case '<':
            if (peek(L) == '=') { advance(L); t = TK_LE; }
            else t = TK_LT;
            break;

        case '>':
            if (peek(L) == '=') { advance(L); t = TK_GE; }
            else t = TK_GT;
            break;

        case '&':
            if (peek(L) == '&') { advance(L); t = TK_ANDAND; }
            break;

        case '|':
            if (peek(L) == '|') { advance(L); t = TK_OROR; }
            break;
        default:
            t = TK_ERR;
            break;
    }

    return make_tok(L, t, start, (size_t)(&L->src[L->pos] - start), 0, line, col);
}
