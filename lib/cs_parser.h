#ifndef CS_PARSER_H
#define CS_PARSER_H

#include "cs_lexer.h"
#include <stddef.h>

typedef struct ast ast;

typedef enum {
    N_BLOCK = 1,
    N_LET,
    N_ASSIGN,
    N_SETINDEX,
    N_IF,
    N_WHILE,
    N_RETURN,
    N_EXPR_STMT,
    N_FNDEF,

    N_BINOP,
    N_UNOP,
    N_CALL,
    N_INDEX,
    N_GETFIELD,
    N_FUNCLIT,
    N_IDENT,
    N_LIT_INT,
    N_LIT_STR,
    N_LIT_BOOL,
    N_LIT_NIL
} node_type;

struct ast {
    node_type type;
    const char* source_name;
    int line, col;

    union {
        struct { ast** items; size_t count; } block;

        struct { char* name; ast* init; } let_stmt;
        struct { char* name; ast* value; } assign_stmt;
        struct { ast* target; ast* index; ast* value; } setindex_stmt;

        struct { ast* cond; ast* then_b; ast* else_b; } if_stmt;
        struct { ast* cond; ast* body; } while_stmt;

        struct { ast* value; } ret_stmt;
        struct { ast* expr; } expr_stmt;

        struct {
            char* name;
            char** params;
            size_t param_count;
            ast* body;
        } fndef;

        struct { int op; ast* left; ast* right; } binop;
        struct { int op; ast* expr; } unop;

        struct { ast* callee; ast** args; size_t argc; } call;
        struct { ast* target; ast* index; } index;
        struct { ast* target; char* field; } getfield;
        struct { char** params; size_t param_count; ast* body; } funclit;

        struct { char* name; } ident;

        struct { long long v; } lit_int;
        struct { char* s; } lit_str;
        struct { int v; } lit_bool;
    } as;
};

typedef struct {
    lexer L;
    token tok;
    const char* source_name;
    char* error; // malloc'd error message or NULL
} parser;

void parser_init(parser* P, const char* src, const char* source_name);
ast* parse_program(parser* P);
void parse_free_error(parser* P);

#endif
