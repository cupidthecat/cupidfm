#include "cs_vm.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char* cs_strdup2(const char* s) {
    size_t n = strlen(s ? s : "");
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static void vm_append_stacktrace(cs_vm* vm, char** io_msg) {
    if (!vm || !io_msg || !*io_msg) return;
    if (vm->frame_count == 0) return;

    size_t base_len = strlen(*io_msg);
    size_t extra = 32;
    for (size_t i = 0; i < vm->frame_count; i++) {
        const cs_frame* f = &vm->frames[vm->frame_count - 1 - i];
        extra += strlen(f->func ? f->func : "<call>") + strlen(f->source ? f->source : "<input>") + 64;
    }

    char* out = (char*)realloc(*io_msg, base_len + extra + 1);
    if (!out) return;
    *io_msg = out;

    size_t w = base_len;
    w += (size_t)snprintf(out + w, base_len + extra + 1 - w, "\nStack trace:");
    for (size_t i = 0; i < vm->frame_count; i++) {
        const cs_frame* f = &vm->frames[vm->frame_count - 1 - i];
        const char* fn = f->func ? f->func : "<call>";
        const char* src = f->source ? f->source : "<input>";
        if (f->line > 0) w += (size_t)snprintf(out + w, base_len + extra + 1 - w, "\n  at %s (%s:%d:%d)", fn, src, f->line, f->col);
        else w += (size_t)snprintf(out + w, base_len + extra + 1 - w, "\n  at %s (%s)", fn, src);
    }
    out[w] = 0;
}

static void vm_frames_push(cs_vm* vm, const char* func, const char* source, int line, int col) {
    if (!vm) return;
    if (vm->frame_count == vm->frame_cap) {
        size_t nc = vm->frame_cap ? vm->frame_cap * 2 : 16;
        cs_frame* nf = (cs_frame*)realloc(vm->frames, nc * sizeof(cs_frame));
        if (!nf) return;
        vm->frames = nf;
        vm->frame_cap = nc;
    }
    vm->frames[vm->frame_count++] = (cs_frame){ func, source, line, col };
}

static void vm_frames_pop(cs_vm* vm) {
    if (!vm || vm->frame_count == 0) return;
    vm->frame_count--;
}

static const char* vm_intern_source(cs_vm* vm, const char* name) {
    if (!vm) return "<input>";
    const char* n = name ? name : "<input>";
    for (size_t i = 0; i < vm->source_count; i++) {
        if (strcmp(vm->sources[i], n) == 0) return vm->sources[i];
    }
    if (vm->source_count == vm->source_cap) {
        size_t nc = vm->source_cap ? vm->source_cap * 2 : 16;
        char** ns = (char**)realloc(vm->sources, nc * sizeof(char*));
        if (!ns) return "<input>";
        vm->sources = ns;
        vm->source_cap = nc;
    }
    char* dup = cs_strdup2(n);
    if (!dup) return "<input>";
    vm->sources[vm->source_count++] = dup;
    return dup;
}

static char* path_dirname_alloc(const char* path) {
    if (!path || !*path) return cs_strdup2(".");
    const char* last_slash = strrchr(path, '/');
    const char* last_bslash = strrchr(path, '\\');
    const char* last = last_slash;
    if (last_bslash && (!last || last_bslash > last)) last = last_bslash;
    if (!last) return cs_strdup2(".");
    size_t n = (size_t)(last - path);
    if (n == 0) return cs_strdup2("/");
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, path, n);
    out[n] = 0;
    return out;
}

static void vm_dir_push_owned(cs_vm* vm, char* dir) {
    if (!vm || !dir) { free(dir); return; }
    if (vm->dir_count == vm->dir_cap) {
        size_t nc = vm->dir_cap ? vm->dir_cap * 2 : 16;
        char** nd = (char**)realloc(vm->dir_stack, nc * sizeof(char*));
        if (!nd) { free(dir); return; }
        vm->dir_stack = nd;
        vm->dir_cap = nc;
    }
    vm->dir_stack[vm->dir_count++] = dir;
}

static void vm_dir_pop(cs_vm* vm) {
    if (!vm || vm->dir_count == 0) return;
    free(vm->dir_stack[--vm->dir_count]);
}

static int env_find(cs_env* e, const char* key);

static int env_get_nocopy(cs_env* e, const char* key, cs_value* out) {
    for (cs_env* cur = e; cur; cur = cur->parent) {
        int idx = env_find(cur, key);
        if (idx >= 0) { *out = cur->vals[idx]; return 1; }
    }
    return 0;
}

static int getfield_dotted_name(ast* e, char* buf, size_t cap) {
    // Build "a.b.c" for nested GETFIELD ending in IDENT, else return 0.
    if (!e || !buf || cap == 0) return 0;
    buf[0] = 0;

    // collect field names reversed
    const char* parts[32];
    size_t count = 0;
    ast* cur = e;
    while (cur && cur->type == N_GETFIELD && count < 31) {
        parts[count++] = cur->as.getfield.field;
        cur = cur->as.getfield.target;
    }
    if (!cur || cur->type != N_IDENT) return 0;
    parts[count++] = cur->as.ident.name;

    // write reversed: IDENT then fields
    size_t w = 0;
    for (size_t i = 0; i < count; i++) {
        const char* p = parts[count - 1 - i];
        size_t n = strlen(p ? p : "");
        if (n == 0) return 0;
        if (i > 0) {
            if (w + 1 >= cap) return 0;
            buf[w++] = '.';
        }
        if (w + n >= cap) return 0;
        memcpy(buf + w, p, n);
        w += n;
    }
    buf[w] = 0;
    return 1;
}

// Exposed error helpers
const char* cs_vm_last_error(cs_vm* vm) {
    if (!vm || !vm->last_error) return "";
    return vm->last_error;
}

void cs_error(cs_vm* vm, const char* msg) {
    if (!vm) return;
    free(vm->last_error);
    vm->last_error = cs_strdup2(msg ? msg : "error");
    vm_append_stacktrace(vm, &vm->last_error);
}

// ---------- value helpers ----------
cs_value cs_nil(void) { cs_value v; v.type = CS_T_NIL; v.as.p = NULL; return v; }
cs_value cs_bool(int x){ cs_value v; v.type=CS_T_BOOL; v.as.b = !!x; return v; }
cs_value cs_int(int64_t x){ cs_value v; v.type=CS_T_INT; v.as.i = x; return v; }

cs_type cs_typeof(cs_value v) { return v.type; }
const char* cs_type_name(cs_type t){ return cs_type_name_impl(t); }

static cs_native* as_native(cs_value v){ return (cs_native*)v.as.p; }
static cs_string* as_str(cs_value v){ return (cs_string*)v.as.p; }
static cs_list_obj* as_list(cs_value v){ return (cs_list_obj*)v.as.p; }
static cs_map_obj*  as_map(cs_value v){ return (cs_map_obj*)v.as.p; }
static cs_strbuf_obj* as_strbuf(cs_value v){ return (cs_strbuf_obj*)v.as.p; }
static struct cs_func* as_func(cs_value v){ return (struct cs_func*)v.as.p; }

static void env_incref(cs_env* e);
static void env_decref(cs_env* e);

const char* cs_to_cstr(cs_value v) {
    if (v.type == CS_T_STR) return as_str(v)->data;
    return NULL;
}

cs_value cs_str(cs_vm* vm, const char* s) {
    (void)vm;
    cs_string* st = cs_str_new(s ? s : "");
    cs_value v; v.type = CS_T_STR; v.as.p = st;
    return v;
}

cs_value cs_str_take(cs_vm* vm, char* owned, uint64_t len) {
    (void)vm;
    cs_string* st = cs_str_new_take(owned, (size_t)len);
    cs_value v; v.type = CS_T_STR; v.as.p = st;
    return v;
}

static cs_list_obj* list_new(void) {
    cs_list_obj* l = (cs_list_obj*)calloc(1, sizeof(cs_list_obj));
    if (!l) return NULL;
    l->ref = 1;
    l->cap = 8;
    l->items = (cs_value*)calloc(l->cap, sizeof(cs_value));
    if (!l->items) { free(l); return NULL; }
    l->len = 0;
    return l;
}

static void list_incref(cs_list_obj* l) { if (l) l->ref++; }

static void list_decref(cs_list_obj* l) {
    if (!l) return;
    if (--l->ref > 0) return;
    for (size_t i = 0; i < l->len; i++) cs_value_release(l->items[i]);
    free(l->items);
    free(l);
}

static cs_map_obj* map_new(void) {
    cs_map_obj* m = (cs_map_obj*)calloc(1, sizeof(cs_map_obj));
    if (!m) return NULL;
    m->ref = 1;
    m->cap = 8;
    m->entries = (cs_map_entry*)calloc(m->cap, sizeof(cs_map_entry));
    if (!m->entries) { free(m); return NULL; }
    m->len = 0;
    return m;
}

static cs_strbuf_obj* strbuf_new(void) {
    cs_strbuf_obj* b = (cs_strbuf_obj*)calloc(1, sizeof(cs_strbuf_obj));
    if (!b) return NULL;
    b->ref = 1;
    b->cap = 256;
    b->data = (char*)malloc(b->cap + 1);
    if (!b->data) { free(b); return NULL; }
    b->len = 0;
    b->data[0] = 0;
    return b;
}

static void strbuf_incref(cs_strbuf_obj* b) { if (b) b->ref++; }

static void strbuf_decref(cs_strbuf_obj* b) {
    if (!b) return;
    if (--b->ref > 0) return;
    free(b->data);
    free(b);
}

static void map_incref(cs_map_obj* m) { if (m) m->ref++; }

static void map_decref(cs_map_obj* m) {
    if (!m) return;
    if (--m->ref > 0) return;
    for (size_t i = 0; i < m->len; i++) {
        cs_str_decref(m->entries[i].key);
        cs_value_release(m->entries[i].val);
    }
    free(m->entries);
    free(m);
}

cs_value cs_list(cs_vm* vm) {
    (void)vm;
    cs_list_obj* l = list_new();
    cs_value v; v.type = CS_T_LIST; v.as.p = l;
    return v;
}

cs_value cs_map(cs_vm* vm) {
    (void)vm;
    cs_map_obj* m = map_new();
    cs_value v; v.type = CS_T_MAP; v.as.p = m;
    return v;
}

cs_value cs_strbuf(cs_vm* vm) {
    (void)vm;
    cs_strbuf_obj* b = strbuf_new();
    cs_value v; v.type = CS_T_STRBUF; v.as.p = b;
    return v;
}

cs_value cs_value_copy(cs_value v) {
    if (v.type == CS_T_STR) cs_str_incref(as_str(v));
    else if (v.type == CS_T_LIST) list_incref(as_list(v));
    else if (v.type == CS_T_MAP) map_incref(as_map(v));
    else if (v.type == CS_T_STRBUF) strbuf_incref(as_strbuf(v));
    else if (v.type == CS_T_NATIVE) as_native(v)->ref++;
    else if (v.type == CS_T_FUNC) as_func(v)->ref++;
    return v;
}

void cs_value_release(cs_value v) {
    if (v.type == CS_T_STR) cs_str_decref(as_str(v));
    else if (v.type == CS_T_LIST) list_decref(as_list(v));
    else if (v.type == CS_T_MAP) map_decref(as_map(v));
    else if (v.type == CS_T_STRBUF) strbuf_decref(as_strbuf(v));
    else if (v.type == CS_T_NATIVE) {
        cs_native* nf = as_native(v);
        if (nf && --nf->ref <= 0) free(nf);
    } else if (v.type == CS_T_FUNC) {
        struct cs_func* f = as_func(v);
        if (f && --f->ref <= 0) {
            env_decref(f->closure);
            free(f);
        }
    }
}

// ---------- env ----------
static void env_incref(cs_env* e) {
    if (e) e->ref++;
}

static void env_decref(cs_env* e) {
    if (!e) return;
    if (--e->ref > 0) return;
    cs_env* parent = e->parent;
    for (size_t i = 0; i < e->count; i++) {
        free(e->keys[i]);
        cs_value_release(e->vals[i]);
    }
    free(e->keys);
    free(e->vals);
    free(e);
    env_decref(parent);
}

static cs_env* env_new(cs_env* parent) {
    cs_env* e = (cs_env*)calloc(1, sizeof(cs_env));
    if (!e) return NULL;
    e->ref = 1;
    e->parent = parent;
    env_incref(parent);
    e->cap = 16;
    e->keys = (char**)calloc(e->cap, sizeof(char*));
    e->vals = (cs_value*)calloc(e->cap, sizeof(cs_value));
    return e;
}

static int env_find(cs_env* e, const char* key) {
    for (size_t i = 0; i < e->count; i++) {
        if (strcmp(e->keys[i], key) == 0) return (int)i;
    }
    return -1;
}

static void env_set_here(cs_env* e, const char* key, cs_value v) {
    int idx = env_find(e, key);
    if (idx >= 0) {
        cs_value_release(e->vals[idx]);
        e->vals[idx] = cs_value_copy(v);
        return;
    }
    if (e->count == e->cap) {
        e->cap *= 2;
        e->keys = (char**)realloc(e->keys, e->cap * sizeof(char*));
        e->vals = (cs_value*)realloc(e->vals, e->cap * sizeof(cs_value));
    }
    e->keys[e->count] = cs_strdup2(key);
    e->vals[e->count] = cs_value_copy(v);
    e->count++;
}

static int env_set(cs_env* e, const char* key, cs_value v) {
    // assign walks upward, but will set in nearest existing scope; otherwise in current scope
    for (cs_env* cur = e; cur; cur = cur->parent) {
        int idx = env_find(cur, key);
        if (idx >= 0) {
            cs_value_release(cur->vals[idx]);
            cur->vals[idx] = cs_value_copy(v);
            return 1;
        }
    }
    env_set_here(e, key, v);
    return 1;
}

static int env_get(cs_env* e, const char* key, cs_value* out) {
    for (cs_env* cur = e; cur; cur = cur->parent) {
        int idx = env_find(cur, key);
        if (idx >= 0) { *out = cs_value_copy(cur->vals[idx]); return 1; }
    }
    return 0;
}

static cs_value* env_get_slot(cs_env* e, const char* key) {
    for (cs_env* cur = e; cur; cur = cur->parent) {
        int idx = env_find(cur, key);
        if (idx >= 0) return &cur->vals[idx];
    }
    return NULL;
}

static int is_truthy(cs_value v) {
    switch (v.type) {
        case CS_T_NIL: return 0;
        case CS_T_BOOL: return v.as.b != 0;
        default: return 1;
    }
}

// ---------- list/map helpers ----------
static int list_ensure(cs_list_obj* l, size_t need) {
    if (!l) return 0;
    if (need <= l->cap) return 1;
    size_t nc = l->cap ? l->cap : 8;
    while (nc < need) nc *= 2;
    cs_value* ni = (cs_value*)realloc(l->items, nc * sizeof(cs_value));
    if (!ni) return 0;
    // zero-init new tail
    for (size_t i = l->cap; i < nc; i++) ni[i] = cs_nil();
    l->items = ni;
    l->cap = nc;
    return 1;
}

static cs_value list_get(cs_list_obj* l, int64_t idx) {
    if (!l) return cs_nil();
    if (idx < 0) return cs_nil();
    if ((size_t)idx >= l->len) return cs_nil();
    return cs_value_copy(l->items[(size_t)idx]);
}

static int list_set(cs_list_obj* l, int64_t idx, cs_value v) {
    if (!l) return 0;
    if (idx < 0) return 0;
    size_t u = (size_t)idx;
    if (!list_ensure(l, u + 1)) return 0;
    if (u >= l->len) {
        for (size_t i = l->len; i < u; i++) l->items[i] = cs_nil();
        l->len = u + 1;
        l->items[u] = cs_value_copy(v);
        return 1;
    }
    cs_value_release(l->items[u]);
    l->items[u] = cs_value_copy(v);
    return 1;
}

static int list_push(cs_list_obj* l, cs_value v) {
    if (!l) return 0;
    if (!list_ensure(l, l->len + 1)) return 0;
    l->items[l->len++] = cs_value_copy(v);
    return 1;
}

static cs_value list_pop(cs_list_obj* l) {
    if (!l || l->len == 0) return cs_nil();
    cs_value out = l->items[l->len - 1];
    l->items[l->len - 1] = cs_nil();
    l->len--;
    return out;
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
    for (size_t i = m->cap; i < nc; i++) {
        ne[i].key = NULL;
        ne[i].val = cs_nil();
    }
    m->entries = ne;
    m->cap = nc;
    return 1;
}

static cs_value map_get(cs_map_obj* m, const char* key) {
    int idx = map_find(m, key);
    if (idx < 0) return cs_nil();
    return cs_value_copy(m->entries[(size_t)idx].val);
}

static int map_set(cs_map_obj* m, cs_string* key, cs_value v) {
    if (!m || !key) return 0;
    int idx = map_find(m, key->data);
    if (idx >= 0) {
        cs_value_release(m->entries[(size_t)idx].val);
        m->entries[(size_t)idx].val = cs_value_copy(v);
        return 1;
    }
    if (!map_ensure(m, m->len + 1)) return 0;
    cs_str_incref(key);
    m->entries[m->len].key = key;
    m->entries[m->len].val = cs_value_copy(v);
    m->len++;
    return 1;
}

// ---------- string helpers ----------
static int str_append_inplace(cs_string* s, const char* add, size_t add_len) {
    if (!s || !add) return 0;
    size_t new_len = s->len + add_len;
    char* buf = s->data;
    if (new_len > s->cap) {
        size_t nc = s->cap ? s->cap : 16;
        while (nc < new_len) nc *= 2;
        buf = (char*)realloc(s->data, nc + 1);
        if (!buf) return 0;
        s->data = buf;
        s->cap = nc;
    }
    memcpy(buf + s->len, add, add_len);
    buf[new_len] = 0;
    s->len = new_len;
    return 1;
}

static int strbuf_reserve(cs_strbuf_obj* b, size_t need) {
    if (!b) return 0;
    if (need <= b->cap) return 1;
    size_t nc = b->cap ? b->cap : 256;
    while (nc < need) nc *= 2;
    char* nb = (char*)realloc(b->data, nc + 1);
    if (!nb) return 0;
    b->data = nb;
    b->cap = nc;
    return 1;
}

static int strbuf_append_bytes(cs_strbuf_obj* b, const char* s, size_t n) {
    if (!b || !s) return 0;
    if (!strbuf_reserve(b, b->len + n)) return 0;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
    return 1;
}

static int strbuf_append_int(cs_strbuf_obj* b, int64_t v) {
    char tmp[64];
    int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
    if (n <= 0) return 0;
    return strbuf_append_bytes(b, tmp, (size_t)n);
}

// ---------- error ----------
static void vm_set_err(cs_vm* vm, const char* msg, const char* source, int line, int col) {
    free(vm->last_error);
    char buf[512];
    const char* src = source ? source : "<input>";
    snprintf(buf, sizeof(buf), "Runtime error at %s:%d:%d: %s", src, line, col, msg);
    vm->last_error = cs_strdup2(buf);
    vm_append_stacktrace(vm, &vm->last_error);
}

const char* cs_last_error(const cs_vm* vm) {
    return vm ? vm->last_error : NULL;
}

// ---------- string unescape ----------
static char* unescape_string_token(const char* tok, size_t n) {
    // tok includes quotes; minimal unescape
    if (n < 2 || tok[0] != '"' || tok[n-1] != '"') return cs_strdup2("");
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    size_t w = 0;
    for (size_t i = 1; i + 1 < n; i++) {
        char c = tok[i];
        if (c == '\\' && i + 1 < n - 1) {
            char x = tok[++i];
            switch (x) {
                case 'n': out[w++] = '\n'; break;
                case 't': out[w++] = '\t'; break;
                case 'r': out[w++] = '\r'; break;
                case '"': out[w++] = '"'; break;
                case '\\': out[w++] = '\\'; break;
                default: out[w++] = x; break;
            }
        } else {
            out[w++] = c;
        }
    }
    out[w] = 0;
    return out;
}

// ---------- eval ----------
typedef struct {
    int did_return;
    cs_value ret;
    int ok;
} exec_result;

static exec_result exec_stmt(cs_vm* vm, cs_env* env, ast* s);
static exec_result exec_block(cs_vm* vm, cs_env* env, ast* b);
static cs_value eval_expr(cs_vm* vm, cs_env* env, ast* e, int* ok);

static cs_value eval_binop(cs_vm* vm, ast* e, cs_value a, cs_value b, int* ok) {
    int op = e->as.binop.op;

    // short-circuit done in caller for && ||
    if (op == TK_PLUS) {
        if (a.type == CS_T_INT && b.type == CS_T_INT) return cs_int(a.as.i + b.as.i);
        if (a.type == CS_T_STR || b.type == CS_T_STR) {
            const char* sa = (a.type == CS_T_STR) ? as_str(a)->data : NULL;
            const char* sb = (b.type == CS_T_STR) ? as_str(b)->data : NULL;

            char bufA[64], bufB[64];
            if (!sa) { snprintf(bufA, sizeof(bufA), "%lld", (long long)a.as.i); sa = bufA; }
            if (!sb) { snprintf(bufB, sizeof(bufB), "%lld", (long long)b.as.i); sb = bufB; }

            size_t na = strlen(sa), nb = strlen(sb);
            char* joined = (char*)malloc(na + nb + 1);
            if (!joined) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
            memcpy(joined, sa, na);
            memcpy(joined + na, sb, nb + 1);
            return cs_str_take(vm, joined, (uint64_t)(na + nb));
        }
        vm_set_err(vm, "type error: '+' expects int or string", e->source_name, e->line, e->col);
        *ok = 0; return cs_nil();
    }

    if (op == TK_MINUS || op == TK_STAR || op == TK_SLASH || op == TK_PERCENT) {
        if (a.type != CS_T_INT || b.type != CS_T_INT) {
            vm_set_err(vm, "type error: arithmetic expects int", e->source_name, e->line, e->col);
            *ok = 0; return cs_nil();
        }
        if (op == TK_MINUS) return cs_int(a.as.i - b.as.i);
        if (op == TK_STAR) return cs_int(a.as.i * b.as.i);
        if (op == TK_SLASH) {
            if (b.as.i == 0) { vm_set_err(vm, "division by zero", e->source_name, e->line, e->col); *ok=0; return cs_nil(); }
            return cs_int(a.as.i / b.as.i);
        }
        if (op == TK_PERCENT) {
            if (b.as.i == 0) { vm_set_err(vm, "mod by zero", e->source_name, e->line, e->col); *ok=0; return cs_nil(); }
            return cs_int(a.as.i % b.as.i);
        }
    }

    if (op == TK_EQ || op == TK_NE) {
        int eq = 0;
        if (a.type != b.type) eq = 0;
        else if (a.type == CS_T_NIL) eq = 1;
        else if (a.type == CS_T_BOOL) eq = (a.as.b == b.as.b);
        else if (a.type == CS_T_INT) eq = (a.as.i == b.as.i);
        else if (a.type == CS_T_STR) eq = (strcmp(as_str(a)->data, as_str(b)->data) == 0);
        else eq = (a.as.p == b.as.p);

        return cs_bool((op == TK_EQ) ? eq : !eq);
    }

    if (op == TK_LT || op == TK_LE || op == TK_GT || op == TK_GE) {
        if (a.type == CS_T_INT && b.type == CS_T_INT) {
            int r = 0;
            if (op == TK_LT) r = (a.as.i < b.as.i);
            if (op == TK_LE) r = (a.as.i <= b.as.i);
            if (op == TK_GT) r = (a.as.i > b.as.i);
            if (op == TK_GE) r = (a.as.i >= b.as.i);
            return cs_bool(r);
        }
        if (a.type == CS_T_STR && b.type == CS_T_STR) {
            int c = strcmp(as_str(a)->data, as_str(b)->data);
            int r = 0;
            if (op == TK_LT) r = (c < 0);
            if (op == TK_LE) r = (c <= 0);
            if (op == TK_GT) r = (c > 0);
            if (op == TK_GE) r = (c >= 0);
            return cs_bool(r);
        }
        vm_set_err(vm, "type error: comparisons require both ints or both strings", e->source_name, e->line, e->col);
        *ok = 0; return cs_nil();
    }

    vm_set_err(vm, "unknown binary operator", e->source_name, e->line, e->col);
    *ok = 0; return cs_nil();
}

static cs_value eval_expr(cs_vm* vm, cs_env* env, ast* e, int* ok) {
    if (!e) return cs_nil();

    switch (e->type) {
        case N_LIT_INT: return cs_int((int64_t)e->as.lit_int.v);
        case N_LIT_BOOL: return cs_bool(e->as.lit_bool.v);
        case N_LIT_NIL: return cs_nil();

        case N_LIT_STR: {
            size_t n = strlen(e->as.lit_str.s);
            char* un = unescape_string_token(e->as.lit_str.s, n);
            if (!un) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
            return cs_str_take(vm, un, (uint64_t)-1);
        }

        case N_FUNCLIT: {
            struct cs_func* f = (struct cs_func*)calloc(1, sizeof(struct cs_func));
            if (!f) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
            f->ref = 1;
            f->name = NULL;
            f->def_source = e->source_name;
            f->def_line = e->line;
            f->def_col = e->col;
            f->params = e->as.funclit.params;
            f->param_count = e->as.funclit.param_count;
            f->body = e->as.funclit.body;
            f->closure = env;
            env_incref(env);

            cs_value fv; fv.type = CS_T_FUNC; fv.as.p = f;
            return fv;
        }

        case N_IDENT: {
            cs_value v;
            if (!env_get(env, e->as.ident.name, &v)) {
                vm_set_err(vm, "undefined variable", e->source_name, e->line, e->col);
                *ok = 0; return cs_nil();
            }
            return v;
        }

        case N_UNOP: {
            cs_value x = eval_expr(vm, env, e->as.unop.expr, ok);
            if (!*ok) return cs_nil();
            if (e->as.unop.op == TK_BANG) { cs_value out = cs_bool(!is_truthy(x)); cs_value_release(x); return out; }
            if (e->as.unop.op == TK_MINUS) {
                if (x.type != CS_T_INT) { cs_value_release(x); vm_set_err(vm, "type error: unary '-' expects int", e->source_name, e->line, e->col); *ok=0; return cs_nil(); }
                cs_value out = cs_int(-x.as.i);
                cs_value_release(x);
                return out;
            }
            cs_value_release(x);
            vm_set_err(vm, "unknown unary operator", e->source_name, e->line, e->col);
            *ok=0; return cs_nil();
        }

        case N_BINOP: {
            int op = e->as.binop.op;

            // short-circuit
            if (op == TK_ANDAND) {
                cs_value left = eval_expr(vm, env, e->as.binop.left, ok);
                if (!*ok) return cs_nil();
                int lt = is_truthy(left);
                cs_value_release(left);
                if (!lt) return cs_bool(0);
                cs_value right = eval_expr(vm, env, e->as.binop.right, ok);
                if (!*ok) return cs_nil();
                int rt = is_truthy(right);
                cs_value_release(right);
                return cs_bool(rt);
            }
            if (op == TK_OROR) {
                cs_value left = eval_expr(vm, env, e->as.binop.left, ok);
                if (!*ok) return cs_nil();
                int lt = is_truthy(left);
                cs_value_release(left);
                if (lt) return cs_bool(1);
                cs_value right = eval_expr(vm, env, e->as.binop.right, ok);
                if (!*ok) return cs_nil();
                int rt = is_truthy(right);
                cs_value_release(right);
                return cs_bool(rt);
            }

            cs_value a = eval_expr(vm, env, e->as.binop.left, ok);
            if (!*ok) return cs_nil();
            cs_value b = eval_expr(vm, env, e->as.binop.right, ok);
            if (!*ok) { cs_value_release(a); return cs_nil(); }

            cs_value out = eval_binop(vm, e, a, b, ok);
            cs_value_release(a);
            cs_value_release(b);
            return out;
        }

        case N_INDEX: {
            cs_value target = eval_expr(vm, env, e->as.index.target, ok);
            if (!*ok) return cs_nil();
            cs_value index = eval_expr(vm, env, e->as.index.index, ok);
            if (!*ok) { cs_value_release(target); return cs_nil(); }

            cs_value out = cs_nil();
            if (target.type == CS_T_LIST && index.type == CS_T_INT) {
                out = list_get(as_list(target), index.as.i);
            } else if (target.type == CS_T_MAP && index.type == CS_T_STR) {
                out = map_get(as_map(target), as_str(index)->data);
            } else {
                vm_set_err(vm, "indexing expects list[int] or map[string]", e->source_name, e->line, e->col);
                *ok = 0;
            }

            cs_value_release(target);
            cs_value_release(index);
            return out;
        }

        case N_GETFIELD: {
            // If this is something like fm.status and fm isn't defined as a value, fall back to dotted globals.
            if (e->as.getfield.target && e->as.getfield.target->type == N_IDENT) {
                cs_value base;
                if (!env_get_nocopy(env, e->as.getfield.target->as.ident.name, &base)) {
                    char name[256];
                    if (getfield_dotted_name(e, name, sizeof(name))) {
                        cs_value v;
                        if (env_get(env, name, &v)) return v;
                    }
                    vm_set_err(vm, "undefined variable", e->source_name, e->line, e->col);
                    *ok = 0;
                    return cs_nil();
                }
            }

            cs_value target = eval_expr(vm, env, e->as.getfield.target, ok);
            if (!*ok) return cs_nil();

            cs_value out = cs_nil();
            if (target.type == CS_T_MAP) {
                out = map_get(as_map(target), e->as.getfield.field);
            } else {
                vm_set_err(vm, "field access expects map", e->source_name, e->line, e->col);
                *ok = 0;
            }
            cs_value_release(target);
            return out;
        }

        case N_CALL: {
            const char* call_name = NULL;
            if (e->as.call.callee && e->as.call.callee->type == N_IDENT) call_name = e->as.call.callee->as.ident.name;

            // Method call support: `obj.method(a,b)` where callee is GETFIELD.
            if (e->as.call.callee && e->as.call.callee->type == N_GETFIELD) {
                ast* gf = e->as.call.callee;
                const char* field = gf->as.getfield.field;

                // Back-compat: if `fm` isn't defined, try calling global "fm.status" etc.
                if (gf->as.getfield.target && gf->as.getfield.target->type == N_IDENT) {
                    cs_value dummy;
                    if (!env_get_nocopy(env, gf->as.getfield.target->as.ident.name, &dummy)) {
                        char name[256];
                        if (getfield_dotted_name(gf, name, sizeof(name))) {
                            cs_value f;
                            if (env_get(env, name, &f)) {
                                int argc = (int)e->as.call.argc;
                                cs_value* argv = NULL;
                                if (argc > 0) {
                                    argv = (cs_value*)calloc((size_t)argc, sizeof(cs_value));
                                    if (!argv) { cs_value_release(f); vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
                                    for (int i = 0; i < argc; i++) {
                                        argv[i] = eval_expr(vm, env, e->as.call.args[i], ok);
                                        if (!*ok) break;
                                    }
                                }
                                cs_value out = cs_nil();
                                if (*ok) {
                                    if (f.type == CS_T_NATIVE) {
                                        cs_native* nf = as_native(f);
                                        vm_frames_push(vm, name, e->source_name, e->line, e->col);
                                        if (nf->fn(vm, nf->userdata, argc, argv, &out) != 0) {
                                            if (!vm->last_error) vm_set_err(vm, "native call failed", e->source_name, e->line, e->col);
                                            *ok = 0;
                                        }
                                        vm_frames_pop(vm);
                                    } else if (f.type == CS_T_FUNC) {
                                        struct cs_func* fn = as_func(f);
                                        cs_env* callenv = env_new(fn->closure);
                                        if (!callenv) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }
                                        if (*ok) {
                                            if ((size_t)argc != fn->param_count) {
                                                vm_set_err(vm, "wrong argument count", e->source_name, e->line, e->col);
                                                *ok = 0;
                                            } else {
                                                vm_frames_push(vm, name, e->source_name, e->line, e->col);
                                                for (size_t i = 0; i < fn->param_count; i++) env_set_here(callenv, fn->params[i], argv[i]);
                                                exec_result r = exec_block(vm, callenv, fn->body);
                                                if (!r.ok) *ok = 0;
                                                if (r.did_return) out = cs_value_copy(r.ret);
                                                cs_value_release(r.ret);
                                                vm_frames_pop(vm);
                                            }
                                        }
                                        env_decref(callenv);
                                    } else {
                                        vm_set_err(vm, "attempted to call non-function", e->source_name, e->line, e->col);
                                        *ok = 0;
                                    }
                                }
                                if (argv) { for (int i = 0; i < argc; i++) cs_value_release(argv[i]); free(argv); }
                                cs_value_release(f);
                                return out;
                            }
                        }
                        vm_set_err(vm, "undefined variable", e->source_name, e->line, e->col);
                        *ok = 0;
                        return cs_nil();
                    }
                }

                cs_value self = eval_expr(vm, env, gf->as.getfield.target, ok);
                if (!*ok) return cs_nil();

                // Build argv for method: (self, ...args)
                int argc0 = (int)e->as.call.argc;
                cs_value* argv0 = NULL;
                if (argc0 > 0) {
                    argv0 = (cs_value*)calloc((size_t)argc0, sizeof(cs_value));
                    if (!argv0) { cs_value_release(self); vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
                    for (int i = 0; i < argc0; i++) {
                        argv0[i] = eval_expr(vm, env, e->as.call.args[i], ok);
                        if (!*ok) break;
                    }
                }

                cs_value out = cs_nil();
                if (*ok) {
                    if (self.type == CS_T_STRBUF) {
                        cs_strbuf_obj* b = as_strbuf(self);
                        if (strcmp(field, "append") == 0) {
                            if (argc0 != 1) {
                                vm_set_err(vm, "strbuf.append expects 1 argument", e->source_name, e->line, e->col);
                                *ok = 0;
                            } else {
                                cs_value v = argv0[0];
                                int ok2 = 1;
                                if (v.type == CS_T_STR) ok2 = strbuf_append_bytes(b, as_str(v)->data, as_str(v)->len);
                                else if (v.type == CS_T_INT) ok2 = strbuf_append_int(b, v.as.i);
                                else if (v.type == CS_T_BOOL) ok2 = strbuf_append_bytes(b, v.as.b ? "true" : "false", v.as.b ? 4 : 5);
                                else if (v.type == CS_T_NIL) ok2 = strbuf_append_bytes(b, "nil", 3);
                                else ok2 = 0;
                                if (!ok2) { vm_set_err(vm, "strbuf.append failed", e->source_name, e->line, e->col); *ok = 0; }
                            }
                        } else if (strcmp(field, "str") == 0) {
                            if (argc0 != 0) {
                                vm_set_err(vm, "strbuf.str expects 0 arguments", e->source_name, e->line, e->col);
                                *ok = 0;
                            } else {
                                char* copy = (char*)malloc(b->len + 1);
                                if (!copy) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }
                                else {
                                    memcpy(copy, b->data, b->len + 1);
                                    out = cs_str_take(vm, copy, (uint64_t)b->len);
                                }
                            }
                        } else if (strcmp(field, "clear") == 0) {
                            if (argc0 != 0) {
                                vm_set_err(vm, "strbuf.clear expects 0 arguments", e->source_name, e->line, e->col);
                                *ok = 0;
                            } else {
                                b->len = 0;
                                if (b->data) b->data[0] = 0;
                            }
                        } else if (strcmp(field, "len") == 0) {
                            if (argc0 != 0) {
                                vm_set_err(vm, "strbuf.len expects 0 arguments", e->source_name, e->line, e->col);
                                *ok = 0;
                            } else {
                                out = cs_int((int64_t)b->len);
                            }
                        } else {
                            vm_set_err(vm, "unknown strbuf method", e->source_name, e->line, e->col);
                            *ok = 0;
                        }
                    } else {
                        vm_set_err(vm, "method call expects strbuf (for now)", e->source_name, e->line, e->col);
                        *ok = 0;
                    }
                }

                if (argv0) { for (int i = 0; i < argc0; i++) cs_value_release(argv0[i]); free(argv0); }
                cs_value_release(self);
                return out;
            }

            cs_value callee = eval_expr(vm, env, e->as.call.callee, ok);
            if (!*ok) return cs_nil();

            int argc = (int)e->as.call.argc;
            cs_value* argv = NULL;
            if (argc > 0) {
                argv = (cs_value*)calloc((size_t)argc, sizeof(cs_value));
                if (!argv) { cs_value_release(callee); vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
                for (int i = 0; i < argc; i++) {
                    argv[i] = eval_expr(vm, env, e->as.call.args[i], ok);
                    if (!*ok) break;
                }
            }

            cs_value out = cs_nil();

            if (*ok) {
                if (callee.type == CS_T_NATIVE) {
                    cs_native* nf = as_native(callee);
                    vm_frames_push(vm, call_name ? call_name : "<native>", e->source_name, e->line, e->col);
                    if (nf->fn(vm, nf->userdata, argc, argv, &out) != 0) {
                        // Preserve any existing native-provided error message
                        if (!vm->last_error) vm_set_err(vm, "native call failed", e->source_name, e->line, e->col);
                        *ok = 0;
                    }
                    vm_frames_pop(vm);
                } else if (callee.type == CS_T_FUNC) {
                    struct cs_func* fn = as_func(callee);
                    cs_env* callenv = env_new(fn->closure);
                    if (!callenv) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }

                    if (*ok) {
                        if ((size_t)argc != fn->param_count) {
                            vm_set_err(vm, "wrong argument count", e->source_name, e->line, e->col);
                            *ok = 0;
                        } else {
                            vm_frames_push(vm, call_name ? call_name : (fn->name ? fn->name : "<fn>"), e->source_name, e->line, e->col);
                            for (size_t i = 0; i < fn->param_count; i++) {
                                env_set_here(callenv, fn->params[i], argv[i]);
                            }
                            exec_result r = exec_block(vm, callenv, fn->body);
                            if (!r.ok) *ok = 0;
                            if (r.did_return) out = cs_value_copy(r.ret);
                            cs_value_release(r.ret);
                            vm_frames_pop(vm);
                        }
                    }
                    env_decref(callenv);
                } else {
                    vm_set_err(vm, "attempted to call non-function", e->source_name, e->line, e->col);
                    *ok = 0;
                }
            }

            if (argv) {
                for (int i = 0; i < argc; i++) cs_value_release(argv[i]);
                free(argv);
            }
            cs_value_release(callee);
            return out;
        }

        default:
            vm_set_err(vm, "invalid expression node", e->source_name, e->line, e->col);
            *ok = 0;
            return cs_nil();
    }
}

static exec_result exec_block(cs_vm* vm, cs_env* env, ast* b) {
    exec_result r;
    r.did_return = 0;
    r.ret = cs_nil();
    r.ok = 1;

    if (!b || b->type != N_BLOCK) return r;

    for (size_t i = 0; i < b->as.block.count; i++) {
        r = exec_stmt(vm, env, b->as.block.items[i]);
        if (!r.ok || r.did_return) return r;
    }
    return r;
}

static exec_result exec_stmt(cs_vm* vm, cs_env* env, ast* s) {
    exec_result r; r.did_return=0; r.ret=cs_nil(); r.ok=1;
    if (!s) return r;

    switch (s->type) {
        case N_BLOCK: {
            cs_env* inner = env_new(env);
            if (!inner) { vm_set_err(vm, "out of memory", s->source_name, s->line, s->col); r.ok = 0; return r; }
            r = exec_block(vm, inner, s);
            env_decref(inner);
            return r;
        }

        case N_LET: {
            cs_value v = cs_nil();
            if (s->as.let_stmt.init) {
                int ok = 1;
                v = eval_expr(vm, env, s->as.let_stmt.init, &ok);
                if (!ok) { r.ok = 0; return r; }
            }
            env_set_here(env, s->as.let_stmt.name, v);
            cs_value_release(v);
            return r;
        }

        case N_ASSIGN: {
            // Fast-path: `x = x + rhs` for strings (in-place append when safe).
            ast* rhs = s->as.assign_stmt.value;
            if (rhs && rhs->type == N_BINOP && rhs->as.binop.op == TK_PLUS &&
                rhs->as.binop.left && rhs->as.binop.left->type == N_IDENT &&
                rhs->as.binop.left->as.ident.name &&
                strcmp(rhs->as.binop.left->as.ident.name, s->as.assign_stmt.name) == 0) {

                cs_value* slot = env_get_slot(env, s->as.assign_stmt.name);
                if (slot && slot->type == CS_T_STR) {
                    cs_string* base = (cs_string*)slot->as.p;
                    if (base && base->ref == 1) {
                        int ok = 1;
                        cs_value rv = eval_expr(vm, env, rhs->as.binop.right, &ok);
                        if (!ok) { r.ok = 0; return r; }

                        const char* add = NULL;
                        size_t add_len = 0;
                        char tmp[64];

                        if (rv.type == CS_T_STR) {
                            cs_string* rs = (cs_string*)rv.as.p;
                            add = rs ? rs->data : "";
                            add_len = rs ? rs->len : 0;
                        } else if (rv.type == CS_T_INT) {
                            int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)rv.as.i);
                            add = tmp;
                            add_len = (n > 0) ? (size_t)n : 0;
                        } else {
                            cs_value_release(rv);
                            vm_set_err(vm, "type error: '+' expects int or string", s->source_name, s->line, s->col);
                            r.ok = 0;
                            return r;
                        }

                        int appended = str_append_inplace(base, add, add_len);
                        cs_value_release(rv);
                        if (!appended) { vm_set_err(vm, "out of memory", s->source_name, s->line, s->col); r.ok = 0; }
                        return r;
                    }
                }
            }

            int ok = 1;
            cs_value v = eval_expr(vm, env, rhs, &ok);
            if (!ok) { r.ok = 0; return r; }
            env_set(env, s->as.assign_stmt.name, v);
            cs_value_release(v);
            return r;
        }

        case N_SETINDEX: {
            int ok = 1;
            cs_value target = eval_expr(vm, env, s->as.setindex_stmt.target, &ok);
            if (!ok) { r.ok = 0; return r; }
            cs_value index = eval_expr(vm, env, s->as.setindex_stmt.index, &ok);
            if (!ok) { cs_value_release(target); r.ok = 0; return r; }
            cs_value value = eval_expr(vm, env, s->as.setindex_stmt.value, &ok);
            if (!ok) { cs_value_release(target); cs_value_release(index); r.ok = 0; return r; }

            int wrote = 0;
            if (target.type == CS_T_LIST && index.type == CS_T_INT) {
                wrote = list_set(as_list(target), index.as.i, value);
                if (!wrote) vm_set_err(vm, "list index assignment failed", s->source_name, s->line, s->col);
            } else if (target.type == CS_T_MAP && index.type == CS_T_STR) {
                wrote = map_set(as_map(target), as_str(index), value);
                if (!wrote) vm_set_err(vm, "map assignment failed", s->source_name, s->line, s->col);
            } else {
                vm_set_err(vm, "index assignment expects list[int] or map[string]", s->source_name, s->line, s->col);
            }

            cs_value_release(target);
            cs_value_release(index);
            cs_value_release(value);
            if (!wrote) { r.ok = 0; }
            return r;
        }

        case N_FNDEF: {
            struct cs_func* f = (struct cs_func*)calloc(1, sizeof(struct cs_func));
            if (!f) { vm_set_err(vm, "out of memory", s->source_name, s->line, s->col); r.ok = 0; return r; }
            f->ref = 1;
            f->name = s->as.fndef.name;
            f->def_source = s->source_name;
            f->def_line = s->line;
            f->def_col = s->col;
            f->params = s->as.fndef.params;
            f->param_count = s->as.fndef.param_count;
            f->body = s->as.fndef.body;
            f->closure = env;
            env_incref(env);

            cs_value fv; fv.type = CS_T_FUNC; fv.as.p = f;
            env_set_here(env, s->as.fndef.name, fv);
            cs_value_release(fv);
            return r;
        }

        case N_IF: {
            int ok = 1;
            cs_value c = eval_expr(vm, env, s->as.if_stmt.cond, &ok);
            if (!ok) { r.ok = 0; return r; }
            int t = is_truthy(c);
            cs_value_release(c);

            if (t) return exec_stmt(vm, env, s->as.if_stmt.then_b);
            if (s->as.if_stmt.else_b) return exec_stmt(vm, env, s->as.if_stmt.else_b);
            return r;
        }

        case N_WHILE: {
            for (;;) {
                int ok = 1;
                cs_value c = eval_expr(vm, env, s->as.while_stmt.cond, &ok);
                if (!ok) { r.ok = 0; return r; }
                int t = is_truthy(c);
                cs_value_release(c);
                if (!t) break;

                r = exec_stmt(vm, env, s->as.while_stmt.body);
                if (!r.ok || r.did_return) return r;
            }
            return r;
        }

        case N_RETURN: {
            r.did_return = 1;
            if (s->as.ret_stmt.value) {
                int ok = 1;
                r.ret = eval_expr(vm, env, s->as.ret_stmt.value, &ok);
                if (!ok) { r.ok = 0; r.did_return = 0; return r; }
            } else {
                r.ret = cs_nil();
            }
            return r;
        }

        case N_EXPR_STMT: {
            int ok = 1;
            cs_value v = eval_expr(vm, env, s->as.expr_stmt.expr, &ok);
            if (!ok) { r.ok = 0; return r; }
            cs_value_release(v);
            return r;
        }

        default:
            vm_set_err(vm, "invalid statement node", s->source_name, s->line, s->col);
            r.ok = 0;
            return r;
    }
}

// ---------- public API ----------
cs_vm* cs_vm_new(void) {
    cs_vm* vm = (cs_vm*)calloc(1, sizeof(cs_vm));
    if (!vm) return NULL;
    vm->globals = env_new(NULL);
    vm->last_error = NULL;
    vm->frames = NULL;
    vm->frame_count = 0;
    vm->frame_cap = 0;
    vm->sources = NULL;
    vm->source_count = 0;
    vm->source_cap = 0;
    vm->dir_stack = NULL;
    vm->dir_count = 0;
    vm->dir_cap = 0;
    vm->require_list = NULL;
    vm->require_count = 0;
    vm->require_cap = 0;
    return vm;
}

void cs_vm_free(cs_vm* vm) {
    if (!vm) return;
    env_decref(vm->globals);
    free(vm->last_error);
    free(vm->frames);
    for (size_t i = 0; i < vm->source_count; i++) free(vm->sources[i]);
    free(vm->sources);
    for (size_t i = 0; i < vm->dir_count; i++) free(vm->dir_stack[i]);
    free(vm->dir_stack);
    for (size_t i = 0; i < vm->require_count; i++) free(vm->require_list[i]);
    free(vm->require_list);
    free(vm);
}

void cs_register_native(cs_vm* vm, const char* name, cs_native_fn fn, void* userdata) {
    if (!vm || !name || !fn) return;
    cs_native* nf = (cs_native*)calloc(1, sizeof(cs_native));
    if (!nf) return;
    nf->ref = 1;
    nf->fn = fn;
    nf->userdata = userdata;
    cs_value v; v.type = CS_T_NATIVE; v.as.p = nf;
    env_set_here(vm->globals, name, v);
    cs_value_release(v);
}

static int run_ast(cs_vm* vm, ast* prog) {
    exec_result r = exec_block(vm, vm->globals, prog);
    cs_value_release(r.ret);
    return r.ok ? 0 : -1;
}

int cs_vm_run_string(cs_vm* vm, const char* code, const char* virtual_name) {
    if (!vm || !code) return -1;

    // Clear stale errors before running new code
    free(vm->last_error);
    vm->last_error = NULL;

    parser P;
    const char* srcname = vm_intern_source(vm, virtual_name ? virtual_name : "<input>");
    parser_init(&P, code, srcname);
    ast* prog = parse_program(&P);

    if (P.error) {
        free(vm->last_error);
        vm->last_error = cs_strdup2(P.error);
        parse_free_error(&P);
        return -1;
    }
    int rc = run_ast(vm, prog);
    // NOTE: v0 does not free AST; treat VM as owning program lifetime (fine for plugins)
    return rc;
}

static char* read_file_all(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t r = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[r] = 0;
    return buf;
}

int cs_vm_run_file(cs_vm* vm, const char* path) {
    if (!vm || !path) return -1;
    // Clear stale errors before loading/exec a file
    free(vm->last_error);
    vm->last_error = NULL;
    char* code = read_file_all(path);
    if (!code) {
        free(vm->last_error);
        vm->last_error = cs_strdup2("could not read file");
        return -1;
    }
    char* dir = path_dirname_alloc(path);
    vm_dir_push_owned(vm, dir);
    int rc = cs_vm_run_string(vm, code, path);
    vm_dir_pop(vm);
    free(code);
    return rc;
}

int cs_call(cs_vm* vm, const char* func_name, int argc, const cs_value* argv, cs_value* out) {
    if (out) *out = cs_nil();
    if (!vm || !func_name) return -1;

    cs_value f;
    if (!env_get(vm->globals, func_name, &f)) return -1;

    int ok = 1;
    cs_value result = cs_nil();

    const char* host_src = vm_intern_source(vm, "(host)");
    vm_frames_push(vm, func_name, host_src, 0, 0);

    if (f.type == CS_T_NATIVE) {
        cs_native* nf = as_native(f);
        if (nf->fn(vm, nf->userdata, argc, argv, &result) != 0) {
            if (!vm->last_error) cs_error(vm, "native call failed");
            ok = 0;
        }
    } else if (f.type == CS_T_FUNC) {
        struct cs_func* fn = as_func(f);
        if ((size_t)argc != fn->param_count) ok = 0;
        else {
            cs_env* callenv = env_new(fn->closure);
            if (!callenv) ok = 0;
            else {
                for (size_t i = 0; i < fn->param_count; i++) env_set_here(callenv, fn->params[i], argv[i]);
                exec_result r = exec_block(vm, callenv, fn->body);
                if (!r.ok) ok = 0;
                if (r.did_return) result = cs_value_copy(r.ret);
                cs_value_release(r.ret);
                env_decref(callenv);
            }
        }
    } else {
        ok = 0;
    }

    vm_frames_pop(vm);

    cs_value_release(f);
    if (!ok) return -1;
    if (out) *out = result;
    else cs_value_release(result);
    return 0;
}

int cs_call_value(cs_vm* vm, cs_value callee, int argc, const cs_value* argv, cs_value* out) {
    if (out) *out = cs_nil();
    if (!vm) return -1;

    // Clear stale host errors before calling into a callback
    free(vm->last_error);
    vm->last_error = NULL;

    const char* host_src = vm_intern_source(vm, "(host)");
    vm_frames_push(vm, "(callback)", host_src, 0, 0);

    int ok = 1;
    cs_value result = cs_nil();

    if (callee.type == CS_T_NATIVE) {
        cs_native* nf = as_native(callee);
        if (nf->fn(vm, nf->userdata, argc, argv, &result) != 0) {
            if (!vm->last_error) cs_error(vm, "native call failed");
            ok = 0;
        }
    } else if (callee.type == CS_T_FUNC) {
        struct cs_func* fn = as_func(callee);
        if ((size_t)argc != fn->param_count) ok = 0;
        else {
            cs_env* callenv = env_new(fn->closure);
            if (!callenv) ok = 0;
            else {
                for (size_t i = 0; i < fn->param_count; i++) env_set_here(callenv, fn->params[i], argv[i]);
                exec_result r = exec_block(vm, callenv, fn->body);
                if (!r.ok) ok = 0;
                if (r.did_return) result = cs_value_copy(r.ret);
                cs_value_release(r.ret);
                env_decref(callenv);
            }
        }
    } else {
        ok = 0;
    }

    vm_frames_pop(vm);

    if (!ok) return -1;
    if (out) *out = result;
    else cs_value_release(result);
    return 0;
}
