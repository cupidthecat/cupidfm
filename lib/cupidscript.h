#ifndef CUPIDSCRIPT_H
#define CUPIDSCRIPT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cs_vm cs_vm;

typedef enum {
    CS_T_NIL = 0,
    CS_T_BOOL,
    CS_T_INT,
    CS_T_STR,
    CS_T_LIST,
    CS_T_MAP,
    CS_T_STRBUF,
    CS_T_FUNC,
    CS_T_NATIVE
} cs_type;

typedef struct {
    cs_type type;
    union {
        int      b;
        int64_t  i;
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

// Calling functions defined in script
int cs_call(cs_vm* vm, const char* func_name, int argc, const cs_value* argv, cs_value* out);
int cs_call_value(cs_vm* vm, cs_value callee, int argc, const cs_value* argv, cs_value* out);

// Register native functions (for CupidFM to expose API)
void cs_register_native(cs_vm* vm, const char* name, cs_native_fn fn, void* userdata);

// Error text (valid until next VM call that sets it)
const char* cs_last_error(const cs_vm* vm);

// Helpers for building values
cs_value cs_nil(void);
cs_value cs_bool(int v);
cs_value cs_int(int64_t v);
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

#ifdef __cplusplus
}
#endif

#endif
