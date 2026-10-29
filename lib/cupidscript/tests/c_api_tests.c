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

static int test_tuple_api(void) {
    int rc = 0;
    cs_vm* vm = cs_vm_new();
    if (!vm) return 1;
    cs_value tuple = cs_tuple(vm, 2);
    cs_value text = cs_str(vm, "owned field");
    rc |= expect_true(tuple.type == CS_T_TUPLE && cs_tuple_len(tuple) == 2, "tuple allocation");
    rc |= expect_true(cs_tuple_set_named(tuple, "name", text) == 0, "named tuple setter");
    cs_value_release(text);
    rc |= expect_true(cs_tuple_set_named(tuple, "count", cs_int(2)) == 0, "second named field");
    rc |= expect_true(cs_tuple_set_named(tuple, "count", cs_int(3)) == 0, "replace named field");
    rc |= expect_true(cs_tuple_set_named(tuple, "extra", cs_nil()) != 0, "tuple capacity checked");
    rc |= expect_true(cs_tuple_set_positional(tuple, 2, cs_int(0)) != 0, "tuple bounds checked");
    rc |= expect_true(cs_tuple_get_positional(tuple, 2).type == CS_T_NIL, "tuple read bounds");
    rc |= expect_true(cs_tuple_get_named(tuple, "missing").type == CS_T_NIL, "missing named field");
    rc |= expect_true(strcmp(cs_tuple_field_name(tuple, 0), "name") == 0, "tuple field name");
    rc |= expect_true(cs_tuple_field_name(tuple, 2) == NULL, "field name bounds");
    cs_value count = cs_tuple_get_named(tuple, "count");
    rc |= expect_true(count.type == CS_T_INT && count.as.i == 3, "named getter");
    cs_value_release(count);
    cs_value retained = cs_value_copy(tuple);
    cs_value_release(tuple);
    cs_value field = cs_tuple_get_positional(retained, 0);
    cs_value_release(retained);
    rc |= expect_true(field.type == CS_T_STR && strcmp(cs_to_cstr(field), "owned field") == 0,
                      "tuple getters retain fields");
    cs_value_release(field);

    // Retained containers must detach from the VM before it is destroyed.
    cs_value external = cs_tuple(vm, 1);
    cs_value list = cs_list(vm);
    cs_value map = cs_map(vm);
    cs_list_push(list, map);
    cs_tuple_set_positional(external, 0, list);
    cs_value_release(list);
    cs_value_release(map);
    cs_vm_free(vm);
    cs_value_release(external);
    return rc;
}

static int test_cycle_roots(void) {
    int rc = 0;
    cs_vm* vm = cs_vm_new();
    if (!vm) return 1;
    cs_vm_set_gc_threshold(vm, 1);
    cs_vm_set_gc_alloc_trigger(vm, 1);
    cs_value list = cs_list(vm);
    cs_value map = cs_map(vm);
    cs_value tuple = cs_tuple(vm, 1);
    cs_list_push(list, map);
    cs_map_set_value(map, tuple, list);
    cs_tuple_set_positional(tuple, 0, map);
    cs_value_release(map);
    cs_value_release(tuple);
    rc |= expect_true(cs_vm_collect_cycles(vm) == 0, "host root keeps container graph alive");
    cs_value child = cs_list_get(list, 0);
    rc |= expect_true(cs_map_len(child) == 1, "reachable map retained through cycle collection");
    cs_value_release(child);
    cs_value_release(list);
    rc |= expect_true(cs_vm_collect_cycles(vm) == 3, "collect list/map-key/tuple cycle");
    rc |= expect_true(vm->tracked_count == 0, "collected containers untracked");
    cs_vm_free(vm);
    return rc;
}

static int test_cleanup_errors(void) {
    int rc = 0;
    cs_vm* vm = cs_vm_new();
    if (!vm) return 1;
    cs_register_native(vm, "store", store_value, NULL);
    const char* cases[] = {
        "try { let value = 1 / 0; } catch (e) { store(99); } finally { store(1); }",
        "try { throw 7; } catch (e) { let value = 1 / 0; } finally { store(2); }",
        "try { try { let value = 1 / 0; } catch (e) {} finally { store(3); } } catch (e) {} finally { store(4); }",
        "defer { store(5); } let value = 1 / 0;"
    };
    const int expected[] = {1, 2, 4, 5};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        cs_value_release(g_stored); g_stored = cs_nil();
        rc |= expect_true(cs_vm_run_string(vm, cases[i], "<cleanup errors>") != 0, "runtime error propagates");
        rc |= expect_true(strstr(cs_vm_last_error(vm), "division by zero") != NULL, "original runtime error retained");
        rc |= expect_true(g_stored.type == CS_T_INT && g_stored.as.i == expected[i], "cleanup runs after runtime error");
    }
    rc |= expect_true(cs_vm_run_string(vm,
        "try { let value = 1 / 0; } catch (e) {} finally { missing_cleanup(); }",
        "<cleanup override>") != 0, "cleanup failure propagates");
    rc |= expect_true(strstr(cs_vm_last_error(vm), "undefined variable") != NULL, "cleanup error replaces prior error");
    cs_value_release(g_stored); g_stored = cs_nil();
    cs_vm_free(vm);
    return rc;
}

int main(void) {
    int rc = test_tuple_api() | test_cycle_roots() | test_cleanup_errors();

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
