# Host Safety Controls

## Table of Contents

- [Overview](#overview)
- [Instruction Limit](#instruction-limit)
- [Timeout](#timeout)
- [Interrupt](#interrupt)
- [Checking Instruction Count](#checking-instruction-count)
- [Best Practices](#best-practices)
- [Implementation Details](#implementation-details)
- [Testing](#testing)
- [Error Handling](#error-handling)

CupidScript provides comprehensive safety controls to protect the host application from malicious or buggy scripts that could hang the system.

## Overview

When embedding CupidScript (e.g., in CupidFM for user plugins), it's critical to prevent scripts from:

- Running infinite loops that freeze the application
- Consuming excessive CPU time
- Blocking the UI thread indefinitely

The VM provides three layers of protection:

1. **Instruction Limit** - caps total operations
2. **Timeout** - caps wall-clock execution time
3. **Interrupt** - allows host to cancel execution

Safety controls can be configured in two ways:

- **Host-level (C API)** - Set default limits for all scripts
- **Script-level** - Scripts can adjust their own limits within bounds

## Instruction Limit

### Host-Level Configuration

```c
cs_vm_set_instruction_limit(vm, 10000000);  // 10 million instructions
```

### Script-Level Configuration

```c
// Scripts can configure their own limits
set_instruction_limit(50000000);  // Request 50M instructions

// Check current limit
let limit = get_instruction_limit();
print("Instruction limit:", limit);

// Check current count
let count = get_instruction_count();
print("Instructions executed:", count);
```

### Behavior

- Counts every expression evaluation in the VM
- When limit is exceeded, script aborts with error
- Error message: `"instruction limit exceeded (N instructions)"`
- Default: `0` (unlimited)

### When to Use

- Predictable CPU budgets
- Scripts with known complexity bounds
- Testing/debugging script performance

### Example

```c
cs_vm* vm = cs_vm_new();
cs_register_stdlib(vm);

// Allow up to 50 million operations
cs_vm_set_instruction_limit(vm, 50000000);

int rc = cs_vm_run_file(vm, "plugin.cs");
if (rc != 0) {
    fprintf(stderr, "%s\n", cs_vm_last_error(vm));
    fprintf(stderr, "Instructions: %llu\n",
            (unsigned long long)cs_vm_get_instruction_count(vm));
}
```

## Timeout

### Host-Level Configuration

```c
cs_vm_set_timeout(vm, 5000);  // 5 second timeout
```

### Script-Level Configuration

```c
// Heavy processing script can request more time
set_timeout(30000);  // Request 30 seconds

// Check current timeout
let timeout = get_timeout();
print("Timeout:", timeout, "ms");

// Quick operations can set shorter timeout
set_timeout(1000);  // 1 second for fast response
```

### Behavior

- Measures wall-clock time from script start
- Checked every 1000 instructions (minimal overhead)
- When exceeded, script aborts with error
- Error message: `"execution timeout exceeded (N ms)"`
- Default: `0` (unlimited)

### When to Use

- Real-time constraints (e.g., UI responsiveness)
- Scripts doing I/O or system calls
- User-facing plugin execution

### Recommended Values

- **UI plugins**: 1-5 seconds (keeps interface responsive)
- **Batch processing**: 30-60 seconds (handles larger datasets)
- **Background tasks**: 5-10 minutes (heavy operations)

### Example

```c
// Plugin must complete within 10 seconds
cs_vm_set_timeout(vm, 10000);

int rc = cs_vm_run_string(vm, user_code, "user_plugin");
if (rc != 0) {
    printf("Plugin timed out: %s\n", cs_vm_last_error(vm));
}
```

## Interrupt

### Usage

```c
// From main thread:
cs_vm_run_file(vm, "long_script.cs");

// From UI thread (e.g., cancel button):
cs_vm_interrupt(vm);
```

### Behavior

- Sets a thread-safe flag checked on every expression
- Script aborts immediately when flag is detected
- Error message: `"execution interrupted by host"`
- Flag auto-resets at start of each script execution

### When to Use

- User-initiated cancellation
- Application shutdown
- Emergency abort scenarios

### Example

```c
#include <pthread.h>

typedef struct {
    cs_vm* vm;
    const char* script_path;
} vm_thread_args;

void* run_script_thread(void* arg) {
    vm_thread_args* args = (vm_thread_args*)arg;
    cs_vm_run_file(args->vm, args->script_path);
    return NULL;
}

int main() {
    cs_vm* vm = cs_vm_new();
    cs_register_stdlib(vm);
    
    vm_thread_args args = { vm, "long_script.cs" };
    pthread_t thread;
    pthread_create(&thread, NULL, run_script_thread, &args);
    
    // Wait for user input
    printf("Press Enter to cancel script...\n");
    getchar();
    
    // Interrupt from main thread
    cs_vm_interrupt(vm);
    
    pthread_join(thread, NULL);
    printf("Script status: %s\n", cs_vm_last_error(vm));
    
    cs_vm_free(vm);
    return 0;
}
```

## Checking Instruction Count

Retrieve the current count for profiling or debugging:

```c
uint64_t count = cs_vm_get_instruction_count(vm);
printf("Script executed %llu instructions\n", (unsigned long long)count);
```

This is useful for:
- Profiling script complexity
- Comparing algorithm efficiency
- Debugging performance issues

## Best Practices

### For Interactive Applications (CupidFM)

```c
// Reasonable defaults for user plugins:
cs_vm_set_instruction_limit(vm, 100000000);  // 100M instructions
cs_vm_set_timeout(vm, 30000);                // 30 second timeout

// Allow user to cancel long operations
on_cancel_button_click() {
    cs_vm_interrupt(vm);
}
```

### For Batch Processing

```c
// Higher limits for non-interactive scripts
cs_vm_set_instruction_limit(vm, 1000000000); // 1B instructions
cs_vm_set_timeout(vm, 300000);               // 5 minute timeout
```

### For Untrusted Code

```c
// Strict limits for sandboxed execution
cs_vm_set_instruction_limit(vm, 10000000);   // 10M instructions
cs_vm_set_timeout(vm, 5000);                 // 5 second timeout
```

### Combining Controls

```c
// Use both instruction limit AND timeout for maximum safety
cs_vm_set_instruction_limit(vm, 50000000);   // CPU budget
cs_vm_set_timeout(vm, 10000);                // Wall-clock budget

// Script will abort when EITHER limit is exceeded
int rc = cs_vm_run_file(vm, "script.cs");
```

## Implementation Details

### Performance Impact

- Instruction counting: negligible overhead (simple increment)
- Timeout checking: only every 1000 instructions
- Interrupt flag: single atomic read per expression

### Thread Safety

- `cs_vm_interrupt()` is the only thread-safe VM function
- All other VM operations must run on a single thread
- Interrupt flag uses volatile semantics for visibility

### Limit Reset

All safety counters automatically reset when calling:
- `cs_vm_run_file()`
- `cs_vm_run_string()`
- `cs_call()` / `cs_call_value()`

This ensures each script execution starts with a fresh budget.

## Testing

See `examples/safety_demo.c` for a complete working demonstration of all safety controls.

Run tests:

```sh
# Compile safety demo
gcc -std=c99 -O2 -Isrc examples/safety_demo.c bin/libcupidscript.a -lm -o bin/safety_demo

# Run demonstrations
./bin/safety_demo
```

## Error Handling

When a safety limit is exceeded, the VM:

1. Sets `vm->last_error` with descriptive message
2. Returns error code from `cs_vm_run_*` function
3. Preserves instruction count for debugging
4. Includes script location in error message

Example error messages:

- `"Runtime error at script.cs:10:5: instruction limit exceeded (10000000 instructions)"`
- `"Runtime error at plugin.cs:45:12: execution timeout exceeded (5000 ms)"`
- `"Runtime error at user.cs:23:8: execution interrupted by host"`
