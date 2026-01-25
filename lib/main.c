#include "cupidscript.h"
#include "cs_vm.h"
// Forward declare in case header exposure is not picked up by the build system yet
extern const char* cs_vm_last_error(cs_vm* vm);
#include <stdio.h>

// Example "fm" natives (stand-in for CupidFM integration)
static int fm_status(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (argc == 1 && argv[0].type == CS_T_STR) {
        fprintf(stderr, "[fm.status] %s\n", cs_to_cstr(argv[0]));
    }
    if (out) *out = cs_nil();
    return 0;
}

static int fm_selected_path(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    // placeholder: CupidFM will return current selection
    *out = cs_str(vm, "/tmp/example.txt");
    return 0;
}

static int fm_open(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (argc == 1 && argv[0].type == CS_T_STR) {
        fprintf(stderr, "[fm.open] %s\n", cs_to_cstr(argv[0]));
    }
    if (out) *out = cs_nil();
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <script.cs>\n", argv[0]);
        return 2;
    }

    cs_vm* vm = cs_vm_new();
    if (!vm) {
        fprintf(stderr, "failed to create VM\n");
        return 1;
    }

    // base stdlib
    cs_register_stdlib(vm);

    // CupidFM API would register lots of these:
    cs_register_native(vm, "fm.status", fm_status, NULL);
    cs_register_native(vm, "fm.selected_path", fm_selected_path, NULL);
    cs_register_native(vm, "fm.open", fm_open, NULL);

    int rc = cs_vm_run_file(vm, argv[1]);
    if (rc != 0) {
        fprintf(stderr, "%s\n", cs_vm_last_error(vm));
        cs_vm_free(vm);
        return 1;
    }

    // If script defines on_load(), call it
    cs_value out = cs_nil();
    (void)cs_call(vm, "on_load", 0, NULL, &out);
    cs_value_release(out);

    cs_vm_free(vm);
    return 0;
}
