#include "cs_lexer.h"
#include <ctype.h>
#include <string.h>

static char peek(lexer* L) { return L->src[L->pos]; }
static char peek2(lexer* L) { return L->src[L->pos + 1]; }

static char advance(lexer* L) {
    char c = L->src[L->pos++];
    if (c == '\n') { L->line++; L->col = 1; }
    else L->col++;
    return c;
}

static void skip_ws_and_comments(lexer* L) {
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
            advance(L); advance(L);
            while (peek(L)) {
                if (peek(L) == '*' && peek2(L) == '/') { advance(L); advance(L); break; }
                advance(L);
            }
            continue;
        }
        break;
    }
}

static token make_tok(lexer* L, token_type t, const char* start, size_t len, long long iv, int line, int col) {
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
    if (n == 2 && memcmp(s, "fn", 2) == 0) return TK_FN;
    if (n == 2 && memcmp(s, "if", 2) == 0) return TK_IF;
    if (n == 4 && memcmp(s, "else", 4) == 0) return TK_ELSE;
    if (n == 5 && memcmp(s, "while", 5) == 0) return TK_WHILE;
    if (n == 6 && memcmp(s, "return", 6) == 0) return TK_RETURN;
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
}

token lex_next(lexer* L) {
    skip_ws_and_comments(L);
    const char* start = &L->src[L->pos];
    int line = L->line;
    int col  = L->col;

    char c = peek(L);
    if (!c) return make_tok(L, TK_EOF, start, 0, 0, line, col);

    // numbers
    if (isdigit((unsigned char)c)) {
        long long v = 0;
        while (isdigit((unsigned char)peek(L))) {
            v = (v * 10) + (advance(L) - '0');
        }
        size_t len = (size_t)(&L->src[L->pos] - start);
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

    // strings
    if (c == '"') {
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
        case '{': t = TK_LBRACE; break;
        case '}': t = TK_RBRACE; break;
        case ',': t = TK_COMMA; break;
        case ';': t = TK_SEMI; break;
        case '.': t = TK_DOT; break;

        case '+': t = TK_PLUS; break;
        case '-': t = TK_MINUS; break;
        case '*': t = TK_STAR; break;
        case '/': t = TK_SLASH; break;
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
