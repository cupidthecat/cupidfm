#ifndef CS_VM_H
#define CS_VM_H

#include "cupidscript.h"
#include "cs_parser.h"
#include "cs_value.h"
#include "cs_event_loop.h"

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
    ast** defaults;         // default expressions (NULL if no default)
    size_t param_count;
    char* rest_param;        // optional rest parameter name
    ast* body;
    cs_env* closure;
    int is_async;
    int is_generator;
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

typedef struct cs_task {
    struct cs_task* next;
    struct cs_func* fn;
    cs_env* bound_env;
    int argc;
    cs_value* argv;
    cs_promise_obj* promise;
    const char* source;
    int line;
    int col;
} cs_task;

typedef struct cs_timer {
    struct cs_timer* next;
    uint64_t due_ms;
    cs_promise_obj* promise;
} cs_timer;

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

    // Generator execution state (only active during generator calls)
    struct cs_list_obj* yield_list;
    int yield_active;
    int yield_used;

    // Async scheduler
    cs_task* task_head;
    cs_task* task_tail;
    cs_timer* timers;

    // Network I/O pending operations
    cs_pending_io* pending_io;
    int pending_io_count;
    uint64_t net_default_timeout_ms;  // default 30000 (30 seconds)

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

    // Profiling counters
    uint64_t prof_string_ops;
    uint64_t prof_string_ms;
    uint64_t prof_pipe_ops;
    uint64_t prof_pipe_ms;
    uint64_t prof_match_ops;
    uint64_t prof_match_ms;
    uint64_t prof_optchain_ops;
    uint64_t prof_optchain_ms;
};

void cs_register_stdlib(cs_vm* vm);

// Async helpers (internal stdlib support)
cs_value cs_promise_new(cs_vm* vm);
int cs_promise_resolve(cs_vm* vm, cs_value promise, cs_value value);
int cs_promise_reject(cs_vm* vm, cs_value promise, cs_value value);
int cs_promise_is_pending(cs_value promise);
void cs_schedule_timer(cs_vm* vm, cs_value promise, uint64_t due_ms);
cs_value cs_wait_promise(cs_vm* vm, cs_value promise, int* ok);

#endif
