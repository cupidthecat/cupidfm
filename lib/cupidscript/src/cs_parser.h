#ifndef CS_PARSER_H
#define CS_PARSER_H

#include "cs_lexer.h"
#include <stddef.h>

typedef struct ast ast;

typedef enum {
    N_ERR = 0,
    // Literals
    N_LIT_INT, N_LIT_FLOAT, N_LIT_STR, N_LIT_BOOL, N_LIT_NIL,
    N_STR_INTERP,
    // Identifiers and patterns
    N_IDENT, N_PLACEHOLDER,
    N_PATTERN_LIST, N_PATTERN_MAP, N_PATTERN_TYPE, N_PATTERN_WILDCARD,
    // Compound literals
    N_LISTLIT, N_MAPLIT, N_TUPLELIT, N_SETLIT,
    N_LISTCOMP, N_MAPCOMP, N_SETCOMP,
    // Operators
    N_BINOP, N_UNOP, N_TERNARY, N_PIPE,
    N_INDEX, N_GETFIELD, N_OPTGETFIELD, N_CALL,
    N_RANGE, N_SPREAD,
    N_WALRUS,
    N_AWAIT,
    // Statements
    N_BLOCK, N_EXPR_STMT,
    N_LET, N_ASSIGN, N_SETINDEX,
    N_IF, N_WHILE, N_FORIN, N_FOR_C_STYLE,
    N_RETURN, N_BREAK, N_CONTINUE, N_YIELD,
    N_FNDEF, N_FUNCLIT,
    N_CLASS, N_STRUCT, N_ENUM,
    N_MATCH, N_SWITCH,
    N_TRY, N_THROW, N_DEFER,
    N_IMPORT, N_EXPORT, N_EXPORT_LIST
} node_type;

struct ast {
    node_type type;
    int line, col;
    const char* source_name;
    union {
        struct { char* name; } ident;
        struct { long long v; } lit_int;
        struct { double v; } lit_float;
        struct { char* s; } lit_str;
        struct { int v; } lit_bool;
        struct { ast** parts; size_t count; } str_interp;

        struct { ast** items; size_t count; } block;

        struct { char* name; ast* init; ast* pattern; int is_const; } let_stmt;
        struct { char* name; ast* value; } assign_stmt;
        struct { ast* target; ast* index; ast* value; int op; } setindex_stmt;
        struct { char* name; ast* value; } walrus;
        struct {
            ast* expr;
            ast** case_exprs;
            ast** case_patterns;
            ast** case_blocks;
            unsigned char* case_kinds;
            size_t case_count;
        } switch_stmt;
        struct {
            ast* expr;
            ast** case_patterns;
            ast** case_guards;
            ast** case_values;
            size_t case_count;
            ast* default_expr;
        } match_expr;
        struct { ast* stmt; } defer_stmt;
        struct {
            ast* path;
            char* default_name;
            char** import_names;
            char** local_names;
            size_t count;
        } import_stmt;
        struct {
            char** local_names;
            char** export_names;
            size_t count;
        } export_list;
        struct { char* name; char* name2; ast* iterable; ast* body; } forin_stmt;
        struct { ast* init; ast* cond; ast* incr; ast* body; } for_c_style_stmt;
        struct { ast* value; } throw_stmt;
        struct { ast* try_b; char* catch_name; ast* catch_b; ast* finally_b; } try_stmt;
        struct { char* name; ast* value; } export_stmt;
        struct {
            char* name;
            char* parent;
            ast** methods;
            size_t method_count;
        } class_stmt;
        struct {
            char* name;
            char** field_names;
            ast** field_defaults;
            size_t field_count;
        } struct_stmt;
        struct {
            char* name;
            char** names;
            ast** values;
            size_t count;
        } enum_stmt;
        struct { ast* value; } yield_stmt;
        struct { ast* cond; ast* then_b; ast* else_b; } if_stmt;
        struct { ast* cond; ast* body; } while_stmt;
        struct { ast* value; } ret_stmt;
        struct { ast* expr; } expr_stmt;
        struct {
            char* name;
            char** params;
            ast** defaults;
            size_t param_count;
            char* rest_param;
            ast* body;
            int is_async;
            int is_generator;
        } fndef;
        struct {
            char** params;
            ast** defaults;
            size_t param_count;
            char* rest_param;
            ast* body;
            int is_async;
            int is_generator;
        } funclit;
        struct { int op; ast* left; ast* right; } binop;
        struct { int op; ast* expr; } unop;
        struct { ast* expr; } await_expr;
        struct { ast* left; ast* right; int inclusive; } range;
        struct { ast* cond; ast* then_e; ast* else_e; } ternary;
        struct { ast* left; ast* right; } pipe;
        struct { ast* callee; ast** args; size_t argc; } call;
        struct { ast* target; ast* index; } index;
        struct { ast* target; char* field; } getfield;
        struct { ast** items; size_t count; } listlit;
        struct { ast** keys; ast** vals; size_t count; } maplit;
        struct { ast** items; size_t count; } setlit;
        struct { 
            char** field_names;
            ast** field_values;
            size_t count;
        } tuplelit;
        struct {
            ast* expr;
            char** vars;
            char** vars2;
            ast** iterables;
            size_t iter_count;
            ast* filter;
        } listcomp;
        struct {
            ast* key_expr;
            ast* val_expr;
            char** key_vars;
            char** val_vars;
            ast** iterables;
            size_t iter_count;
            ast* filter;
        } mapcomp;
        struct {
            ast* expr;
            char** vars;
            char** vars2;
            ast** iterables;
            size_t iter_count;
            ast* filter;
        } setcomp;
        struct { char** names; size_t count; char* rest_name; } list_pattern;
        struct { char** keys; char** names; size_t count; char* rest_name; } map_pattern;
        struct { char* type_name; ast* inner; } type_pattern;
        struct { ast* expr; } spread;
    } as;
};

typedef struct {
    lexer L;
    token tok;
    const char* source_name;
    char* error;
    int saw_yield;
} parser;

void parser_init(parser* P, const char* src, const char* source_name);
ast* parse_program(parser* P);
void parse_free_error(parser* P);
void ast_free(ast* node);

#endif
