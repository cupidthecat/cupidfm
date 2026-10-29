# Embedding CupidScript

Include `cupidscript.h`, create a VM with `cs_vm_new()`, register any native functions or globals, and run code with `cs_vm_run_string()` or `cs_vm_run_file()`. Check return codes and read `cs_vm_last_error()` when execution fails. Release host-held values, then destroy the VM with `cs_vm_free()`.

## Value ownership

Constructors return an owned reference. `cs_value_copy(value)` retains another reference, and `cs_value_release(value)` releases one. Lists, maps, tuples, globals, and calls retain values they store; passing a newly allocated string directly to a setter still leaves the caller's reference to release.

Getters such as `cs_list_get`, `cs_map_get`, and tuple getters return retained values. Release each result when finished. `cs_to_cstr()` returns a borrowed pointer valid while the corresponding string remains alive.

The following complete example builds a named tuple containing a list and releases every owned value:

```c
#include "cupidscript.h"

int main(void) {
    cs_vm *vm = cs_vm_new();
    if (!vm) return 1;
    cs_value items = cs_list(vm);
    cs_value name = cs_str(vm, "example");
    cs_value record = cs_tuple(vm, 2);
    int failed = !items.as.p || !name.as.p || !record.as.p;
    if (!failed) {
        failed = cs_list_push(items, cs_int(42)) != 0 ||
                 cs_tuple_set_named(record, "name", name) != 0 ||
                 cs_tuple_set_named(record, "items", items) != 0;
    }
    cs_value retained = cs_nil();
    if (!failed) {
        retained = cs_tuple_get_named(record, "items");
        failed = retained.type != CS_T_LIST || cs_list_len(retained) != 1;
    }
    cs_value_release(retained);
    cs_value_release(record);
    cs_value_release(name);
    cs_value_release(items);
    cs_vm_free(vm);
    return failed;
}
```

From the source checkout, build the library and link the example:

```sh
make lib
cc -std=c99 -Isrc example.c bin/libcupidscript.a -lssl -lcrypto -lz -lm -pthread -o example
./example
```

## Tuple builders

`cs_tuple(vm, count)` allocates a fixed number of fields. Positional setters update an index. Named setters update an existing name or use the first unnamed slot. Setters return zero on success and minus one for invalid input, full tuples, or allocation failure. An unnamed slot may already contain a positional value, so use one construction style unless that replacement is intended.

Getters retain their returned values. Out-of-range positions and missing names return nil. `cs_tuple_field_name()` returns a borrowed field-name pointer. Host builders can set fields; script assignment to tuple fields remains read-only.

## Collection and VM shutdown

The cycle collector follows list items, map keys and values, set members, and tuple fields. Host-held references keep reachable containers alive. Call `cs_vm_collect_cycles()` directly or configure automatic collection through the GC controls.

VM shutdown stops the event loop, releases queued tasks, breaks environment/closure cycles, clears VM roots, and collects unreachable container cycles. Retained containers are detached from the destroyed VM. Script callbacks must not be invoked after their VM is destroyed.

See [Errors and Debugging](Errors-and-Debugging) for explicit exceptions, runtime errors, and cleanup behavior.
