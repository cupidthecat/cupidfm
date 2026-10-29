#include "cupidscript.h"
#include "cs_vm.h"
#include "cs_parser.h"
#include <stdio.h>
#include <string.h>

static cs_value g_stored = {0};

static int store_value(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_nil();
    if (argc != 1) {
        cs_error(vm, "store_value expects 1 arg");
        return 1;
    }

    // Replace any existing stored value.
    cs_value_release(g_stored);
    g_stored = cs_value_copy(argv[0]);
    return 0;
}

static int expect_true(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        return 1;
    }
    return 0;
}

int main(void) {
    int rc = 0;

    cs_vm* vm = cs_vm_new();
    rc |= expect_true(vm != NULL, "cs_vm_new");
    if (!vm) return 1;

    cs_register_stdlib(vm);

    // Exercise error getters/setters.
    (void)cs_vm_last_error(vm);
    (void)cs_vm_last_error(NULL);
    cs_error(vm, "hello-error");
    rc |= expect_true(strcmp(cs_vm_last_error(vm), "hello-error") == 0, "cs_vm_last_error reflects cs_error");
    rc |= expect_true(strcmp(cs_last_error(vm), "hello-error") == 0, "cs_last_error reflects cs_error");
    cs_error(vm, NULL);
    rc |= expect_true(strcmp(cs_vm_last_error(vm), "error") == 0, "cs_error NULL msg");

    // Exercise safety controls API.
    cs_vm_set_instruction_limit(vm, 123);
    cs_vm_set_timeout(vm, 456);
    cs_vm_interrupt(vm);
    (void)cs_vm_get_instruction_count(vm);

    // Exercise list/map C APIs.
    cs_value lv = cs_list(vm);
    rc |= expect_true(lv.type == CS_T_LIST, "cs_list type");
    rc |= expect_true(cs_list_len(lv) == 0, "cs_list_len empty");
    rc |= expect_true(cs_list_pop(lv).type == CS_T_NIL, "cs_list_pop empty -> nil");

    rc |= expect_true(cs_list_push(lv, cs_int(1)) == 0, "cs_list_push");
    rc |= expect_true(cs_list_push(lv, cs_int(2)) == 0, "cs_list_push 2");
    rc |= expect_true(cs_list_len(lv) == 2, "cs_list_len 2");

    cs_value v0 = cs_list_get(lv, 0);
    rc |= expect_true(v0.type == CS_T_INT && v0.as.i == 1, "cs_list_get 0");
    cs_value_release(v0);

    rc |= expect_true(cs_list_set(lv, 1, cs_int(9)) == 0, "cs_list_set");
    cs_value v1 = cs_list_get(lv, 1);
    rc |= expect_true(v1.type == CS_T_INT && v1.as.i == 9, "cs_list_get updated");
    cs_value_release(v1);

    cs_value vp = cs_list_pop(lv);
    rc |= expect_true(vp.type == CS_T_INT && vp.as.i == 9, "cs_list_pop -> 9");
    cs_value_release(vp);

    cs_value mv = cs_map(vm);
    rc |= expect_true(mv.type == CS_T_MAP, "cs_map type");
    rc |= expect_true(cs_map_len(mv) == 0, "cs_map_len empty");
    rc |= expect_true(cs_map_has(mv, "a") == 0, "cs_map_has missing");

    rc |= expect_true(cs_map_set(mv, "a", cs_int(1)) == 0, "cs_map_set");
    rc |= expect_true(cs_map_has(mv, "a") == 1, "cs_map_has present");

    cs_value ga = cs_map_get(mv, "a");
    rc |= expect_true(ga.type == CS_T_INT && ga.as.i == 1, "cs_map_get");
    cs_value_release(ga);

    rc |= expect_true(cs_map_del(mv, "a") == 0, "cs_map_del");
    rc |= expect_true(cs_map_has(mv, "a") == 0, "cs_map_has after del");

    cs_value ks = cs_map_keys(vm, mv);
    rc |= expect_true(ks.type == CS_T_LIST, "cs_map_keys returns list");
    cs_value_release(ks);

    // Exercise cs_call_value by capturing a function value from script.
    cs_register_native(vm, "store", store_value, NULL);

    const char* code =
        "fn add(a, b) { return a + b; }\n"
        "store(add);\n";

    if (cs_vm_run_string(vm, code, "<c_api_tests>") != 0) {
        fprintf(stderr, "script error: %s\n", cs_vm_last_error(vm));
        rc |= 1;
    } else {
        cs_value out = cs_nil();
        cs_value argv[2] = { cs_int(2), cs_int(3) };
        int call_rc = cs_call_value(vm, g_stored, 2, argv, &out);
        rc |= expect_true(call_rc == 0, "cs_call_value rc");
        rc |= expect_true(out.type == CS_T_INT && out.as.i == 5, "cs_call_value result");
        cs_value_release(out);
    }

    // Exercise stack trace capture (basic sanity: returns a value).
    cs_value st = cs_capture_stack_trace(vm);
    rc |= expect_true(st.type == CS_T_LIST || st.type == CS_T_NIL, "cs_capture_stack_trace type");
    cs_value_release(st);

    // Exercise cs_typeof / cs_type_name branches and cs_to_cstr non-string.
    rc |= expect_true(cs_typeof(cs_nil()) == CS_T_NIL, "cs_typeof nil");
    rc |= expect_true(strcmp(cs_type_name(CS_T_STRBUF), "strbuf") == 0, "cs_type_name strbuf");
    rc |= expect_true(strcmp(cs_type_name(CS_T_FUNC), "function") == 0, "cs_type_name function");
    rc |= expect_true(strcmp(cs_type_name(CS_T_NATIVE), "native") == 0, "cs_type_name native");
    rc |= expect_true(strcmp(cs_type_name((cs_type)12345), "unknown") == 0, "cs_type_name unknown");
    rc |= expect_true(cs_to_cstr(cs_int(5)) == NULL, "cs_to_cstr non-string returns NULL");

    // Exercise cs_str_new_take owned==NULL via cs_str_take.
    cs_value empty_taken = cs_str_take(vm, NULL, 0);
    rc |= expect_true(empty_taken.type == CS_T_STR, "cs_str_take NULL owned returns string");
    cs_value_release(empty_taken);
    cs_str_decref(NULL);

    // Exercise parser_init source_name fallback and parse error cleanup.
    {
        parser P;
        parser_init(&P, "let a = 1;", NULL);
        ast* prog = parse_program(&P);
        rc |= expect_true(P.error == NULL, "parser_init default source name");
        parse_free_error(&P);
        ast_free(prog);
    }

    {
        parser P;
        parser_init(&P, "fn broken() {", NULL);
        ast* prog = parse_program(&P);
        rc |= expect_true(P.error != NULL, "parser reports unterminated block");
        parse_free_error(&P);
        ast_free(prog);
    }

    cs_value_release(lv);
    cs_value_release(mv);
    cs_value_release(g_stored);
    g_stored = cs_nil();

    cs_vm_free(vm);

    if (rc == 0) {
        printf("c_api_tests ok\n");
    }
    return rc ? 1 : 0;
}
