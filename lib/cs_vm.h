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
    unsigned char* is_const;
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

typedef struct cs_module {
    char* path;
    cs_value exports;
} cs_module;

typedef enum {
    CS_TRACK_LIST = 1,
    CS_TRACK_MAP = 2
} cs_track_type;

typedef struct cs_track_node {
    cs_track_type type;
    void* ptr;
    struct cs_track_node* next;
} cs_track_node;

struct cs_vm {
    cs_env* globals;
    char* last_error;

    int pending_throw;
    cs_value pending_thrown;

    cs_frame* frames;
    size_t frame_count;
    size_t frame_cap;

    char** sources;
    size_t source_count;
    size_t source_cap;

    char** dir_stack;
    size_t dir_count;
    size_t dir_cap;

    cs_module* modules;
    size_t module_count;
    size_t module_cap;

    cs_track_node* tracked;
    size_t tracked_count;

    // Retained ASTs (keep parsed programs alive for closures/functions)
    ast** asts;
    size_t ast_count;
    size_t ast_cap;

    // GC auto-collect policy
    size_t gc_threshold;            // collect when tracked_count >= threshold; 0 = disabled
    size_t gc_allocations;          // total allocations since last GC
    size_t gc_alloc_trigger;        // collect every N allocations; 0 = disabled
    size_t gc_collections;          // total collections performed
    size_t gc_objects_collected;    // total objects collected

    // Safety controls (prevents runaway scripts)
    uint64_t instruction_count;
    uint64_t instruction_limit;     // 0 = unlimited
    uint64_t exec_start_ms;         // execution start time
    uint64_t exec_timeout_ms;       // 0 = unlimited
    int interrupt_requested;        // set by host to abort execution
};

void cs_register_stdlib(cs_vm* vm);

#endif
