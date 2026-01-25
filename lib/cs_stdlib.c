#include "cs_vm.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#if !defined(_WIN32)
#include <sys/time.h>
#endif

static char* cs_strdup2_local(const char* s) {
    size_t n = strlen(s ? s : "");
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static int is_abs_path(const char* p) {
    if (!p || !*p) return 0;
    if (p[0] == '/' || p[0] == '\\') return 1;
    if (isalpha((unsigned char)p[0]) && p[1] == ':') return 1; // Windows drive
    return 0;
}

static char* path_join_alloc(const char* a, const char* b) {
    if (!a || !*a) return cs_strdup2_local(b ? b : "");
    if (!b || !*b) return cs_strdup2_local(a);

    size_t na = strlen(a), nb = strlen(b);
    int need_sep = 1;
    if (na > 0) {
        char last = a[na - 1];
        if (last == '/' || last == '\\') need_sep = 0;
    }
    if (b[0] == '/' || b[0] == '\\') need_sep = 0;

    char* out = (char*)malloc(na + (need_sep ? 1 : 0) + nb + 1);
    if (!out) return NULL;
    memcpy(out, a, na);
    size_t w = na;
    if (need_sep) out[w++] = '/';
    memcpy(out + w, b, nb + 1);
    return out;
}

static const char* vm_current_dir(const cs_vm* vm) {
    if (!vm || vm->dir_count == 0) return ".";
    return vm->dir_stack[vm->dir_count - 1];
}

static char* resolve_path_alloc(cs_vm* vm, const char* p) {
    if (is_abs_path(p)) return cs_strdup2_local(p);
    return path_join_alloc(vm_current_dir(vm), p);
}

static const char* value_repr(cs_value v, char* buf, size_t buf_sz) {
    if (!buf || buf_sz == 0) return "";
    buf[0] = 0;
    switch (v.type) {
        case CS_T_NIL:  return "nil";
        case CS_T_BOOL: return v.as.b ? "true" : "false";
        case CS_T_INT:
            snprintf(buf, buf_sz, "%lld", (long long)v.as.i);
            return buf;
        case CS_T_STR:
            return ((cs_string*)v.as.p)->data;
        case CS_T_LIST: {
            cs_list_obj* l = (cs_list_obj*)v.as.p;
            snprintf(buf, buf_sz, "<list len=%lld>", (long long)(l ? l->len : 0));
            return buf;
        }
        case CS_T_MAP: {
            cs_map_obj* m = (cs_map_obj*)v.as.p;
            snprintf(buf, buf_sz, "<map len=%lld>", (long long)(m ? m->len : 0));
            return buf;
        }
        case CS_T_STRBUF: {
            cs_strbuf_obj* b = (cs_strbuf_obj*)v.as.p;
            snprintf(buf, buf_sz, "<strbuf len=%lld>", (long long)(b ? b->len : 0));
            return buf;
        }
        case CS_T_FUNC:   return "<function>";
        case CS_T_NATIVE: return "<native>";
        default:          return "<obj>";
    }
}

static int nf_print(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    for (int i = 0; i < argc; i++) {
        char tmp[64];
        switch (argv[i].type) {
            case CS_T_NIL:  fputs("nil", stdout); break;
            case CS_T_BOOL: fputs(argv[i].as.b ? "true" : "false", stdout); break;
            case CS_T_INT:  fprintf(stdout, "%lld", (long long)argv[i].as.i); break;
            case CS_T_STR:  fputs(((cs_string*)argv[i].as.p)->data, stdout); break;
            default:        fputs(value_repr(argv[i], tmp, sizeof(tmp)), stdout); break;
        }
        if (i + 1 < argc) fputc(' ', stdout);
    }
    fputc('\n', stdout);
    if (out) *out = cs_nil();
    return 0;
}

static int nf_typeof(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc != 1) { if (out) *out = cs_nil(); return 0; }
    const char* tn = cs_type_name(argv[0].type);
    *out = cs_str(vm, tn);
    return 0;
}

static int nf_getenv(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }
    const char* key = ((cs_string*)argv[0].as.p)->data;
    const char* v = getenv(key);
    if (!v) { *out = cs_nil(); return 0; }
    *out = cs_str(vm, v);
    return 0;
}

static int truthy_local(cs_value v) {
    if (v.type == CS_T_NIL) return 0;
    if (v.type == CS_T_BOOL) return v.as.b != 0;
    return 1;
}

static int nf_assert(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_nil();
    if (argc < 1) return 0;
    if (!truthy_local(argv[0])) {
        if (argc >= 2 && argv[1].type == CS_T_STR) {
            cs_error(vm, cs_to_cstr(argv[1]));
        } else {
            cs_error(vm, "assertion failed");
        }
        return 1;
    }
    return 0;
}

static int vm_require_has(cs_vm* vm, const char* path) {
    if (!vm || !path) return 0;
    for (size_t i = 0; i < vm->require_count; i++) {
        if (strcmp(vm->require_list[i], path) == 0) return 1;
    }
    return 0;
}

static void vm_require_add(cs_vm* vm, char* owned_path) {
    if (!vm || !owned_path) { free(owned_path); return; }
    if (vm->require_count == vm->require_cap) {
        size_t nc = vm->require_cap ? vm->require_cap * 2 : 16;
        char** nl = (char**)realloc(vm->require_list, nc * sizeof(char*));
        if (!nl) { free(owned_path); return; }
        vm->require_list = nl;
        vm->require_cap = nc;
    }
    vm->require_list[vm->require_count++] = owned_path;
}

static int nf_load(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_nil();
    if (argc != 1 || argv[0].type != CS_T_STR) return 0;

    char* path = resolve_path_alloc(vm, cs_to_cstr(argv[0]));
    if (!path) { cs_error(vm, "out of memory"); return 1; }

    int rc = cs_vm_run_file(vm, path);
    free(path);
    if (rc != 0) return 1;
    if (out) *out = cs_bool(1);
    return 0;
}

static int nf_require(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_nil();
    if (argc != 1 || argv[0].type != CS_T_STR) return 0;

    char* path = resolve_path_alloc(vm, cs_to_cstr(argv[0]));
    if (!path) { cs_error(vm, "out of memory"); return 1; }

    if (vm_require_has(vm, path)) {
        free(path);
        if (out) *out = cs_bool(1);
        return 0;
    }

    int rc = cs_vm_run_file(vm, path);
    if (rc != 0) { free(path); return 1; }

    vm_require_add(vm, path); // takes ownership
    if (out) *out = cs_bool(1);
    return 0;
}

static int list_ensure(cs_list_obj* l, size_t need) {
    if (!l) return 0;
    if (need <= l->cap) return 1;
    size_t nc = l->cap ? l->cap : 8;
    while (nc < need) nc *= 2;
    cs_value* ni = (cs_value*)realloc(l->items, nc * sizeof(cs_value));
    if (!ni) return 0;
    for (size_t i = l->cap; i < nc; i++) ni[i] = cs_nil();
    l->items = ni;
    l->cap = nc;
    return 1;
}

static int nf_list(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    *out = cs_list(vm);
    if (!out->as.p) { cs_error(vm, "out of memory"); return 1; }
    return 0;
}

static int nf_map(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    *out = cs_map(vm);
    if (!out->as.p) { cs_error(vm, "out of memory"); return 1; }
    return 0;
}

static int nf_strbuf(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    *out = cs_strbuf(vm);
    if (!out->as.p) { cs_error(vm, "out of memory"); return 1; }
    return 0;
}

static int nf_len(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_int(0); return 0; }
    if (argv[0].type == CS_T_STR) { *out = cs_int((int64_t)((cs_string*)argv[0].as.p)->len); return 0; }
    if (argv[0].type == CS_T_LIST) { *out = cs_int((int64_t)((cs_list_obj*)argv[0].as.p)->len); return 0; }
    if (argv[0].type == CS_T_MAP) { *out = cs_int((int64_t)((cs_map_obj*)argv[0].as.p)->len); return 0; }
    if (argv[0].type == CS_T_STRBUF) { *out = cs_int((int64_t)((cs_strbuf_obj*)argv[0].as.p)->len); return 0; }
    *out = cs_int(0);
    return 0;
}

static int nf_push(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (out) *out = cs_nil();
    if (argc != 2 || argv[0].type != CS_T_LIST) return 0;
    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    if (!list_ensure(l, l->len + 1)) { cs_error(vm, "out of memory"); return 1; }
    l->items[l->len++] = cs_value_copy(argv[1]);
    return 0;
}

static int nf_pop(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_LIST) { *out = cs_nil(); return 0; }
    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    if (!l || l->len == 0) { *out = cs_nil(); return 0; }
    *out = l->items[l->len - 1];
    l->items[l->len - 1] = cs_nil();
    l->len--;
    return 0;
}

static int map_find(cs_map_obj* m, const char* key) {
    if (!m || !key) return -1;
    for (size_t i = 0; i < m->len; i++) {
        if (strcmp(m->entries[i].key->data, key) == 0) return (int)i;
    }
    return -1;
}

static int map_ensure(cs_map_obj* m, size_t need) {
    if (!m) return 0;
    if (need <= m->cap) return 1;
    size_t nc = m->cap ? m->cap : 8;
    while (nc < need) nc *= 2;
    cs_map_entry* ne = (cs_map_entry*)realloc(m->entries, nc * sizeof(cs_map_entry));
    if (!ne) return 0;
    for (size_t i = m->cap; i < nc; i++) { ne[i].key = NULL; ne[i].val = cs_nil(); }
    m->entries = ne;
    m->cap = nc;
    return 1;
}

static int nf_mget(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_MAP || argv[1].type != CS_T_STR) { *out = cs_nil(); return 0; }
    cs_map_obj* m = (cs_map_obj*)argv[0].as.p;
    const char* key = ((cs_string*)argv[1].as.p)->data;
    int idx = map_find(m, key);
    if (idx < 0) { *out = cs_nil(); return 0; }
    *out = cs_value_copy(m->entries[(size_t)idx].val);
    return 0;
}

static int nf_mset(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_nil();
    if (argc != 3 || argv[0].type != CS_T_MAP || argv[1].type != CS_T_STR) return 0;
    cs_map_obj* m = (cs_map_obj*)argv[0].as.p;
    cs_string* key = (cs_string*)argv[1].as.p;
    int idx = map_find(m, key->data);
    if (idx >= 0) {
        cs_value_release(m->entries[(size_t)idx].val);
        m->entries[(size_t)idx].val = cs_value_copy(argv[2]);
        return 0;
    }
    if (!map_ensure(m, m->len + 1)) { cs_error(vm, "out of memory"); return 1; }
    cs_str_incref(key);
    m->entries[m->len].key = key;
    m->entries[m->len].val = cs_value_copy(argv[2]);
    m->len++;
    return 0;
}

static int nf_mhas(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_MAP || argv[1].type != CS_T_STR) { *out = cs_bool(0); return 0; }
    cs_map_obj* m = (cs_map_obj*)argv[0].as.p;
    const char* key = ((cs_string*)argv[1].as.p)->data;
    *out = cs_bool(map_find(m, key) >= 0);
    return 0;
}

static int nf_keys(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_MAP) { *out = cs_list(vm); return 0; }
    cs_map_obj* m = (cs_map_obj*)argv[0].as.p;
    cs_value listv = cs_list(vm);
    if (!listv.as.p) { cs_error(vm, "out of memory"); return 1; }
    cs_list_obj* l = (cs_list_obj*)listv.as.p;
    if (!list_ensure(l, m->len)) { cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
    for (size_t i = 0; i < m->len; i++) {
        cs_value sv; sv.type = CS_T_STR; sv.as.p = m->entries[i].key;
        cs_str_incref(m->entries[i].key);
        l->items[l->len++] = sv;
    }
    *out = listv;
    return 0;
}

static int nf_str_find(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { *out = cs_int(-1); return 0; }
    const char* s = ((cs_string*)argv[0].as.p)->data;
    const char* sub = ((cs_string*)argv[1].as.p)->data;
    const char* p = strstr(s, sub);
    if (!p) { *out = cs_int(-1); return 0; }
    *out = cs_int((int64_t)(p - s));
    return 0;
}

static int nf_str_replace(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 3 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR || argv[2].type != CS_T_STR) { *out = cs_nil(); return 0; }
    const char* s = ((cs_string*)argv[0].as.p)->data;
    const char* old = ((cs_string*)argv[1].as.p)->data;
    const char* rep = ((cs_string*)argv[2].as.p)->data;
    if (!*old) { *out = cs_str(vm, s); return 0; }

    size_t sl = strlen(s), ol = strlen(old), rl = strlen(rep);
    size_t count = 0;
    for (const char* p = s; (p = strstr(p, old)); p += ol) count++;
    size_t nl = sl;
    if (rl >= ol) nl += count * (rl - ol);
    else nl -= count * (ol - rl);
    char* buf = (char*)malloc(nl + 1);
    if (!buf) { cs_error(vm, "out of memory"); return 1; }

    size_t w = 0;
    const char* p = s;
    while (1) {
        const char* hit = strstr(p, old);
        if (!hit) break;
        size_t npre = (size_t)(hit - p);
        memcpy(buf + w, p, npre);
        w += npre;
        memcpy(buf + w, rep, rl);
        w += rl;
        p = hit + ol;
    }
    size_t tail = strlen(p);
    memcpy(buf + w, p, tail);
    w += tail;
    buf[w] = 0;
    *out = cs_str_take(vm, buf, (uint64_t)w);
    return 0;
}

static int nf_str_split(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { *out = cs_list(vm); return 0; }
    const char* s = ((cs_string*)argv[0].as.p)->data;
    const char* sep = ((cs_string*)argv[1].as.p)->data;
    if (!*sep) { // return [s]
        cs_value listv = cs_list(vm);
        if (!listv.as.p) { cs_error(vm, "out of memory"); return 1; }
        cs_list_obj* l = (cs_list_obj*)listv.as.p;
        if (!list_ensure(l, 1)) { cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
        l->items[l->len++] = cs_value_copy(argv[0]);
        *out = listv;
        return 0;
    }

    cs_value listv = cs_list(vm);
    if (!listv.as.p) { cs_error(vm, "out of memory"); return 1; }
    cs_list_obj* l = (cs_list_obj*)listv.as.p;

    size_t sepl = strlen(sep);
    const char* p = s;
    while (1) {
        const char* hit = strstr(p, sep);
        if (!hit) break;
        size_t n = (size_t)(hit - p);
        char* part = (char*)malloc(n + 1);
        if (!part) { cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
        memcpy(part, p, n);
        part[n] = 0;
        cs_value sv = cs_str_take(vm, part, (uint64_t)n);
        if (!list_ensure(l, l->len + 1)) { cs_value_release(sv); cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
        l->items[l->len++] = sv;
        p = hit + sepl;
    }
    cs_value tailv = cs_str(vm, p);
    if (!list_ensure(l, l->len + 1)) { cs_value_release(tailv); cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
    l->items[l->len++] = tailv;

    *out = listv;
    return 0;
}

static int nf_path_join(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { *out = cs_nil(); return 0; }
    char* joined = path_join_alloc(((cs_string*)argv[0].as.p)->data, ((cs_string*)argv[1].as.p)->data);
    if (!joined) { cs_error(vm, "out of memory"); return 1; }
    *out = cs_str_take(vm, joined, (uint64_t)-1);
    return 0;
}

static int nf_path_dirname(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }
    const char* path = ((cs_string*)argv[0].as.p)->data;
    const char* last_slash = strrchr(path, '/');
    const char* last_bslash = strrchr(path, '\\');
    const char* last = last_slash;
    if (last_bslash && (!last || last_bslash > last)) last = last_bslash;
    if (!last) { *out = cs_str(vm, "."); return 0; }
    size_t n = (size_t)(last - path);
    if (n == 0) { *out = cs_str(vm, "/"); return 0; }
    char* buf = (char*)malloc(n + 1);
    if (!buf) { cs_error(vm, "out of memory"); return 1; }
    memcpy(buf, path, n);
    buf[n] = 0;
    *out = cs_str_take(vm, buf, (uint64_t)n);
    return 0;
}

static int nf_path_basename(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }
    const char* path = ((cs_string*)argv[0].as.p)->data;
    const char* last_slash = strrchr(path, '/');
    const char* last_bslash = strrchr(path, '\\');
    const char* last = last_slash;
    if (last_bslash && (!last || last_bslash > last)) last = last_bslash;
    const char* base = last ? last + 1 : path;
    *out = cs_str(vm, base);
    return 0;
}

static int nf_path_ext(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }
    const char* path = ((cs_string*)argv[0].as.p)->data;
    const char* base = path;
    const char* last_slash = strrchr(path, '/');
    const char* last_bslash = strrchr(path, '\\');
    const char* last = last_slash;
    if (last_bslash && (!last || last_bslash > last)) last = last_bslash;
    if (last) base = last + 1;
    const char* dot = strrchr(base, '.');
    if (!dot || dot == base) { *out = cs_str(vm, ""); return 0; }
    *out = cs_str(vm, dot + 1);
    return 0;
}

static int sb_append(char** buf, size_t* len, size_t* cap, const char* s, size_t n) {
    if (!buf || !len || !cap) return 0;
    if (*len + n + 1 > *cap) {
        size_t nc = (*cap == 0) ? 64 : *cap;
        while (*len + n + 1 > nc) nc *= 2;
        char* nb = (char*)realloc(*buf, nc);
        if (!nb) return 0;
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = 0;
    return 1;
}

static int nf_fmt(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }

    const char* fmt = ((cs_string*)argv[0].as.p)->data;
    char* buf = NULL;
    size_t len = 0, cap = 0;
    int ai = 1;

    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            if (!sb_append(&buf, &len, &cap, &fmt[i], 1)) { free(buf); cs_error(vm, "out of memory"); return 1; }
            continue;
        }
        char spec = fmt[++i];
        if (!spec) break;
        if (spec == '%') {
            if (!sb_append(&buf, &len, &cap, "%", 1)) { free(buf); cs_error(vm, "out of memory"); return 1; }
            continue;
        }
        if (ai >= argc) { free(buf); cs_error(vm, "fmt: not enough arguments"); return 1; }

        char tmp[128];
        tmp[0] = 0;
        const cs_value v = argv[ai++];
        const char* s = NULL;
        size_t sn = 0;

        if (spec == 'd') {
            if (v.type != CS_T_INT) { free(buf); cs_error(vm, "fmt: %d expects int"); return 1; }
            snprintf(tmp, sizeof(tmp), "%lld", (long long)v.as.i);
            s = tmp;
        } else if (spec == 'b') {
            if (v.type != CS_T_BOOL) { free(buf); cs_error(vm, "fmt: %b expects bool"); return 1; }
            s = v.as.b ? "true" : "false";
        } else if (spec == 's') {
            if (v.type != CS_T_STR) { free(buf); cs_error(vm, "fmt: %s expects string"); return 1; }
            s = ((cs_string*)v.as.p)->data;
        } else if (spec == 'v') {
            s = value_repr(v, tmp, sizeof(tmp));
        } else {
            free(buf);
            cs_error(vm, "fmt: unknown format specifier");
            return 1;
        }

        sn = strlen(s);
        if (!sb_append(&buf, &len, &cap, s, sn)) { free(buf); cs_error(vm, "out of memory"); return 1; }
    }

    if (!buf) buf = cs_strdup2_local("");
    if (!buf) { cs_error(vm, "out of memory"); return 1; }
    *out = cs_str_take(vm, buf, (uint64_t)len);
    return 0;
}

static int nf_now_ms(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
#if !defined(_WIN32)
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) { *out = cs_int(0); return 0; }
    int64_t ms = (int64_t)tv.tv_sec * 1000 + (int64_t)(tv.tv_usec / 1000);
    *out = cs_int(ms);
    return 0;
#else
    *out = cs_int((int64_t)(clock() * 1000 / (int64_t)CLOCKS_PER_SEC));
    return 0;
#endif
}

static int nf_sleep(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_nil();
    if (argc != 1 || argv[0].type != CS_T_INT) return 0;
    int64_t ms = argv[0].as.i;
    if (ms <= 0) return 0;
#if !defined(_WIN32)
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    (void)nanosleep(&ts, NULL);
    return 0;
#else
    (void)vm;
    // No portable C99 sleep for Windows here; host should provide its own.
    cs_error(vm, "sleep not supported on this platform in stdlib");
    return 1;
#endif
}

void cs_register_stdlib(cs_vm* vm) {
    cs_register_native(vm, "print",  nf_print,  NULL);
    cs_register_native(vm, "typeof", nf_typeof, NULL);
    cs_register_native(vm, "getenv", nf_getenv, NULL);
    cs_register_native(vm, "assert", nf_assert, NULL);
    cs_register_native(vm, "load",   nf_load,   NULL);
    cs_register_native(vm, "require", nf_require, NULL);
    cs_register_native(vm, "list",   nf_list,   NULL);
    cs_register_native(vm, "map",    nf_map,    NULL);
    cs_register_native(vm, "strbuf", nf_strbuf, NULL);
    cs_register_native(vm, "len",    nf_len,    NULL);
    cs_register_native(vm, "push",   nf_push,   NULL);
    cs_register_native(vm, "pop",    nf_pop,    NULL);
    cs_register_native(vm, "mget",   nf_mget,   NULL);
    cs_register_native(vm, "mset",   nf_mset,   NULL);
    cs_register_native(vm, "mhas",   nf_mhas,   NULL);
    cs_register_native(vm, "keys",   nf_keys,   NULL);
    cs_register_native(vm, "str_find",    nf_str_find,    NULL);
    cs_register_native(vm, "str_replace", nf_str_replace, NULL);
    cs_register_native(vm, "str_split",   nf_str_split,   NULL);
    cs_register_native(vm, "path_join",   nf_path_join,   NULL);
    cs_register_native(vm, "path_dirname", nf_path_dirname, NULL);
    cs_register_native(vm, "path_basename", nf_path_basename, NULL);
    cs_register_native(vm, "path_ext",    nf_path_ext,    NULL);
    cs_register_native(vm, "fmt",         nf_fmt,         NULL);
    cs_register_native(vm, "now_ms",      nf_now_ms,      NULL);
    cs_register_native(vm, "sleep",       nf_sleep,       NULL);
}
