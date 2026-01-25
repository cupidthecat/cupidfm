#ifndef CUPIDSCRIPT_H
#define CUPIDSCRIPT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cs_vm cs_vm;

typedef enum {
    CS_T_NIL = 0,
    CS_T_BOOL,
    CS_T_INT,
    CS_T_FLOAT,
    CS_T_STR,
    CS_T_LIST,
    CS_T_MAP,
    CS_T_STRBUF,
    CS_T_RANGE,
    CS_T_FUNC,
    CS_T_NATIVE
} cs_type;

typedef struct {
    cs_type type;
    union {
        int      b;
        int64_t  i;
        double   f;
        void*    p;   // string*, func*, native*
    } as;
} cs_value;

typedef int (*cs_native_fn)(cs_vm* vm, void* userdata, int argc, const cs_value* argv, cs_value* out);

// VM lifecycle
cs_vm* cs_vm_new(void);
void   cs_vm_free(cs_vm* vm);

// Error handling helpers (exposed for CLI tooling and CupidFM integration)
// Returns the last error message as a C string. If there is no error, returns "".
const char* cs_vm_last_error(cs_vm* vm);
// Set a VM-local error message (allocates its own storage).
void        cs_error(cs_vm* vm, const char* msg);

// Running code
int cs_vm_run_file(cs_vm* vm, const char* path);
int cs_vm_run_string(cs_vm* vm, const char* code, const char* virtual_name);
// Module loader: executes a file once and returns its exports map.
// Path should generally be absolute or already resolved by the host.
int cs_vm_require_module(cs_vm* vm, const char* path, cs_value* exports_out);
// Cycle collection for refcounted containers (lists/maps). Returns number of objects collected.
size_t cs_vm_collect_cycles(cs_vm* vm);

// GC auto-collect configuration
void cs_vm_set_gc_threshold(cs_vm* vm, size_t threshold);     // collect when tracked >= threshold
void cs_vm_set_gc_alloc_trigger(cs_vm* vm, size_t interval); // collect every N allocations

// Calling functions defined in script
int cs_call(cs_vm* vm, const char* func_name, int argc, const cs_value* argv, cs_value* out);
int cs_call_value(cs_vm* vm, cs_value callee, int argc, const cs_value* argv, cs_value* out);

// Register native functions (for CupidFM to expose API)
void cs_register_native(cs_vm* vm, const char* name, cs_native_fn fn, void* userdata);
// Register global values (constants, etc.)
void cs_register_global(cs_vm* vm, const char* name, cs_value value);

// Error text (valid until next VM call that sets it)
const char* cs_last_error(const cs_vm* vm);

// Helpers for building values
cs_value cs_nil(void);
cs_value cs_bool(int v);
cs_value cs_int(int64_t v);
cs_value cs_float(double v);
cs_value cs_str(cs_vm* vm, const char* s); // makes a VM-owned string
cs_value cs_str_take(cs_vm* vm, char* owned, uint64_t len); // takes ownership of malloc'd string
cs_value cs_list(cs_vm* vm);
cs_value cs_map(cs_vm* vm);
cs_value cs_strbuf(cs_vm* vm);

// Value lifetime helpers (useful for hosts storing callbacks/values)
cs_value cs_value_copy(cs_value v);
void     cs_value_release(cs_value v);

// Introspection helpers (optional)
cs_type  cs_typeof(cs_value v);
const char* cs_type_name(cs_type t);

// Convert to C string (returns internal pointer; valid while VM lives)
const char* cs_to_cstr(cs_value v);

// Capture current call stack (for error objects)
cs_value cs_capture_stack_trace(cs_vm* vm);

// C API for list operations (for hosts to manipulate lists)
size_t cs_list_len(cs_value list_val);
cs_value cs_list_get(cs_value list_val, size_t index);  // returns cs_nil() if out of bounds
int cs_list_set(cs_value list_val, size_t index, cs_value value);  // returns 0 on success
int cs_list_push(cs_value list_val, cs_value value);  // returns 0 on success
cs_value cs_list_pop(cs_value list_val);  // returns cs_nil() if empty

// C API for map operations (for hosts to manipulate maps)
size_t cs_map_len(cs_value map_val);
cs_value cs_map_get(cs_value map_val, const char* key);  // returns cs_nil() if missing
int cs_map_set(cs_value map_val, const char* key, cs_value value);  // returns 0 on success
int cs_map_has(cs_value map_val, const char* key);  // returns 1 if key exists
int cs_map_del(cs_value map_val, const char* key);  // returns 0 on success
cs_value cs_map_keys(cs_vm* vm, cs_value map_val);  // returns list of string keys

// Safety controls (prevents runaway scripts from hanging host)
// Set instruction limit (0 = unlimited). Script aborts if exceeded.
void cs_vm_set_instruction_limit(cs_vm* vm, uint64_t limit);
// Set execution timeout in milliseconds (0 = unlimited). Script aborts if exceeded.
void cs_vm_set_timeout(cs_vm* vm, uint64_t timeout_ms);
// Request immediate interruption of running script (thread-safe signal).
void cs_vm_interrupt(cs_vm* vm);
// Get current instruction count (useful for profiling).
uint64_t cs_vm_get_instruction_count(cs_vm* vm);

#ifdef __cplusplus
}
#endif

#endif
