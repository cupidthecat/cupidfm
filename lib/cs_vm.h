#ifndef CS_VM_H
#define CS_VM_H

#include "cupidscript.h"
#include "cs_parser.h"
#include "cs_value.h"

typedef struct cs_env {
    int ref;
    struct cs_env* parent;
    char** keys;
    cs_value* vals;
    size_t count;
    size_t cap;
} cs_env;

struct cs_func {
    int ref;
    const char* name;
    const char* def_source;
    int def_line;
    int def_col;
    char** params;
    size_t param_count;
    ast* body;
    cs_env* closure;
};

typedef struct cs_frame {
    const char* func;
    const char* source;
    int line;
    int col;
} cs_frame;

struct cs_vm {
    cs_env* globals;
    char* last_error;

    cs_frame* frames;
    size_t frame_count;
    size_t frame_cap;

    char** sources;
    size_t source_count;
    size_t source_cap;

    char** dir_stack;
    size_t dir_count;
    size_t dir_cap;

    char** require_list;
    size_t require_count;
    size_t require_cap;
};

void cs_register_stdlib(cs_vm* vm);

#endif
