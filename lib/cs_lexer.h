#ifndef CS_LEXER_H
#define CS_LEXER_H

#include <stddef.h>

typedef enum {
    TK_EOF = 0,
    TK_ERR,

    TK_IDENT,
    TK_INT,
    TK_FLOAT,
    TK_STR,
    TK_RAW_STR,
    TK_STR_PART,    // raw string chunk inside interpolated string
    TK_STR_END,     // end of interpolated string
    TK_INTERP_START, // ${
    TK_INTERP_END,   // }

    // keywords
    TK_LET,
    TK_CONST,
    TK_MATCH,
    TK_DEFER,
    TK_IMPORT,
    TK_FROM,
    TK_AS,
    TK_FN,
    TK_IF,
    TK_ELSE,
    TK_WHILE,
    TK_SWITCH,
    TK_CASE,
    TK_DEFAULT,
    TK_RETURN,
    TK_BREAK,
    TK_CONTINUE,
    TK_FOR,
    TK_IN,
    TK_THROW,
    TK_TRY,
    TK_CATCH,
    TK_FINALLY,
    TK_EXPORT,
    TK_CLASS,
    TK_STRUCT,
    TK_ENUM,
    TK_ASYNC,
    TK_AWAIT,
    TK_YIELD,
    TK_SELF,
    TK_SUPER,
    TK_TRUE,
    TK_FALSE,
    TK_NIL,

    // punctuation
    TK_LPAREN, TK_RPAREN,
    TK_LBRACKET, TK_RBRACKET,
    TK_LBRACE, TK_RBRACE,
    TK_COMMA,
    TK_SEMI,
    TK_DOT,
    TK_COLON,
    TK_QMARK,
    TK_QQ,          // ??
    TK_QDOT,        // ?.
    TK_DOTDOTDOT,   // ...

    // operators
    TK_ASSIGN,     // =
    TK_PLUSEQ, TK_MINUSEQ, TK_STAREQ, TK_SLASHEQ,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_BANG,
    TK_EQ, TK_NE,
    TK_LT, TK_LE,
    TK_GT, TK_GE,
    TK_ANDAND, TK_OROR,
    TK_ARROW,      // =>
    TK_PIPE,       // |>
    TK_PLACEHOLDER, // _
    TK_RANGE,      // ..
    TK_RANGE_INC   // ..=
} token_type;

typedef struct {
    token_type type;
    const char* start;
    size_t len;
    long long int_val;
    double float_val;
    int line;
    int col;
} token;

typedef struct {
    const char* src;
    size_t pos;
    int line;
    int col;
    token current;
    int mode;          // 0=normal, 1=string, 2=interp, 3=string_end, 4=interp_start
    int interp_depth;  // brace depth inside ${...}
} lexer;

void  lex_init(lexer* L, const char* src);
token lex_next(lexer* L);

#endif
