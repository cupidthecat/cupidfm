#include "cs_vm.h"
#include "cs_event_loop.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#if !defined(_WIN32)
#include <sys/time.h>
#else
#include <windows.h>
#endif

static char* cs_strdup2(const char* s) {
    size_t n = strlen(s ? s : "");
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

// Forward declarations for scheduler helpers
static int bind_params_with_defaults(cs_vm* vm, cs_env* callenv, struct cs_func* fn, int argc, const cs_value* argv, int* ok);
static void vm_set_pending_throw(cs_vm* vm, cs_value thrown);

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

static const char* vm_intern_source(cs_vm* vm, const char* src) {
    if (!vm) return src ? src : "";
    if (!src) src = "";
    for (size_t i = 0; i < vm->source_count; i++) {
        if (strcmp(vm->sources[i], src) == 0) return vm->sources[i];
    }
    if (vm->source_count == vm->source_cap) {
        size_t nc = vm->source_cap ? vm->source_cap * 2 : 16;
        char** ns = (char**)realloc(vm->sources, nc * sizeof(char*));
        if (!ns) return src;
        vm->sources = ns;
        vm->source_cap = nc;
    }
    char* dup = cs_strdup2(src);
    if (!dup) return src;
    vm->sources[vm->source_count++] = dup;
    return dup;
}

static char* path_normalize_alloc(const char* path) {
    if (!path || !*path) return cs_strdup2(".");
    size_t len = strlen(path);
    // Work on a mutable copy so we can NUL-terminate segments in-place.
    char* tmp = (char*)malloc(len + 1);
    if (!tmp) return NULL;
    for (size_t i = 0; i < len; i++) {
        tmp[i] = (path[i] == '\\') ? '/' : path[i];
    }
    tmp[len] = 0;

    // Detect prefix / root.
    // - POSIX absolute: "/..."
    // - Windows drive: "C:" or "C:/..."
    // - UNC-ish: "//server/share" (we preserve leading "//")
    size_t start = 0;
    int is_abs = 0;
    int has_drive = 0;
    char drive0 = 0;
    if (len >= 2 && ((tmp[0] >= 'A' && tmp[0] <= 'Z') || (tmp[0] >= 'a' && tmp[0] <= 'z')) && tmp[1] == ':') {
        has_drive = 1;
        drive0 = tmp[0];
        start = 2;
        if (tmp[2] == '/') {
            is_abs = 1;
            start = 3;
        }
    } else if (tmp[0] == '/') {
        is_abs = 1;
        if (tmp[1] == '/') {
            // UNC-ish / network path.
            start = 2;
        } else {
            start = 1;
        }
    }

    size_t max_parts = (len / 2) + 2;
    char** parts = (char**)malloc(sizeof(char*) * max_parts);
    if (!parts) { free(tmp); return NULL; }
    size_t count = 0;

    // Split into segments and resolve '.' / '..'.
    size_t i = start;
    while (i <= len) {
        // Skip slashes
        while (i < len && tmp[i] == '/') i++;
        if (i >= len) break;

        size_t seg_start = i;
        while (i < len && tmp[i] != '/') i++;
        tmp[i] = 0; // terminate segment
        char* seg = &tmp[seg_start];

        if (seg[0] == 0 || (seg[0] == '.' && seg[1] == 0)) {
            // skip empty and '.'
        } else if (seg[0] == '.' && seg[1] == '.' && seg[2] == 0) {
            // '..'
            if (count > 0 && strcmp(parts[count - 1], "..") != 0) {
                count--; // pop
            } else {
                // For relative paths, preserve leading ".."; for absolute paths, clamp at root.
                if (!is_abs) parts[count++] = seg;
            }
        } else {
            parts[count++] = seg;
        }

        i++; // move past NUL (was '/')
    }

    // Compute output length.
    size_t out_len = 0;
    if (has_drive) {
        out_len += 2; // "C:"
        if (is_abs) out_len += 1; // "/"
    } else if (is_abs) {
        out_len += (tmp[0] == '/' && tmp[1] == '/') ? 2 : 1;
    }
    if (count == 0) {
        if (!is_abs && !has_drive) {
            free(parts);
            free(tmp);
            return cs_strdup2(".");
        }
        // Absolute root (or drive root / "C:" relative-to-drive).
        char* out0 = (char*)malloc(out_len + 1);
        if (!out0) { free(parts); free(tmp); return NULL; }
        size_t w0 = 0;
        if (has_drive) {
            out0[w0++] = drive0;
            out0[w0++] = ':';
            if (is_abs) out0[w0++] = '/';
        } else if (is_abs) {
            out0[w0++] = '/';
            if (tmp[0] == '/' && tmp[1] == '/') out0[w0++] = '/';
        }
        out0[w0] = 0;
        free(parts);
        free(tmp);
        return out0;
    }

    for (size_t p = 0; p < count; p++) {
        out_len += strlen(parts[p]);
        if (p + 1 < count) out_len += 1; // '/'
    }
    // If prefix exists and doesn't already end with '/', we may need one before first segment.
    int prefix_ends_with_slash = 0;
    if (has_drive) prefix_ends_with_slash = is_abs;
    else if (is_abs) prefix_ends_with_slash = 1; // '/' or '//'
    if ((has_drive || is_abs) && !prefix_ends_with_slash) out_len += 1;

    char* out = (char*)malloc(out_len + 1);
    if (!out) { free(parts); free(tmp); return NULL; }

    size_t w = 0;
    if (has_drive) {
        out[w++] = drive0;
        out[w++] = ':';
        if (is_abs) out[w++] = '/';
    } else if (is_abs) {
        out[w++] = '/';
        if (tmp[0] == '/' && tmp[1] == '/') out[w++] = '/';
    }

    if (w > 0 && out[w - 1] != '/') {
        out[w++] = '/';
    }

    for (size_t p = 0; p < count; p++) {
        size_t n = strlen(parts[p]);
        memcpy(out + w, parts[p], n);
        w += n;
        if (p + 1 < count) out[w++] = '/';
    }
    out[w] = 0;

    free(parts);
    free(tmp);
    return out;
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

static void vm_maybe_auto_gc(cs_vm* vm);  // Forward declaration
static int list_push(cs_list_obj* l, cs_value v);  // Forward declaration
static cs_range_obj* range_new(int64_t start, int64_t end, int64_t step, int inclusive);
static void range_incref(cs_range_obj* r);
static void range_decref(cs_range_obj* r);

static cs_env* env_new(cs_env* parent);

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

cs_vm* cs_vm_new(void) {
    cs_vm* vm = (cs_vm*)calloc(1, sizeof(cs_vm));
    if (!vm) return NULL;
    vm->globals = env_new(NULL);
    if (!vm->globals) {
        free(vm);
        return NULL;
    }
    vm->last_error = NULL;
    vm->pending_throw = 0;
    vm->pending_thrown = cs_nil();

    vm->frames = NULL;
    vm->frame_count = 0;
    vm->frame_cap = 0;

    vm->sources = NULL;
    vm->source_count = 0;
    vm->source_cap = 0;

    vm->dir_stack = NULL;
    vm->dir_count = 0;
    vm->dir_cap = 0;

    vm->modules = NULL;
    vm->module_count = 0;
    vm->module_cap = 0;

    vm->tracked = NULL;
    vm->tracked_count = 0;

    vm->asts = NULL;
    vm->ast_count = 0;
    vm->ast_cap = 0;

    vm->gc_threshold = 0;
    vm->gc_allocations = 0;
    vm->gc_alloc_trigger = 0;
    vm->gc_collections = 0;
    vm->gc_objects_collected = 0;

    vm->instruction_count = 0;
    vm->instruction_limit = 0;
    vm->exec_start_ms = 0;
    vm->exec_timeout_ms = 0;
    vm->interrupt_requested = 0;
    vm->yield_list = NULL;
    vm->yield_active = 0;
    vm->yield_used = 0;

    vm->task_head = NULL;
    vm->task_tail = NULL;
    vm->timers = NULL;
    vm->pending_io = NULL;
    vm->pending_io_count = 0;
    vm->net_default_timeout_ms = 30000;

    return vm;
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
cs_value cs_float(double x){ cs_value v; v.type=CS_T_FLOAT; v.as.f = x; return v; }

cs_type cs_typeof(cs_value v) { return v.type; }
const char* cs_type_name(cs_type t){ return cs_type_name_impl(t); }

static const char* interp_repr(cs_value v, char* buf, size_t buf_sz) {
    if (!buf || buf_sz == 0) return "";
    buf[0] = 0;
    switch (v.type) {
        case CS_T_NIL:  return "nil";
        case CS_T_BOOL: return v.as.b ? "true" : "false";
        case CS_T_INT:
            snprintf(buf, buf_sz, "%lld", (long long)v.as.i);
            return buf;
        case CS_T_FLOAT:
            snprintf(buf, buf_sz, "%g", v.as.f);
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
        case CS_T_RANGE: {
            cs_range_obj* r = (cs_range_obj*)v.as.p;
            if (!r) return "<range>";
            snprintf(buf, buf_sz, "<range %lld..%s%lld>",
                (long long)r->start,
                r->inclusive ? "=" : "",
                (long long)r->end);
            return buf;
        }
        case CS_T_FUNC:   return "<function>";
        case CS_T_NATIVE: return "<native>";
        case CS_T_PROMISE: {
            cs_promise_obj* p = (cs_promise_obj*)v.as.p;
            const char* state = "pending";
            if (p && p->state == 1) state = "fulfilled";
            else if (p && p->state == 2) state = "rejected";
            snprintf(buf, buf_sz, "<promise %s>", state);
            return buf;
        }
        default:          return "<obj>";
    }
}

static cs_native* as_native(cs_value v){ return (cs_native*)v.as.p; }
static cs_string* as_str(cs_value v){ return (cs_string*)v.as.p; }
static cs_list_obj* as_list(cs_value v){ return (cs_list_obj*)v.as.p; }
static cs_map_obj*  as_map(cs_value v){ return (cs_map_obj*)v.as.p; }
static cs_strbuf_obj* as_strbuf(cs_value v){ return (cs_strbuf_obj*)v.as.p; }
static struct cs_func* as_func(cs_value v){ return (struct cs_func*)v.as.p; }
static cs_promise_obj* as_promise(cs_value v){ return (cs_promise_obj*)v.as.p; }

static void env_incref(cs_env* e);
static void env_decref(cs_env* e);

const char* cs_to_cstr(cs_value v) {
    if (v.type == CS_T_STR) return as_str(v)->data;
    return NULL;
}

// Capture current call stack for error objects
cs_value cs_capture_stack_trace(cs_vm* vm) {
    cs_value stack_list = cs_list(vm);
    if (!stack_list.as.p) return cs_nil();
    cs_list_obj* list = as_list(stack_list);
    
    for (size_t i = vm->frame_count; i > 0; i--) {
        cs_frame* frame = &vm->frames[i - 1];
        
        char buf[512];
        if (frame->func && frame->func[0]) {
            snprintf(buf, sizeof(buf), "%s() at %s:%d:%d",
                frame->func,
                frame->source ? frame->source : "<unknown>",
                frame->line,
                frame->col);
        } else {
            snprintf(buf, sizeof(buf), "<script> at %s:%d:%d",
                frame->source ? frame->source : "<unknown>",
                frame->line,
                frame->col);
        }
        
        cs_value entry = cs_str(vm, buf);
        if (entry.as.p) {
            list_push(list, entry);  // Use list_push() to ensure capacity
        }
    }
    
    return stack_list;
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

static int vm_track_add(cs_vm* vm, cs_track_type type, void* ptr) {
    if (!vm || !ptr) return 0;
    cs_track_node* n = (cs_track_node*)calloc(1, sizeof(cs_track_node));
    if (!n) return 0;
    n->type = type;
    n->ptr = ptr;
    n->next = vm->tracked;
    vm->tracked = n;
    vm->tracked_count++;
    return 1;
}

static void vm_track_remove(cs_vm* vm, cs_track_type type, void* ptr) {
    if (!vm || !ptr) return;
    cs_track_node** pp = &vm->tracked;
    while (*pp) {
        cs_track_node* cur = *pp;
        if (cur->type == type && cur->ptr == ptr) {
            *pp = cur->next;
            free(cur);
            if (vm->tracked_count) vm->tracked_count--;
            return;
        }
        pp = &cur->next;
    }
}

static cs_list_obj* list_new(cs_vm* vm) {
    cs_list_obj* l = (cs_list_obj*)calloc(1, sizeof(cs_list_obj));
    if (!l) return NULL;
    l->ref = 1;
    l->owner = vm;
    l->cap = 8;
    l->items = (cs_value*)calloc(l->cap, sizeof(cs_value));
    if (!l->items) { free(l); return NULL; }
    l->len = 0;
    if (!vm_track_add(vm, CS_TRACK_LIST, l)) { free(l->items); free(l); return NULL; }
    
    // Track allocation and maybe trigger GC
    if (vm) {
        vm->gc_allocations++;
        vm_maybe_auto_gc(vm);
    }
    
    return l;
}

static void list_incref(cs_list_obj* l) { if (l) l->ref++; }

static void list_decref(cs_list_obj* l) {
    if (!l) return;
    if (--l->ref > 0) return;
    for (size_t i = 0; i < l->len; i++) cs_value_release(l->items[i]);
    free(l->items);
    vm_track_remove(l->owner, CS_TRACK_LIST, l);
    free(l);
}

static cs_map_obj* map_new(cs_vm* vm) {
    cs_map_obj* m = (cs_map_obj*)calloc(1, sizeof(cs_map_obj));
    if (!m) return NULL;
    m->ref = 1;
    m->owner = vm;
    m->cap = 8;
    m->entries = (cs_map_entry*)calloc(m->cap, sizeof(cs_map_entry));
    if (!m->entries) { free(m); return NULL; }
    m->len = 0;
    if (!vm_track_add(vm, CS_TRACK_MAP, m)) { free(m->entries); free(m); return NULL; }
    
    // Track allocation and maybe trigger GC
    if (vm) {
        vm->gc_allocations++;
        vm_maybe_auto_gc(vm);
    }
    
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

static cs_range_obj* as_range(cs_value v){ return (cs_range_obj*)v.as.p; }
static void map_incref(cs_map_obj* m) { if (m) m->ref++; }

static void map_decref(cs_map_obj* m) {
    if (!m) return;
    if (--m->ref > 0) return;
    for (size_t i = 0; i < m->cap; i++) {
        if (!m->entries[i].in_use) continue;
        cs_value_release(m->entries[i].key);
        cs_value_release(m->entries[i].val);
    }
    free(m->entries);
    vm_track_remove(m->owner, CS_TRACK_MAP, m);
    free(m);
}

static cs_range_obj* range_new(int64_t start, int64_t end, int64_t step, int inclusive) {
    cs_range_obj* r = (cs_range_obj*)calloc(1, sizeof(cs_range_obj));
    if (!r) return NULL;
    r->ref = 1;
    r->start = start;
    r->end = end;
    r->step = step;
    r->inclusive = inclusive ? 1 : 0;
    return r;
}

static void range_incref(cs_range_obj* r) {
    if (r) r->ref++;
}

static void range_decref(cs_range_obj* r) {
    if (!r) return;
    r->ref--;
    if (r->ref <= 0) {
        free(r);
    }
}

static void promise_incref(cs_promise_obj* p) {
    if (p) p->ref++;
}

static void promise_decref(cs_promise_obj* p) {
    if (!p) return;
    p->ref--;
    if (p->ref <= 0) {
        cs_value_release(p->value);
        free(p);
    }
}

cs_value cs_list(cs_vm* vm) {
    cs_list_obj* l = list_new(vm);
    cs_value v; v.type = CS_T_LIST; v.as.p = l;
    return v;
}

cs_value cs_map(cs_vm* vm) {
    cs_map_obj* m = map_new(vm);
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
    else if (v.type == CS_T_RANGE) range_incref(as_range(v));
    else if (v.type == CS_T_PROMISE) promise_incref(as_promise(v));
    else if (v.type == CS_T_NATIVE) as_native(v)->ref++;
    else if (v.type == CS_T_FUNC) as_func(v)->ref++;
    return v;
}

void cs_value_release(cs_value v) {
    if (v.type == CS_T_STR) cs_str_decref(as_str(v));
    else if (v.type == CS_T_LIST) list_decref(as_list(v));
    else if (v.type == CS_T_MAP) map_decref(as_map(v));
    else if (v.type == CS_T_STRBUF) strbuf_decref(as_strbuf(v));
    else if (v.type == CS_T_RANGE) range_decref(as_range(v));
    else if (v.type == CS_T_PROMISE) promise_decref(as_promise(v));
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
    free(e->is_const);
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
    e->is_const = (unsigned char*)calloc(e->cap, sizeof(unsigned char));
    return e;
}

static int env_find(cs_env* e, const char* key) {
    for (size_t i = 0; i < e->count; i++) {
        if (strcmp(e->keys[i], key) == 0) return (int)i;
    }
    return -1;
}

static void env_set_here_ex(cs_env* e, const char* key, cs_value v, int is_const) {
    int idx = env_find(e, key);
    if (idx >= 0) {
        cs_value_release(e->vals[idx]);
        e->vals[idx] = cs_value_copy(v);
        if (is_const) e->is_const[idx] = 1;
        return;
    }
    if (e->count == e->cap) {
        e->cap *= 2;
        e->keys = (char**)realloc(e->keys, e->cap * sizeof(char*));
        e->vals = (cs_value*)realloc(e->vals, e->cap * sizeof(cs_value));
        e->is_const = (unsigned char*)realloc(e->is_const, e->cap * sizeof(unsigned char));
    }
    e->keys[e->count] = cs_strdup2(key);
    e->vals[e->count] = cs_value_copy(v);
    e->is_const[e->count] = (unsigned char)(is_const ? 1 : 0);
    e->count++;
}

static void env_set_here(cs_env* e, const char* key, cs_value v) {
    env_set_here_ex(e, key, v, 0);
}

static int env_is_const(cs_env* e, const char* key) {
    for (cs_env* cur = e; cur; cur = cur->parent) {
        int idx = env_find(cur, key);
        if (idx >= 0) return cur->is_const[idx] ? 1 : 0;
    }
    return 0;
}

static int env_assign_existing(cs_env* e, const char* key, cs_value v) {
    // assignment walks upward, but must target an existing binding
    for (cs_env* cur = e; cur; cur = cur->parent) {
        int idx = env_find(cur, key);
        if (idx >= 0) {
            if (cur->is_const[idx]) {
                return -1; // const binding
            }
            cs_value_release(cur->vals[idx]);
            cur->vals[idx] = cs_value_copy(v);
            return 1;
        }
    }
    return 0;
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

static int vm_value_equals(cs_value a, cs_value b) {
    return cs_value_key_equals(a, b);
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

static uint32_t map_key_hash(cs_value key) {
    return cs_value_hash(key);
}

static int map_key_equals(cs_value a, cs_value b) {
    return cs_value_key_equals(a, b);
}

static int map_rehash(cs_map_obj* m, size_t new_cap) {
    if (!m) return 0;
    if (new_cap < 8) new_cap = 8;

    cs_map_entry* ne = (cs_map_entry*)calloc(new_cap, sizeof(cs_map_entry));
    if (!ne) return 0;

    cs_map_entry* old_entries = m->entries;
    size_t old_cap = m->cap;

    m->entries = ne;
    m->cap = new_cap;

    if (old_entries) {
        for (size_t i = 0; i < old_cap; i++) {
            if (!old_entries[i].in_use) continue;
            size_t idx = old_entries[i].hash % new_cap;
            while (ne[idx].in_use) idx = (idx + 1) % new_cap;
            ne[idx] = old_entries[i];
        }
        free(old_entries);
    }

    return 1;
}

static int map_find(cs_map_obj* m, cs_value key, uint32_t hash) {
    if (!m || m->len == 0) return -1;

    size_t idx = hash % m->cap;
    size_t probe = 0;
    while (probe < m->cap) {
        if (!m->entries[idx].in_use) return -1;
        if (m->entries[idx].hash == hash && map_key_equals(m->entries[idx].key, key)) return (int)idx;
        idx = (idx + 1) % m->cap;
        probe++;
    }
    return -1;
}

static int map_ensure(cs_map_obj* m, size_t need) {
    if (!m) return 0;
    if (need <= m->cap) return 1;

    size_t nc = m->cap ? m->cap : 8;
    while (nc < need) nc *= 2;
    return map_rehash(m, nc);
}

static cs_value map_get_value(cs_map_obj* m, cs_value key) {
    uint32_t h = map_key_hash(key);
    int idx = map_find(m, key, h);
    if (idx < 0) return cs_nil();
    return cs_value_copy(m->entries[(size_t)idx].val);
}

static int map_set_value(cs_map_obj* m, cs_value key, cs_value v) {
    if (!m) return 0;

    // Maintain load factor < 0.7
    if ((m->len + 1) > (m->cap * 7 / 10)) {
        size_t new_cap = m->cap ? m->cap * 2 : 8;
        if (!map_ensure(m, new_cap)) return 0;
    }

    uint32_t h = map_key_hash(key);
    size_t idx = h % m->cap;
    size_t probe = 0;
    while (probe < m->cap) {
        if (!m->entries[idx].in_use) {
            m->entries[idx].key = cs_value_copy(key);
            m->entries[idx].val = cs_value_copy(v);
            m->entries[idx].hash = h;
            m->entries[idx].in_use = 1;
            m->len++;
            return 1;
        }
        if (m->entries[idx].hash == h && map_key_equals(m->entries[idx].key, key)) {
            cs_value_release(m->entries[idx].val);
            m->entries[idx].val = cs_value_copy(v);
            return 1;
        }
        idx = (idx + 1) % m->cap;
        probe++;
    }

    return 0;
}

static int map_has_value(cs_map_obj* m, cs_value key) {
    if (!m) return 0;
    uint32_t h = map_key_hash(key);
    return map_find(m, key, h) >= 0 ? 1 : 0;
}

static int map_has_cstr(cs_map_obj* m, const char* key) {
    if (!m || !key) return 0;
    cs_string tmp;
    tmp.ref = 1;
    tmp.data = (char*)key;
    tmp.len = strlen(key);
    tmp.cap = tmp.len;
    cs_value kv; kv.type = CS_T_STR; kv.as.p = &tmp;
    return map_has_value(m, kv);
}

static int map_del_value(cs_map_obj* m, cs_value key) {
    if (!m) return 0;
    uint32_t h = map_key_hash(key);
    int idx = map_find(m, key, h);
    if (idx < 0) return 0;

    cs_map_entry* ne = (cs_map_entry*)calloc(m->cap, sizeof(cs_map_entry));
    if (!ne) return 0;

    for (size_t i = 0; i < m->cap; i++) {
        if (!m->entries[i].in_use) continue;
        if ((int)i == idx) continue;
        size_t pos = m->entries[i].hash % m->cap;
        while (ne[pos].in_use) pos = (pos + 1) % m->cap;
        ne[pos] = m->entries[i];
    }

    cs_value_release(m->entries[(size_t)idx].key);
    cs_value_release(m->entries[(size_t)idx].val);

    free(m->entries);
    m->entries = ne;
    if (m->len) m->len--;
    return 1;
}

static cs_value map_get_cstr(cs_map_obj* m, const char* key) {
    if (!m || !key) return cs_nil();
    cs_string tmp;
    tmp.ref = 1;
    tmp.data = (char*)key;
    tmp.len = strlen(key);
    tmp.cap = tmp.len;
    cs_value kv; kv.type = CS_T_STR; kv.as.p = &tmp;
    return map_get_value(m, kv);
}

static int map_set_cstr(cs_map_obj* m, const char* key, cs_value v) {
    if (!m || !key) return 0;
    cs_string* ks = cs_str_new(key);
    if (!ks) return 0;
    cs_value kv; kv.type = CS_T_STR; kv.as.p = ks;
    int ok = map_set_value(m, kv, v);
    cs_str_decref(ks);
    return ok;
}

static cs_string* key_is_class(void) {
    static cs_string* k = NULL;
    if (!k) k = cs_str_new("__is_class");
    return k;
}

static cs_string* key_is_struct(void) {
    static cs_string* k = NULL;
    if (!k) k = cs_str_new("__is_struct");
    return k;
}

static cs_string* key_class(void) {
    static cs_string* k = NULL;
    if (!k) k = cs_str_new("__class");
    return k;
}

static cs_string* key_parent(void) {
    static cs_string* k = NULL;
    if (!k) k = cs_str_new("__parent");
    return k;
}

static cs_string* key_fields(void) {
    static cs_string* k = NULL;
    if (!k) k = cs_str_new("__fields");
    return k;
}

static cs_string* key_defaults(void) {
    static cs_string* k = NULL;
    if (!k) k = cs_str_new("__defaults");
    return k;
}

static cs_string* key_struct(void) {
    static cs_string* k = NULL;
    if (!k) k = cs_str_new("__struct");
    return k;
}

static cs_string* key_new_method(void) {
    static cs_string* k = NULL;
    if (!k) k = cs_str_new("new");
    return k;
}

static cs_value map_get_strkey(cs_map_obj* m, cs_string* key) {
    if (!m || !key) return cs_nil();
    cs_value kv; kv.type = CS_T_STR; kv.as.p = key;
    return map_get_value(m, kv);
}

static int map_has_strkey(cs_map_obj* m, cs_string* key) {
    if (!m || !key) return 0;
    cs_value kv; kv.type = CS_T_STR; kv.as.p = key;
    return map_has_value(m, kv);
}

static int map_set_strkey(cs_map_obj* m, cs_string* key, cs_value v) {
    if (!m || !key) return 0;
    cs_value kv; kv.type = CS_T_STR; kv.as.p = key;
    return map_set_value(m, kv, v);
}

static int map_is_class(cs_value v) {
    if (v.type != CS_T_MAP) return 0;
    cs_string* k = key_is_class();
    cs_value flag = k ? map_get_strkey(as_map(v), k) : map_get_cstr(as_map(v), "__is_class");
    int ok = (flag.type == CS_T_BOOL && flag.as.b);
    cs_value_release(flag);
    return ok;
}

static int map_is_struct(cs_value v) {
    if (v.type != CS_T_MAP) return 0;
    cs_string* k = key_is_struct();
    cs_value flag = k ? map_get_strkey(as_map(v), k) : map_get_cstr(as_map(v), "__is_struct");
    int ok = (flag.type == CS_T_BOOL && flag.as.b);
    cs_value_release(flag);
    if (ok) return 1;
    cs_string* kf = key_fields();
    if ((kf && map_has_strkey(as_map(v), kf)) || map_has_cstr(as_map(v), "__fields")) return 1;
    cs_string* kd = key_defaults();
    if ((kd && map_has_strkey(as_map(v), kd)) || map_has_cstr(as_map(v), "__defaults")) return 1;
    return 0;
}


static cs_value map_get_class(cs_value v) {
    if (v.type != CS_T_MAP) return cs_nil();
    cs_string* k = key_class();
    return k ? map_get_strkey(as_map(v), k) : map_get_cstr(as_map(v), "__class");
}

static int class_find_method(cs_value class_val, const char* name, cs_value* method_out, cs_value* owner_out) {
    if (method_out) *method_out = cs_nil();
    if (owner_out) *owner_out = cs_nil();
    cs_value cur = cs_value_copy(class_val);
    int name_is_new = (name && name[0] == 'n' && name[1] == 'e' && name[2] == 'w' && name[3] == 0);
    cs_string* k_new = name_is_new ? key_new_method() : NULL;
    cs_string* k_parent = key_parent();
    while (cur.type == CS_T_MAP) {
        cs_map_obj* m = as_map(cur);
        if (name_is_new && k_new && map_has_strkey(m, k_new)) {
            if (method_out) *method_out = map_get_strkey(m, k_new);
            if (owner_out) *owner_out = cs_value_copy(cur);
            cs_value_release(cur);
            return 1;
        }
        if (map_has_cstr(m, name)) {
            if (method_out) *method_out = map_get_cstr(m, name);
            if (owner_out) *owner_out = cs_value_copy(cur);
            cs_value_release(cur);
            return 1;
        }
        cs_value parent = k_parent ? map_get_strkey(m, k_parent) : map_get_cstr(m, "__parent");
        cs_value_release(cur);
        cur = parent;
        if (cur.type == CS_T_NIL) { cs_value_release(cur); break; }
    }
    return 0;
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

// ---------- time helper ----------
static uint64_t get_time_ms(void) {
#if !defined(_WIN32)
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)(tv.tv_usec / 1000);
#else
    return (uint64_t)(clock() * 1000 / CLOCKS_PER_SEC);
#endif
}

// ---------- error ----------
static void vm_set_err(cs_vm* vm, const char* msg, const char* source, int line, int col);  // Forward declaration

// ---------- eval ----------
typedef struct {
    int did_return;
    int did_break;
    int did_continue;
    int did_throw;
    cs_value ret;
    cs_value thrown;
    int ok;
} exec_result;

static exec_result exec_block(cs_vm* vm, cs_env* env, ast* b);

static int vm_check_safety(cs_vm* vm, ast* e, int* ok) {
    if (!vm) return 1;
    
    // Check interrupt flag (allows host to abort execution)
    if (vm->interrupt_requested) {
        vm_set_err(vm, "execution interrupted by host", 
                   e ? e->source_name : "<unknown>", 
                   e ? e->line : 0, 
                   e ? e->col : 0);
        *ok = 0;
        return 0;
    }
    
    // Check instruction limit
    if (vm->instruction_limit > 0 && vm->instruction_count >= vm->instruction_limit) {
        char buf[128];
        snprintf(buf, sizeof(buf), "instruction limit exceeded (%llu instructions)", 
                 (unsigned long long)vm->instruction_limit);
        vm_set_err(vm, buf, 
                   e ? e->source_name : "<unknown>", 
                   e ? e->line : 0, 
                   e ? e->col : 0);
        *ok = 0;
        return 0;
    }
    
    // Check timeout (only check every 1000 instructions to reduce overhead)
    if (vm->exec_timeout_ms > 0 && (vm->instruction_count % 1000) == 0) {
        uint64_t elapsed = get_time_ms() - vm->exec_start_ms;
        if (elapsed >= vm->exec_timeout_ms) {
            char buf[128];
            snprintf(buf, sizeof(buf), "execution timeout exceeded (%llu ms)", 
                     (unsigned long long)vm->exec_timeout_ms);
            vm_set_err(vm, buf, 
                       e ? e->source_name : "<unknown>", 
                       e ? e->line : 0, 
                       e ? e->col : 0);
            *ok = 0;
            return 0;
        }
    }
    
    return 1;
}

// ---------- promises + scheduler ----------
static cs_promise_obj* promise_new(void) {
    cs_promise_obj* p = (cs_promise_obj*)calloc(1, sizeof(cs_promise_obj));
    if (!p) return NULL;
    p->ref = 1;
    p->state = 0;
    p->value = cs_nil();
    return p;
}

static int promise_is_pending(cs_promise_obj* p) {
    return p && p->state == 0;
}

static int promise_fulfill(cs_promise_obj* p, cs_value v) {
    if (!p || p->state != 0) return 0;
    p->state = 1;
    p->value = cs_value_copy(v);
    return 1;
}

static int promise_reject(cs_promise_obj* p, cs_value v) {
    if (!p || p->state != 0) return 0;
    p->state = 2;
    p->value = cs_value_copy(v);
    return 1;
}

static cs_value make_promise_value(cs_promise_obj* p) {
    cs_value v; v.type = CS_T_PROMISE; v.as.p = p;
    return v;
}

static void scheduler_push_task(cs_vm* vm, cs_task* t) {
    if (!vm || !t) return;
    t->next = NULL;
    if (!vm->task_tail) {
        vm->task_head = vm->task_tail = t;
    } else {
        vm->task_tail->next = t;
        vm->task_tail = t;
    }
}

static cs_task* scheduler_pop_task(cs_vm* vm) {
    if (!vm || !vm->task_head) return NULL;
    cs_task* t = vm->task_head;
    vm->task_head = t->next;
    if (!vm->task_head) vm->task_tail = NULL;
    t->next = NULL;
    return t;
}

static void scheduler_add_timer(cs_vm* vm, cs_timer* timer) {
    if (!vm || !timer) return;
    if (!vm->timers || timer->due_ms < vm->timers->due_ms) {
        timer->next = vm->timers;
        vm->timers = timer;
        return;
    }
    cs_timer* cur = vm->timers;
    while (cur->next && cur->next->due_ms <= timer->due_ms) cur = cur->next;
    timer->next = cur->next;
    cur->next = timer;
}

static int scheduler_run_due_timers(cs_vm* vm) {
    if (!vm) return 0;
    uint64_t now = get_time_ms();
    int ran = 0;
    while (vm->timers && vm->timers->due_ms <= now) {
        cs_timer* t = vm->timers;
        vm->timers = t->next;
        if (promise_is_pending(t->promise)) {
            promise_fulfill(t->promise, cs_nil());
        }
        promise_decref(t->promise);
        free(t);
        ran = 1;
    }
    return ran;
}

static int scheduler_run_one_task(cs_vm* vm, ast* e, int* ok) {
    if (!vm) return 0;
    cs_task* t = scheduler_pop_task(vm);
    if (!t) return 0;

    cs_env* callenv = NULL;
    if (t->bound_env) {
        callenv = t->bound_env;
    } else {
        callenv = env_new(t->fn->closure);
    }
    if (!callenv) {
        vm_set_err(vm, "out of memory", e ? e->source_name : "<async>", e ? e->line : 0, e ? e->col : 0);
        *ok = 0;
        promise_reject(t->promise, cs_str(vm, "out of memory"));
        goto task_cleanup;
    }

    int bind_ok = 1;
    if (!bind_params_with_defaults(vm, callenv, t->fn, t->argc, t->argv, &bind_ok)) {
        env_decref(callenv);
        if (!bind_ok) {
            if (vm->last_error) {
                cs_value msg = cs_str(vm, vm->last_error);
                promise_reject(t->promise, msg);
                cs_value_release(msg);
                free(vm->last_error);
                vm->last_error = NULL;
            } else {
                promise_reject(t->promise, cs_str(vm, "async bind failed"));
            }
        }
        goto task_cleanup;
    }

    vm_frames_push(vm, t->fn->name ? t->fn->name : "<async>", t->source ? t->source : "<async>", t->line, t->col);
    exec_result r = exec_block(vm, callenv, t->fn->body);
    if (r.did_throw) {
        promise_reject(t->promise, r.thrown);
        r.thrown = cs_nil();
    } else if (r.did_break) {
        promise_reject(t->promise, cs_str(vm, "break used outside of a loop"));
    } else if (r.did_continue) {
        promise_reject(t->promise, cs_str(vm, "continue used outside of a loop"));
    } else if (!r.ok) {
        if (vm->last_error) {
            cs_value msg = cs_str(vm, vm->last_error);
            promise_reject(t->promise, msg);
            cs_value_release(msg);
            free(vm->last_error);
            vm->last_error = NULL;
        } else {
            promise_reject(t->promise, cs_str(vm, "async task failed"));
        }
    } else if (r.did_return) {
        promise_fulfill(t->promise, r.ret);
    } else {
        promise_fulfill(t->promise, cs_nil());
    }
    cs_value_release(r.ret);
    cs_value_release(r.thrown);
    vm_frames_pop(vm);
    env_decref(callenv);

task_cleanup:
    for (int i = 0; i < t->argc; i++) cs_value_release(t->argv[i]);
    free(t->argv);
    t->bound_env = NULL;
    if (t->fn) {
        t->fn->ref--;
        if (t->fn->ref <= 0) {
            env_decref(t->fn->closure);
            free(t->fn);
        }
    }
    promise_decref(t->promise);
    free(t);
    return 1;
}

static int scheduler_wait_and_run(cs_vm* vm, ast* e, int* ok) {
    if (!vm) return 0;
    if (scheduler_run_due_timers(vm)) return 1;
    if (scheduler_run_one_task(vm, e, ok)) return 1;

    if (vm->pending_io_count > 0) {
        int timeout = 100;
        if (vm->timers) {
            uint64_t now = get_time_ms();
            uint64_t due = vm->timers->due_ms;
            if (due <= now) timeout = 0;
            else {
                uint64_t delta = due - now;
                if (delta < (uint64_t)timeout) timeout = (int)delta;
            }
        }
        cs_poll_pending_io(vm, timeout);
        return 1;
    }

    if (vm->timers) {
        uint64_t now = get_time_ms();
        uint64_t due = vm->timers->due_ms;
        if (due > now) {
            uint64_t delta = due - now;
#if !defined(_WIN32)
            struct timespec ts;
            ts.tv_sec = (time_t)(delta / 1000);
            ts.tv_nsec = (long)((delta % 1000) * 1000000L);
            (void)nanosleep(&ts, NULL);
#else
            Sleep((DWORD)delta);
#endif
        }
        return scheduler_run_due_timers(vm);
    }

    return 0;
}

static cs_value await_promise(cs_vm* vm, cs_promise_obj* p, ast* e, int* ok) {
    while (promise_is_pending(p) && *ok) {
        if (!scheduler_wait_and_run(vm, e, ok)) {
            // No tasks or timers; avoid infinite wait
            vm_set_err(vm, "await deadlock: no scheduled work", e ? e->source_name : "<await>", e ? e->line : 0, e ? e->col : 0);
            *ok = 0;
            break;
        }
        // Check timeout while waiting
        if (vm && vm->exec_timeout_ms > 0) {
            uint64_t elapsed = get_time_ms() - vm->exec_start_ms;
            if (elapsed >= vm->exec_timeout_ms) {
                char buf[128];
                snprintf(buf, sizeof(buf), "execution timeout exceeded (%llu ms)",
                         (unsigned long long)vm->exec_timeout_ms);
                vm_set_err(vm, buf, e ? e->source_name : "<await>", e ? e->line : 0, e ? e->col : 0);
                *ok = 0;
                break;
            }
        }
        // Check safety (interrupts/limits) while waiting
        if (vm) {
            vm->instruction_count++;
            if (!vm_check_safety(vm, e, ok)) break;
        }
    }

    if (!*ok) return cs_nil();
    if (p->state == 1) return cs_value_copy(p->value);
    if (p->state == 2) {
        cs_value thrown = cs_value_copy(p->value);
        vm_set_pending_throw(vm, thrown);
        *ok = 0;
        return cs_nil();
    }
    return cs_nil();
}

cs_value cs_wait_promise(cs_vm* vm, cs_value promise, int* ok) {
    if (!ok) return cs_nil();
    if (promise.type != CS_T_PROMISE) return cs_value_copy(promise);
    cs_promise_obj* p = as_promise(promise);
    return await_promise(vm, p, NULL, ok);
}

static cs_value schedule_async_call(cs_vm* vm, struct cs_func* fn, int argc, cs_value* argv, cs_env* bound_env, ast* e, int* ok) {
    cs_promise_obj* p = promise_new();
    if (!p) {
        vm_set_err(vm, "out of memory", e ? e->source_name : "<async>", e ? e->line : 0, e ? e->col : 0);
        *ok = 0;
        return cs_nil();
    }

    cs_task* t = (cs_task*)calloc(1, sizeof(cs_task));
    if (!t) {
        promise_decref(p);
        vm_set_err(vm, "out of memory", e ? e->source_name : "<async>", e ? e->line : 0, e ? e->col : 0);
        *ok = 0;
        return cs_nil();
    }

    t->argc = argc;
    t->argv = NULL;
    if (argc > 0) {
        t->argv = (cs_value*)calloc((size_t)argc, sizeof(cs_value));
        if (!t->argv) {
            free(t);
            promise_decref(p);
            vm_set_err(vm, "out of memory", e ? e->source_name : "<async>", e ? e->line : 0, e ? e->col : 0);
            *ok = 0;
            return cs_nil();
        }
        for (int i = 0; i < argc; i++) t->argv[i] = cs_value_copy(argv[i]);
    }

    fn->ref++;
    t->fn = fn;
    promise_incref(p);
    t->promise = p;
    if (bound_env) {
        env_incref(bound_env);
        t->bound_env = bound_env;
    }
    t->source = e ? e->source_name : "<async>";
    t->line = e ? e->line : 0;
    t->col = e ? e->col : 0;

    scheduler_push_task(vm, t);

    cs_value pv = make_promise_value(p);
    return pv;
}

cs_value cs_promise_new(cs_vm* vm) {
    (void)vm;
    cs_promise_obj* p = promise_new();
    if (!p) return cs_nil();
    return make_promise_value(p);
}

int cs_promise_resolve(cs_vm* vm, cs_value promise, cs_value value) {
    (void)vm;
    if (promise.type != CS_T_PROMISE) return 0;
    return promise_fulfill(as_promise(promise), value);
}

int cs_promise_reject(cs_vm* vm, cs_value promise, cs_value value) {
    (void)vm;
    if (promise.type != CS_T_PROMISE) return 0;
    return promise_reject(as_promise(promise), value);
}

int cs_promise_is_pending(cs_value promise) {
    if (promise.type != CS_T_PROMISE) return 0;
    return promise_is_pending(as_promise(promise));
}

void cs_schedule_timer(cs_vm* vm, cs_value promise, uint64_t due_ms) {
    if (!vm || promise.type != CS_T_PROMISE) return;
    cs_timer* timer = (cs_timer*)calloc(1, sizeof(cs_timer));
    if (!timer) return;
    timer->due_ms = due_ms;
    timer->promise = as_promise(promise);
    promise_incref(timer->promise);
    scheduler_add_timer(vm, timer);
}

static void vm_set_err(cs_vm* vm, const char* msg, const char* source, int line, int col) {
    free(vm->last_error);
    char buf[512];
    const char* src = source ? source : "<input>";
    snprintf(buf, sizeof(buf), "Runtime error at %s:%d:%d: %s", src, line, col, msg);
    vm->last_error = cs_strdup2(buf);
    vm_append_stacktrace(vm, &vm->last_error);
}

static void vm_clear_pending_throw(cs_vm* vm) {
    if (!vm || !vm->pending_throw) return;
    cs_value_release(vm->pending_thrown);
    vm->pending_thrown = cs_nil();
    vm->pending_throw = 0;
}

static void vm_set_pending_throw(cs_vm* vm, cs_value thrown) {
    if (!vm) { cs_value_release(thrown); return; }
    vm_clear_pending_throw(vm);
    vm->pending_throw = 1;
    vm->pending_thrown = thrown;
}

static int vm_take_pending_throw(cs_vm* vm, cs_value* out) {
    if (!vm || !vm->pending_throw) return 0;
    if (out) *out = vm->pending_thrown;
    else cs_value_release(vm->pending_thrown);
    vm->pending_thrown = cs_nil();
    vm->pending_throw = 0;
    return 1;
}

const char* cs_last_error(const cs_vm* vm) {
    return vm ? vm->last_error : NULL;
}

static void vm_report_uncaught_throw(cs_vm* vm, cs_value thrown) {
    if (!vm) return;
    char buf[256];
    const char* extra = "";
    char tmp[128];
    tmp[0] = 0;
    if (thrown.type == CS_T_STR) extra = as_str(thrown)->data;
    else if (thrown.type == CS_T_INT) { snprintf(tmp, sizeof(tmp), "%lld", (long long)thrown.as.i); extra = tmp; }
    else if (thrown.type == CS_T_BOOL) extra = thrown.as.b ? "true" : "false";
    else if (thrown.type == CS_T_NIL) extra = "nil";
    else extra = cs_type_name(thrown.type);
    snprintf(buf, sizeof(buf), "Uncaught throw: %s", extra);
    cs_error(vm, buf);
}

// ---------- string unescape ----------
static int hex_val_char(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static char* unescape_string_token(const char* tok, size_t n) {
    // tok includes quotes/backticks; minimal unescape
    if (n < 2) return cs_strdup2("");
    if (tok[0] == '`' && tok[n - 1] == '`') {
        size_t out_len = n - 2;
        char* out = (char*)malloc(out_len + 1);
        if (!out) return NULL;
        if (out_len) memcpy(out, tok + 1, out_len);
        out[out_len] = 0;
        return out;
    }
    if (tok[0] != '"' || tok[n-1] != '"') return cs_strdup2("");
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
                case 'e': out[w++] = 27; break; // ESC
                case 'x': {
                    if (i + 2 < n - 1) {
                        int hi = hex_val_char(tok[i + 1]);
                        int lo = hex_val_char(tok[i + 2]);
                        if (hi >= 0 && lo >= 0) {
                            out[w++] = (char)((hi << 4) | lo);
                            i += 2;
                            break;
                        }
                    }
                    out[w++] = 'x';
                    break;
                }
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

static int exec_take_vm_throw(cs_vm* vm, exec_result* r) {
    if (!vm || !r) return 0;
    if (vm_take_pending_throw(vm, &r->thrown)) {
        r->did_throw = 1;
        return 1;
    }
    return 0;
}

static exec_result exec_stmt(cs_vm* vm, cs_env* env, ast* s);
static exec_result exec_block(cs_vm* vm, cs_env* env, ast* b);
static cs_value eval_expr(cs_vm* vm, cs_env* env, ast* e, int* ok);

static int build_call_argv(cs_vm* vm, cs_env* env, ast** args, size_t arg_count, cs_value** out_argv, int* out_argc, int* ok, const char* src, int line, int col) {
    if (out_argv) *out_argv = NULL;
    if (out_argc) *out_argc = 0;
    if (!args || arg_count == 0) return 1;

    size_t cap = arg_count ? arg_count : 4;
    size_t cnt = 0;
    cs_value* argv = (cs_value*)calloc(cap, sizeof(cs_value));
    if (!argv) { vm_set_err(vm, "out of memory", src, line, col); *ok = 0; return 0; }

    for (size_t i = 0; i < arg_count; i++) {
        ast* a = args[i];
        if (a && a->type == N_SPREAD) {
            cs_value spread = eval_expr(vm, env, a->as.spread.expr, ok);
            if (!*ok) {
                for (size_t k = 0; k < cnt; k++) cs_value_release(argv[k]);
                free(argv);
                return 0;
            }
            if (spread.type == CS_T_NIL) { cs_value_release(spread); continue; }
            if (spread.type != CS_T_LIST) {
                cs_value_release(spread);
                for (size_t k = 0; k < cnt; k++) cs_value_release(argv[k]);
                free(argv);
                vm_set_err(vm, "spread expects list", src, line, col);
                *ok = 0;
                return 0;
            }
            cs_list_obj* l = as_list(spread);
            for (size_t j = 0; j < l->len; j++) {
                if (cnt == cap) {
                    cap *= 2;
                    cs_value* nv = (cs_value*)realloc(argv, sizeof(cs_value) * cap);
                    if (!nv) {
                        cs_value_release(spread);
                        for (size_t k = 0; k < cnt; k++) cs_value_release(argv[k]);
                        free(argv);
                        vm_set_err(vm, "out of memory", src, line, col);
                        *ok = 0;
                        return 0;
                    }
                    argv = nv;
                }
                argv[cnt++] = cs_value_copy(l->items[j]);
            }
            cs_value_release(spread);
        } else {
            if (cnt == cap) {
                cap *= 2;
                cs_value* nv = (cs_value*)realloc(argv, sizeof(cs_value) * cap);
                if (!nv) {
                    for (size_t k = 0; k < cnt; k++) cs_value_release(argv[k]);
                    free(argv);
                    vm_set_err(vm, "out of memory", src, line, col);
                    *ok = 0;
                    return 0;
                }
                argv = nv;
            }
            cs_value v = eval_expr(vm, env, a, ok);
            if (!*ok) {
                for (size_t k = 0; k < cnt; k++) cs_value_release(argv[k]);
                free(argv);
                return 0;
            }
            argv[cnt++] = v;
        }
    }

    if (out_argv) *out_argv = argv;
    if (out_argc) *out_argc = (int)cnt;
    return 1;
}

static int match_type_name(cs_vm* vm, cs_env* env, const char* name, cs_value v) {
    if (!name) return 0;
    if (strcmp(name, "nil") == 0) return v.type == CS_T_NIL;
    if (strcmp(name, "bool") == 0) return v.type == CS_T_BOOL;
    if (strcmp(name, "int") == 0) return v.type == CS_T_INT;
    if (strcmp(name, "float") == 0) return v.type == CS_T_FLOAT;
    if (strcmp(name, "string") == 0) return v.type == CS_T_STR;
    if (strcmp(name, "list") == 0) return v.type == CS_T_LIST;
    if (strcmp(name, "map") == 0) return v.type == CS_T_MAP;
    if (strcmp(name, "strbuf") == 0) return v.type == CS_T_STRBUF;
    if (strcmp(name, "range") == 0) return v.type == CS_T_RANGE;
    if (strcmp(name, "function") == 0) return v.type == CS_T_FUNC;
    if (strcmp(name, "native") == 0) return v.type == CS_T_NATIVE;
    if (strcmp(name, "promise") == 0) return v.type == CS_T_PROMISE;

    if (env) {
        cs_value tv = cs_nil();
        if (env_get(env, name, &tv)) {
            int matched = 0;
            if (tv.type == CS_T_MAP && map_is_class(tv) && v.type == CS_T_MAP) {
                cs_string* k_class = key_class();
                cs_value cls = k_class ? map_get_strkey(as_map(v), k_class)
                                       : map_get_cstr(as_map(v), "__class");
                matched = (cls.type == CS_T_MAP && vm_value_equals(cls, tv));
                cs_value_release(cls);
            } else if (tv.type == CS_T_MAP && map_is_struct(tv) && v.type == CS_T_MAP) {
                cs_string* k_struct = key_struct();
                cs_value st = k_struct ? map_get_strkey(as_map(v), k_struct)
                                       : map_get_cstr(as_map(v), "__struct");
                matched = (st.type == CS_T_MAP && vm_value_equals(st, tv));
                cs_value_release(st);
            }
            cs_value_release(tv);
            return matched;
        }
    }
    return 0;
}

static int match_pattern(cs_vm* vm, cs_env* match_env, ast* pat, cs_value mv, int* ok) {
    if (!pat) return 0;
    switch (pat->type) {
        case N_PATTERN_WILDCARD:
            return 1;
        case N_IDENT: {
            if (pat->as.ident.name && strcmp(pat->as.ident.name, "_") == 0) return 1;
            env_set_here(match_env, pat->as.ident.name, mv);
            return 1;
        }
        case N_PATTERN_TYPE: {
            if (!match_type_name(vm, match_env, pat->as.type_pattern.type_name, mv)) return 0;
            if (pat->as.type_pattern.inner) {
                return match_pattern(vm, match_env, pat->as.type_pattern.inner, mv, ok);
            }
            return 1;
        }
        case N_PATTERN_LIST: {
            if (mv.type != CS_T_LIST) return 0;
            cs_list_obj* l = as_list(mv);
            size_t count = pat->as.list_pattern.count;
            if (!l) return 0;
            if (pat->as.list_pattern.rest_name) {
                if (l->len < count) return 0;
            } else if (l->len != count) return 0;
            for (size_t j = 0; j < count; j++) {
                const char* name = pat->as.list_pattern.names[j];
                if (name && strcmp(name, "_") == 0) continue;
                cs_value item = cs_value_copy(l->items[j]);
                env_set_here(match_env, name, item);
                cs_value_release(item);
            }
            if (pat->as.list_pattern.rest_name && strcmp(pat->as.list_pattern.rest_name, "_") != 0) {
                cs_value rest = cs_list(vm);
                if (!rest.as.p) { *ok = 0; return 0; }
                for (size_t j = count; j < l->len; j++) {
                    if (cs_list_push(rest, l->items[j]) != 0) { cs_value_release(rest); *ok = 0; return 0; }
                }
                env_set_here(match_env, pat->as.list_pattern.rest_name, rest);
                cs_value_release(rest);
            }
            return 1;
        }
        case N_PATTERN_MAP: {
            if (mv.type != CS_T_MAP) return 0;
            cs_map_obj* m = as_map(mv);
            for (size_t j = 0; j < pat->as.map_pattern.count; j++) {
                const char* key = pat->as.map_pattern.keys[j];
                const char* name = pat->as.map_pattern.names[j];
                if (!map_has_cstr(m, key)) return 0;
                if (name && strcmp(name, "_") == 0) continue;
                cs_value item = map_get_cstr(m, key);
                env_set_here(match_env, name, item);
                cs_value_release(item);
            }
            if (pat->as.map_pattern.rest_name && strcmp(pat->as.map_pattern.rest_name, "_") != 0) {
                cs_value rest = cs_map(vm);
                if (!rest.as.p) { *ok = 0; return 0; }
                for (size_t j = 0; j < m->cap; j++) {
                    if (!m->entries[j].in_use) continue;
                    if (m->entries[j].key.type == CS_T_STR) {
                        const char* k = as_str(m->entries[j].key)->data;
                        if (k) {
                            int skip = 0;
                            for (size_t x = 0; x < pat->as.map_pattern.count; x++) {
                                if (strcmp(k, pat->as.map_pattern.keys[x]) == 0) { skip = 1; break; }
                            }
                            if (skip) continue;
                        }
                    }
                    if (!map_set_value(as_map(rest), m->entries[j].key, m->entries[j].val)) { cs_value_release(rest); *ok = 0; return 0; }
                }
                env_set_here(match_env, pat->as.map_pattern.rest_name, rest);
                cs_value_release(rest);
            }
            return 1;
        }
        case N_LIT_INT: {
            cs_value pv = cs_int((int64_t)pat->as.lit_int.v);
            return vm_value_equals(mv, pv);
        }
        case N_LIT_FLOAT: {
            cs_value pv = cs_float(pat->as.lit_float.v);
            return vm_value_equals(mv, pv);
        }
        case N_LIT_BOOL: {
            cs_value pv = cs_bool(pat->as.lit_bool.v);
            return vm_value_equals(mv, pv);
        }
        case N_LIT_NIL: {
            cs_value pv = cs_nil();
            return vm_value_equals(mv, pv);
        }
        case N_LIT_STR: {
            size_t n = strlen(pat->as.lit_str.s);
            char* un = unescape_string_token(pat->as.lit_str.s, n);
            if (!un) { vm_set_err(vm, "out of memory", pat->source_name, pat->line, pat->col); *ok = 0; return 0; }
            cs_value pv = cs_str_take(vm, un, (uint64_t)-1);
            int matched = vm_value_equals(mv, pv);
            cs_value_release(pv);
            return matched;
        }
        default:
            return 0;
    }
}

// Helper to bind parameters with default value support
static int bind_params_with_defaults(cs_vm* vm, cs_env* callenv, struct cs_func* fn, int argc, const cs_value* argv, int* ok) {
    // Calculate required params (those without defaults)
    size_t required = 0;
    for (size_t i = 0; i < fn->param_count; i++) {
        if (!fn->defaults || !fn->defaults[i]) required = i + 1;
    }

    if ((size_t)argc < required) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s() expects at least %zu argument(s), got %d",
                 fn->name ? fn->name : "<anon>", required, argc);
        cs_error(vm, buf);
        *ok = 0;
        return 0;
    }
    if (!fn->rest_param && (size_t)argc > fn->param_count) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s() expects at most %zu argument(s), got %d",
                 fn->name ? fn->name : "<anon>", fn->param_count, argc);
        cs_error(vm, buf);
        *ok = 0;
        return 0;
    }

    for (size_t i = 0; i < fn->param_count; i++) {
        cs_value val;
        if (i < (size_t)argc) {
            val = cs_value_copy(argv[i]);
        } else if (fn->defaults && fn->defaults[i]) {
            int eval_ok = 1;
            val = eval_expr(vm, callenv, fn->defaults[i], &eval_ok);
            if (!eval_ok || vm->pending_throw) {
                *ok = 0;
                return 0;
            }
        } else {
            val = cs_nil();
        }
        env_set_here(callenv, fn->params[i], val);
        cs_value_release(val);
    }

    if (fn->rest_param) {
        cs_value rest = cs_list(vm);
        if (!rest.as.p) {
            cs_error(vm, "out of memory");
            *ok = 0;
            return 0;
        }
        for (int i = (int)fn->param_count; i < argc; i++) {
            if (cs_list_push(rest, argv[i]) != 0) {
                cs_value_release(rest);
                cs_error(vm, "out of memory");
                *ok = 0;
                return 0;
            }
        }
        env_set_here(callenv, fn->rest_param, rest);
        cs_value_release(rest);
    }
    return 1;
}

static int get_exports_map(cs_vm* vm, cs_env* env, ast* s, cs_value* out) {
    if (out) *out = cs_nil();
    cs_value exports = cs_nil();
    if (!env_get(env, "exports", &exports)) {
        // Create a root-level exports map for non-module scripts.
        cs_env* root = env;
        while (root && root->parent) root = root->parent;
        cs_value new_exports = cs_map(vm);
        if (!new_exports.as.p) {
            vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
            return 0;
        }
        env_set_here(root ? root : env, "exports", new_exports);
        cs_value_release(new_exports);
        (void)env_get(env, "exports", &exports);
    }

    if (exports.type != CS_T_MAP) {
        cs_value_release(exports);
        vm_set_err(vm, "export target is not a map", s->source_name, s->line, s->col);
        return 0;
    }
    if (out) *out = exports;
    else cs_value_release(exports);
    return 1;
}

static cs_value eval_binop(cs_vm* vm, ast* e, cs_value a, cs_value b, int* ok) {
    int op = e->as.binop.op;

    // short-circuit done in caller for && ||
    if (op == TK_PLUS) {
        // Numeric addition (int or float)
        if ((a.type == CS_T_INT || a.type == CS_T_FLOAT) && (b.type == CS_T_INT || b.type == CS_T_FLOAT)) {
            double av = (a.type == CS_T_INT) ? (double)a.as.i : a.as.f;
            double bv = (b.type == CS_T_INT) ? (double)b.as.i : b.as.f;
            // If both are int, return int; otherwise return float
            if (a.type == CS_T_INT && b.type == CS_T_INT) {
                return cs_int(a.as.i + b.as.i);
            }
            return cs_float(av + bv);
        }
        // String concatenation
        if (a.type == CS_T_STR || b.type == CS_T_STR) {
            uint64_t prof_start = get_time_ms();
            const char* sa = (a.type == CS_T_STR) ? as_str(a)->data : NULL;
            const char* sb = (b.type == CS_T_STR) ? as_str(b)->data : NULL;

            char bufA[64], bufB[64];
            if (!sa) { 
                if (a.type == CS_T_INT) snprintf(bufA, sizeof(bufA), "%lld", (long long)a.as.i);
                else if (a.type == CS_T_FLOAT) snprintf(bufA, sizeof(bufA), "%g", a.as.f);
                else bufA[0] = 0;
                sa = bufA;
            }
            if (!sb) {
                if (b.type == CS_T_INT) snprintf(bufB, sizeof(bufB), "%lld", (long long)b.as.i);
                else if (b.type == CS_T_FLOAT) snprintf(bufB, sizeof(bufB), "%g", b.as.f);
                else bufB[0] = 0;
                sb = bufB;
            }

            size_t na = (a.type == CS_T_STR) ? as_str(a)->len : strlen(sa);
            size_t nb = (b.type == CS_T_STR) ? as_str(b)->len : strlen(sb);

            if (a.type == CS_T_STR && b.type == CS_T_STR) {
                if (na == 0) { cs_value out = cs_value_copy(b); if (vm) { vm->prof_string_ops++; vm->prof_string_ms += get_time_ms() - prof_start; } return out; }
                if (nb == 0) { cs_value out = cs_value_copy(a); if (vm) { vm->prof_string_ops++; vm->prof_string_ms += get_time_ms() - prof_start; } return out; }
            }
            char* joined = (char*)malloc(na + nb + 1);
            if (!joined) {
                vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                *ok = 0;
                if (vm) { vm->prof_string_ops++; vm->prof_string_ms += get_time_ms() - prof_start; }
                return cs_nil();
            }
            memcpy(joined, sa, na);
            memcpy(joined + na, sb, nb + 1);
            cs_value out = cs_str_take(vm, joined, (uint64_t)(na + nb));
            if (vm) { vm->prof_string_ops++; vm->prof_string_ms += get_time_ms() - prof_start; }
            return out;
        }
        vm_set_err(vm, "type error: '+' expects int/float or string", e->source_name, e->line, e->col);
        *ok = 0; return cs_nil();
    }

    if (op == TK_MINUS || op == TK_STAR || op == TK_SLASH || op == TK_PERCENT) {
        if ((a.type == CS_T_INT || a.type == CS_T_FLOAT) && (b.type == CS_T_INT || b.type == CS_T_FLOAT)) {
            double av = (a.type == CS_T_INT) ? (double)a.as.i : a.as.f;
            double bv = (b.type == CS_T_INT) ? (double)b.as.i : b.as.f;
            
            if (op == TK_SLASH) {
                if (bv == 0.0) { vm_set_err(vm, "division by zero", e->source_name, e->line, e->col); *ok=0; return cs_nil(); }
                return cs_float(av / bv);
            }
            
            if (op == TK_PERCENT) {
                // Modulo requires integers
                if (a.type != CS_T_INT || b.type != CS_T_INT) {
                    vm_set_err(vm, "type error: modulo requires int", e->source_name, e->line, e->col);
                    *ok = 0; return cs_nil();
                }
                if (b.as.i == 0) { vm_set_err(vm, "mod by zero", e->source_name, e->line, e->col); *ok=0; return cs_nil(); }
                return cs_int(a.as.i % b.as.i);
            }
            
            // If both are int, return int; otherwise return float
            if (a.type == CS_T_INT && b.type == CS_T_INT) {
                if (op == TK_MINUS) return cs_int(a.as.i - b.as.i);
                if (op == TK_STAR) return cs_int(a.as.i * b.as.i);
            }
            
            if (op == TK_MINUS) return cs_float(av - bv);
            if (op == TK_STAR) return cs_float(av * bv);
        }
        vm_set_err(vm, "type error: arithmetic expects int or float", e->source_name, e->line, e->col);
        *ok = 0; return cs_nil();
    }

    if (op == TK_EQ || op == TK_NE) {
        int eq = 0;
        if (a.type != b.type) {
            // Allow int == float comparison
            if ((a.type == CS_T_INT && b.type == CS_T_FLOAT) || (a.type == CS_T_FLOAT && b.type == CS_T_INT)) {
                double av = (a.type == CS_T_INT) ? (double)a.as.i : a.as.f;
                double bv = (b.type == CS_T_INT) ? (double)b.as.i : b.as.f;
                eq = (av == bv);
            } else {
                eq = 0;
            }
        } else if (a.type == CS_T_NIL) eq = 1;
        else if (a.type == CS_T_BOOL) eq = (a.as.b == b.as.b);
        else if (a.type == CS_T_INT) eq = (a.as.i == b.as.i);
        else if (a.type == CS_T_FLOAT) eq = (a.as.f == b.as.f);
        else if (a.type == CS_T_STR) eq = (strcmp(as_str(a)->data, as_str(b)->data) == 0);
        else eq = (a.as.p == b.as.p);

        return cs_bool((op == TK_EQ) ? eq : !eq);
    }

    if (op == TK_LT || op == TK_LE || op == TK_GT || op == TK_GE) {
        if ((a.type == CS_T_INT || a.type == CS_T_FLOAT) && (b.type == CS_T_INT || b.type == CS_T_FLOAT)) {
            double av = (a.type == CS_T_INT) ? (double)a.as.i : a.as.f;
            double bv = (b.type == CS_T_INT) ? (double)b.as.i : b.as.f;
            int r = 0;
            if (op == TK_LT) r = (av < bv);
            if (op == TK_LE) r = (av <= bv);
            if (op == TK_GT) r = (av > bv);
            if (op == TK_GE) r = (av >= bv);
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
        vm_set_err(vm, "type error: comparisons require both ints/floats or both strings", e->source_name, e->line, e->col);
        *ok = 0; return cs_nil();
    }

    vm_set_err(vm, "unknown binary operator", e->source_name, e->line, e->col);
    *ok = 0; return cs_nil();
}

static cs_value eval_expr(cs_vm* vm, cs_env* env, ast* e, int* ok) {
    if (!e) return cs_nil();
    
    // Increment instruction counter and check safety limits
    vm->instruction_count++;
    if (!vm_check_safety(vm, e, ok)) {
        return cs_nil();
    }

    switch (e->type) {
        case N_LIT_INT: return cs_int((int64_t)e->as.lit_int.v);
        case N_LIT_FLOAT: return cs_float(e->as.lit_float.v);
        case N_LIT_BOOL: return cs_bool(e->as.lit_bool.v);
        case N_LIT_NIL: return cs_nil();

        case N_LIT_STR: {
            size_t n = strlen(e->as.lit_str.s);
            char* un = unescape_string_token(e->as.lit_str.s, n);
            if (!un) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
            return cs_str_take(vm, un, (uint64_t)-1);
        }

        case N_STR_INTERP: {
            cs_strbuf_obj* b = strbuf_new();
            if (!b) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }

            for (size_t i = 0; i < e->as.str_interp.count; i++) {
                cs_value v = eval_expr(vm, env, e->as.str_interp.parts[i], ok);
                if (!*ok) { strbuf_decref(b); return cs_nil(); }

                if (v.type == CS_T_STR) {
                    cs_string* s = (cs_string*)v.as.p;
                    if (s && !strbuf_append_bytes(b, s->data, s->len)) {
                        cs_value_release(v);
                        strbuf_decref(b);
                        vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                        *ok = 0;
                        return cs_nil();
                    }
                } else {
                    char tmp[128];
                    const char* s = interp_repr(v, tmp, sizeof(tmp));
                    size_t n = strlen(s);
                    if (!strbuf_append_bytes(b, s, n)) {
                        cs_value_release(v);
                        strbuf_decref(b);
                        vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                        *ok = 0;
                        return cs_nil();
                    }
                }
                cs_value_release(v);
            }

            char* out = (char*)malloc(b->len + 1);
            if (!out) { strbuf_decref(b); vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
            memcpy(out, b->data, b->len);
            out[b->len] = 0;
            cs_value rv = cs_str_take(vm, out, (uint64_t)b->len);
            strbuf_decref(b);
            return rv;
        }

        case N_PLACEHOLDER:
            vm_set_err(vm, "placeholder '_' is only valid in pipe expressions", e->source_name, e->line, e->col);
            *ok = 0;
            return cs_nil();

        case N_FUNCLIT: {
            struct cs_func* f = (struct cs_func*)calloc(1, sizeof(struct cs_func));
            if (!f) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
            f->ref = 1;
            f->name = NULL;
            f->def_source = e->source_name;
            f->def_line = e->line;
            f->def_col = e->col;
            f->params = e->as.funclit.params;
            f->defaults = e->as.funclit.defaults;
            f->param_count = e->as.funclit.param_count;
            f->rest_param = e->as.funclit.rest_param;
            f->body = e->as.funclit.body;
            f->closure = env;
            env_incref(env);
            f->is_async = e->as.funclit.is_async;
            f->is_generator = e->as.funclit.is_generator;

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
                if (x.type == CS_T_INT) {
                    cs_value out = cs_int(-x.as.i);
                    cs_value_release(x);
                    return out;
                }
                if (x.type == CS_T_FLOAT) {
                    cs_value out = cs_float(-x.as.f);
                    cs_value_release(x);
                    return out;
                }
                vm_set_err(vm, "unary '-' expects int or float", e->source_name, e->line, e->col);
                *ok = 0;
            }
            cs_value_release(x);
            return cs_nil();
        }

        case N_AWAIT: {
            cs_value v = eval_expr(vm, env, e->as.await_expr.expr, ok);
            if (!*ok) return cs_nil();
            if (v.type != CS_T_PROMISE) return v;
            cs_promise_obj* p = as_promise(v);
            cs_value out = await_promise(vm, p, e, ok);
            cs_value_release(v);
            return out;
        }

        case N_TERNARY: {
            cs_value c = eval_expr(vm, env, e->as.ternary.cond, ok);
            if (!*ok) return cs_nil();
            int t = is_truthy(c);
            cs_value_release(c);
            if (t) return eval_expr(vm, env, e->as.ternary.then_e, ok);
            return eval_expr(vm, env, e->as.ternary.else_e, ok);
        }

        case N_PIPE: {
            cs_value left = eval_expr(vm, env, e->as.pipe.left, ok);
            if (!*ok) return cs_nil();

            ast* right = e->as.pipe.right;
            if (!right) { cs_value_release(left); return cs_nil(); }

            cs_value callee = cs_nil();
            cs_value* argv = NULL;
            int argc = 0;
            int used_placeholder = 0;

            if (right->type == N_CALL) {
                callee = eval_expr(vm, env, right->as.call.callee, ok);
                if (!*ok) { cs_value_release(left); return cs_nil(); }

                size_t cap = right->as.call.argc ? right->as.call.argc + 1 : 4;
                argv = (cs_value*)calloc(cap, sizeof(cs_value));
                if (!argv) { cs_value_release(left); cs_value_release(callee); vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }

                for (size_t i = 0; i < right->as.call.argc; i++) {
                    ast* a = right->as.call.args[i];
                    if (a && a->type == N_PLACEHOLDER) {
                        if (argc == (int)cap) {
                            cap *= 2;
                            cs_value* nv = (cs_value*)realloc(argv, sizeof(cs_value) * cap);
                            if (!nv) { cs_value_release(left); cs_value_release(callee); for (int k = 0; k < argc; k++) cs_value_release(argv[k]); free(argv); vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
                            argv = nv;
                        }
                        argv[argc++] = cs_value_copy(left);
                        used_placeholder = 1;
                    } else if (a && a->type == N_SPREAD) {
                        cs_value spread = eval_expr(vm, env, a->as.spread.expr, ok);
                        if (!*ok) { cs_value_release(left); cs_value_release(callee); for (int k = 0; k < argc; k++) cs_value_release(argv[k]); free(argv); return cs_nil(); }
                        if (spread.type == CS_T_NIL) { cs_value_release(spread); continue; }
                        if (spread.type != CS_T_LIST) {
                            cs_value_release(left); cs_value_release(callee); cs_value_release(spread); for (int k = 0; k < argc; k++) cs_value_release(argv[k]); free(argv);
                            vm_set_err(vm, "spread expects list", e->source_name, e->line, e->col); *ok = 0; return cs_nil();
                        }
                        cs_list_obj* l = as_list(spread);
                        for (size_t j = 0; j < l->len; j++) {
                            if (argc == (int)cap) {
                                cap *= 2;
                                cs_value* nv = (cs_value*)realloc(argv, sizeof(cs_value) * cap);
                                if (!nv) { cs_value_release(left); cs_value_release(callee); cs_value_release(spread); for (int k = 0; k < argc; k++) cs_value_release(argv[k]); free(argv); vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
                                argv = nv;
                            }
                            argv[argc++] = cs_value_copy(l->items[j]);
                        }
                        cs_value_release(spread);
                    } else {
                        if (argc == (int)cap) {
                            cap *= 2;
                            cs_value* nv = (cs_value*)realloc(argv, sizeof(cs_value) * cap);
                            if (!nv) { cs_value_release(left); cs_value_release(callee); for (int k = 0; k < argc; k++) cs_value_release(argv[k]); free(argv); vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
                            argv = nv;
                        }
                        cs_value v = eval_expr(vm, env, a, ok);
                        if (!*ok) { cs_value_release(left); cs_value_release(callee); for (int k = 0; k < argc; k++) cs_value_release(argv[k]); free(argv); return cs_nil(); }
                        argv[argc++] = v;
                    }
                }

                if (!used_placeholder) {
                    cs_value* nv = (cs_value*)realloc(argv, sizeof(cs_value) * ((size_t)argc + 1));
                    if (!nv) { cs_value_release(left); cs_value_release(callee); for (int k = 0; k < argc; k++) cs_value_release(argv[k]); free(argv); vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
                    argv = nv;
                    memmove(argv + 1, argv, sizeof(cs_value) * (size_t)argc);
                    argv[0] = cs_value_copy(left);
                    argc += 1;
                }
            } else {
                callee = eval_expr(vm, env, right, ok);
                if (!*ok) { cs_value_release(left); return cs_nil(); }
                argv = (cs_value*)calloc(1, sizeof(cs_value));
                if (!argv) { cs_value_release(left); cs_value_release(callee); vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
                argv[0] = cs_value_copy(left);
                argc = 1;
            }

            cs_value out = cs_nil();
            uint64_t pipe_start = get_time_ms();
            if (*ok) {
                if (callee.type == CS_T_NATIVE) {
                    cs_native* nf = as_native(callee);
                    if (nf->fn(vm, nf->userdata, argc, argv, &out) != 0) {
                        if (!vm->last_error) vm_set_err(vm, "native call failed", e->source_name, e->line, e->col);
                        *ok = 0;
                    }
                } else if (callee.type == CS_T_FUNC) {
                    struct cs_func* fn = as_func(callee);
                    if (fn->is_async) {
                        out = schedule_async_call(vm, fn, argc, argv, NULL, e, ok);
                    } else {
                    cs_env* callenv = env_new(fn->closure);
                    if (!callenv) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }
                    if (*ok) {
                        if (!bind_params_with_defaults(vm, callenv, fn, argc, argv, ok)) {
                            env_decref(callenv);
                        } else {
                            cs_value listv = cs_list(vm);
                            cs_list_obj* yl = as_list(listv);
                            if (!yl) {
                                vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                                *ok = 0;
                            } else {
                                int prev_active = vm->yield_active;
                                int prev_used = vm->yield_used;
                                cs_list_obj* prev_list = vm->yield_list;
                                vm->yield_active = 1;
                                vm->yield_used = 0;
                                vm->yield_list = yl;

                                exec_result r = exec_block(vm, callenv, fn->body);

                                int used = vm->yield_used;
                                vm->yield_active = prev_active;
                                vm->yield_used = prev_used;
                                vm->yield_list = prev_list;

                                if (r.did_throw) {
                                    vm_set_pending_throw(vm, r.thrown);
                                    r.thrown = cs_nil();
                                    *ok = 0;
                                } else if (r.did_break) {
                                    vm_set_err(vm, "break used outside of a loop", e->source_name, e->line, e->col);
                                    *ok = 0;
                                } else if (r.did_continue) {
                                    vm_set_err(vm, "continue used outside of a loop", e->source_name, e->line, e->col);
                                    *ok = 0;
                                } else if (!r.ok) {
                                    *ok = 0;
                                } else if (fn->is_generator || used) {
                                    out = cs_value_copy(listv);
                                } else if (r.did_return) {
                                    out = cs_value_copy(r.ret);
                                }
                                cs_value_release(r.ret);
                                cs_value_release(r.thrown);
                            }
                            cs_value_release(listv);
                        }
                    }
                    env_decref(callenv);
                    }
                } else if (callee.type == CS_T_MAP && map_is_class(callee)) {
                    cs_value instance = cs_map(vm);
                    if (!instance.as.p) {
                        vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                        *ok = 0;
                    } else {
                        cs_string* k_class = key_class();
                        int set_ok = k_class ? map_set_strkey(as_map(instance), k_class, callee)
                                             : map_set_cstr(as_map(instance), "__class", callee);
                        if (!set_ok) {
                            cs_value_release(instance);
                            vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                            *ok = 0;
                            goto class_done;
                        }
                    }
                    if (*ok) {
                        cs_value ctor = cs_nil();
                        cs_value owner_class = cs_nil();
                        if (class_find_method(callee, "new", &ctor, &owner_class)) {
                            if (ctor.type == CS_T_FUNC) {
                                struct cs_func* fn = as_func(ctor);
                                cs_env* callenv = env_new(fn->closure);
                                if (!callenv) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }
                                if (*ok) {
                                    env_set_here(callenv, "self", instance);
                                    cs_value super_val = cs_nil();
                                    if (owner_class.type == CS_T_MAP) {
                                        cs_string* k_parent = key_parent();
                                        super_val = k_parent ? map_get_strkey(as_map(owner_class), k_parent)
                                                             : map_get_cstr(as_map(owner_class), "__parent");
                                    }
                                    env_set_here(callenv, "super", super_val);
                                    cs_value_release(super_val);
                                    if (!bind_params_with_defaults(vm, callenv, fn, argc, argv, ok)) {
                                        env_decref(callenv);
                                    } else {
                                        exec_result r = exec_block(vm, callenv, fn->body);
                                        if (r.did_throw) {
                                            vm_set_pending_throw(vm, r.thrown);
                                            r.thrown = cs_nil();
                                            *ok = 0;
                                        } else if (r.did_break) {
                                            vm_set_err(vm, "break used outside of a loop", e->source_name, e->line, e->col);
                                            *ok = 0;
                                        } else if (r.did_continue) {
                                            vm_set_err(vm, "continue used outside of a loop", e->source_name, e->line, e->col);
                                            *ok = 0;
                                        } else if (!r.ok) {
                                            *ok = 0;
                                        }
                                        cs_value_release(r.ret);
                                        cs_value_release(r.thrown);
                                    }
                                }
                                env_decref(callenv);
                            }
                            cs_value_release(ctor);
                            cs_value_release(owner_class);
                        }
                        if (*ok) out = cs_value_copy(instance);
                        cs_value_release(instance);
                    }
class_done:
                } else if (callee.type == CS_T_MAP && map_is_struct(callee)) {
                    cs_value fields_v = cs_nil();
                    cs_value defaults_v = cs_nil();
                    cs_string* k_fields = key_fields();
                    cs_string* k_defaults = key_defaults();
                    fields_v = k_fields ? map_get_strkey(as_map(callee), k_fields)
                                        : map_get_cstr(as_map(callee), "__fields");
                    defaults_v = k_defaults ? map_get_strkey(as_map(callee), k_defaults)
                                            : map_get_cstr(as_map(callee), "__defaults");
                    if (fields_v.type != CS_T_LIST || defaults_v.type != CS_T_LIST) {
                        vm_set_err(vm, "invalid struct metadata", e->source_name, e->line, e->col);
                        *ok = 0;
                    } else {
                        cs_list_obj* fl = as_list(fields_v);
                        cs_list_obj* dl = as_list(defaults_v);
                        size_t field_count = fl ? fl->len : 0;
                        if ((size_t)argc > field_count) {
                            vm_set_err(vm, "too many arguments for struct", e->source_name, e->line, e->col);
                            *ok = 0;
                        } else {
                            cs_value instance = cs_map(vm);
                            if (!instance.as.p) {
                                vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                                *ok = 0;
                            } else {
                                cs_string* k_struct = key_struct();
                                int set_ok = k_struct ? map_set_strkey(as_map(instance), k_struct, callee)
                                                      : map_set_cstr(as_map(instance), "__struct", callee);
                                if (!set_ok) {
                                    cs_value_release(instance);
                                    vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                                    *ok = 0;
                                    goto struct_done;
                                }
                            }
                            if (*ok) {
                                cs_map_obj* im = as_map(instance);
                                for (size_t i = 0; i < field_count; i++) {
                                    cs_value namev = list_get(fl, (int64_t)i);
                                    cs_value val = cs_nil();
                                    int val_needs_release = 0;
                                    if ((int)i < argc) {
                                        val = argv[i];
                                    } else if (dl && i < dl->len) {
                                        val = list_get(dl, (int64_t)i);
                                        val_needs_release = 1;
                                    }
                                    if (!map_set_value(im, namev, val)) {
                                        if (val_needs_release) cs_value_release(val);
                                        cs_value_release(namev);
                                        cs_value_release(instance);
                                        vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                                        *ok = 0;
                                        goto struct_done;
                                    }
                                    if (val_needs_release) cs_value_release(val);
                                    cs_value_release(namev);
                                }
                                out = cs_value_copy(instance);
                                cs_value_release(instance);
                            }
                        }
                    }
struct_done:
                    cs_value_release(fields_v);
                    cs_value_release(defaults_v);
                } else {
                    vm_set_err(vm, "attempted to call non-function", e->source_name, e->line, e->col);
                    *ok = 0;
                }
            }

            if (vm) { vm->prof_pipe_ops++; vm->prof_pipe_ms += get_time_ms() - pipe_start; }

            for (int i = 0; i < argc; i++) cs_value_release(argv[i]);
            free(argv);
            cs_value_release(callee);
            cs_value_release(left);
            return out;
        }

        case N_MATCH: {
            cs_value mv = eval_expr(vm, env, e->as.match_expr.expr, ok);
            if (!*ok) return cs_nil();

            uint64_t match_start = get_time_ms();

            for (size_t i = 0; i < e->as.match_expr.case_count; i++) {
                cs_env* match_env = env_new(env);
                if (!match_env) { cs_value_release(mv); vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }

                int matched = match_pattern(vm, match_env, e->as.match_expr.case_patterns[i], mv, ok);
                if (!*ok) { env_decref(match_env); cs_value_release(mv); if (vm) { vm->prof_match_ops++; vm->prof_match_ms += get_time_ms() - match_start; } return cs_nil(); }

                if (matched && e->as.match_expr.case_guards[i]) {
                    cs_value gv = eval_expr(vm, match_env, e->as.match_expr.case_guards[i], ok);
                    if (!*ok) { env_decref(match_env); cs_value_release(mv); if (vm) { vm->prof_match_ops++; vm->prof_match_ms += get_time_ms() - match_start; } return cs_nil(); }
                    matched = is_truthy(gv);
                    cs_value_release(gv);
                }

                if (matched) {
                    cs_value out = eval_expr(vm, match_env, e->as.match_expr.case_values[i], ok);
                    env_decref(match_env);
                    cs_value_release(mv);
                    if (vm) { vm->prof_match_ops++; vm->prof_match_ms += get_time_ms() - match_start; }
                    return out;
                }

                env_decref(match_env);
            }

            cs_value_release(mv);
            if (e->as.match_expr.default_expr) {
                cs_value out = eval_expr(vm, env, e->as.match_expr.default_expr, ok);
                if (vm) { vm->prof_match_ops++; vm->prof_match_ms += get_time_ms() - match_start; }
                return out;
            }
            if (vm) { vm->prof_match_ops++; vm->prof_match_ms += get_time_ms() - match_start; }
            return cs_nil();
        }

        case N_RANGE: {
            cs_value start = eval_expr(vm, env, e->as.range.left, ok);
            if (!*ok) return cs_nil();
            cs_value end = eval_expr(vm, env, e->as.range.right, ok);
            if (!*ok) { cs_value_release(start); return cs_nil(); }

            if (!((start.type == CS_T_INT || start.type == CS_T_FLOAT) &&
                  (end.type == CS_T_INT || end.type == CS_T_FLOAT))) {
                cs_value_release(start);
                cs_value_release(end);
                vm_set_err(vm, "range expects int or float", e->source_name, e->line, e->col);
                *ok = 0;
                return cs_nil();
            }

            int64_t s = (start.type == CS_T_INT) ? start.as.i : (int64_t)start.as.f;
            int64_t e_val = (end.type == CS_T_INT) ? end.as.i : (int64_t)end.as.f;

            cs_value_release(start);
            cs_value_release(end);

            int64_t step = (s <= e_val) ? 1 : -1;
            cs_range_obj* r = range_new(s, e_val, step, e->as.range.inclusive);
            if (!r) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
            cs_value rv; rv.type = CS_T_RANGE; rv.as.p = r;
            return rv;
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
            if (op == TK_QQ) {
                cs_value left = eval_expr(vm, env, e->as.binop.left, ok);
                if (!*ok) return cs_nil();
                if (left.type != CS_T_NIL) return left;
                cs_value_release(left);
                return eval_expr(vm, env, e->as.binop.right, ok);
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

        case N_LISTLIT: {
            cs_value lv = cs_list(vm);
            if (!lv.as.p) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
            cs_list_obj* l = as_list(lv);
            for (size_t i = 0; i < e->as.listlit.count; i++) {
                ast* item = e->as.listlit.items[i];
                if (item && item->type == N_SPREAD) {
                    cs_value spread = eval_expr(vm, env, item->as.spread.expr, ok);
                    if (!*ok) { cs_value_release(lv); return cs_nil(); }
                    if (spread.type == CS_T_NIL) {
                        cs_value_release(spread);
                        continue;
                    }
                    if (spread.type != CS_T_LIST) {
                        cs_value_release(spread);
                        cs_value_release(lv);
                        vm_set_err(vm, "spread expects list", e->source_name, e->line, e->col);
                        *ok = 0;
                        return cs_nil();
                    }
                    cs_list_obj* sl = as_list(spread);
                    for (size_t j = 0; j < sl->len; j++) {
                        if (!list_push(l, sl->items[j])) {
                            cs_value_release(spread);
                            cs_value_release(lv);
                            vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                            *ok = 0;
                            return cs_nil();
                        }
                    }
                    cs_value_release(spread);
                } else {
                    cs_value v = eval_expr(vm, env, item, ok);
                    if (!*ok) { cs_value_release(lv); return cs_nil(); }
                    if (!list_push(l, v)) {
                        cs_value_release(v);
                        cs_value_release(lv);
                        vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                        *ok = 0;
                        return cs_nil();
                    }
                    cs_value_release(v);
                }
            }
            return lv;
        }

        case N_MAPLIT: {
            cs_value mv = cs_map(vm);
            if (!mv.as.p) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; return cs_nil(); }
            cs_map_obj* m = as_map(mv);
            for (size_t i = 0; i < e->as.maplit.count; i++) {
                ast* key = e->as.maplit.keys[i];
                if (key && key->type == N_SPREAD) {
                    cs_value spread = eval_expr(vm, env, key->as.spread.expr, ok);
                    if (!*ok) { cs_value_release(mv); return cs_nil(); }
                    if (spread.type == CS_T_NIL) {
                        cs_value_release(spread);
                        continue;
                    }
                    if (spread.type != CS_T_MAP) {
                        cs_value_release(spread);
                        cs_value_release(mv);
                        vm_set_err(vm, "spread expects map", e->source_name, e->line, e->col);
                        *ok = 0;
                        return cs_nil();
                    }
                    cs_map_obj* sm = as_map(spread);
                    for (size_t j = 0; j < sm->cap; j++) {
                        if (!sm->entries[j].in_use) continue;
                        if (!map_set_value(m, sm->entries[j].key, sm->entries[j].val)) {
                            cs_value_release(spread);
                            cs_value_release(mv);
                            vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                            *ok = 0;
                            return cs_nil();
                        }
                    }
                    cs_value_release(spread);
                } else {
                    cs_value k = eval_expr(vm, env, key, ok);
                    if (!*ok) { cs_value_release(mv); return cs_nil(); }
                    cs_value v = eval_expr(vm, env, e->as.maplit.vals[i], ok);
                    if (!*ok) { cs_value_release(k); cs_value_release(mv); return cs_nil(); }
                    if (!map_set_value(m, k, v)) {
                        cs_value_release(k);
                        cs_value_release(v);
                        cs_value_release(mv);
                        vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                        *ok = 0;
                        return cs_nil();
                    }
                    cs_value_release(k);
                    cs_value_release(v);
                }
            }
            return mv;
        }

        case N_INDEX: {
            cs_value target = eval_expr(vm, env, e->as.index.target, ok);
            if (!*ok) return cs_nil();
            cs_value index = eval_expr(vm, env, e->as.index.index, ok);
            if (!*ok) { cs_value_release(target); return cs_nil(); }

            cs_value out = cs_nil();
            if (target.type == CS_T_LIST && index.type == CS_T_INT) {
                out = list_get(as_list(target), index.as.i);
            } else if (target.type == CS_T_MAP) {
                out = map_get_value(as_map(target), index);
            } else {
                vm_set_err(vm, "indexing expects list[int] or map[key]", e->source_name, e->line, e->col);
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
                out = map_get_cstr(as_map(target), e->as.getfield.field);
            } else {
                vm_set_err(vm, "field access expects map", e->source_name, e->line, e->col);
                *ok = 0;
            }
            cs_value_release(target);
            return out;
        }

        case N_OPTGETFIELD: {
            cs_value target = eval_expr(vm, env, e->as.getfield.target, ok);
            if (!*ok) return cs_nil();

            uint64_t opt_start = get_time_ms();

            if (target.type == CS_T_NIL) {
                cs_value_release(target);
                if (vm) { vm->prof_optchain_ops++; vm->prof_optchain_ms += get_time_ms() - opt_start; }
                return cs_nil();
            }

            cs_value out = cs_nil();
            if (target.type == CS_T_MAP) {
                out = map_get_cstr(as_map(target), e->as.getfield.field);
            } else {
                vm_set_err(vm, "field access expects map", e->source_name, e->line, e->col);
                *ok = 0;
            }
            cs_value_release(target);
            if (vm) { vm->prof_optchain_ops++; vm->prof_optchain_ms += get_time_ms() - opt_start; }
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
                                int argc = 0;
                                cs_value* argv = NULL;
                                if (!build_call_argv(vm, env, e->as.call.args, e->as.call.argc, &argv, &argc, ok, e->source_name, e->line, e->col)) {
                                    cs_value_release(f);
                                    return cs_nil();
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
                                        if (fn->is_async) {
                                            out = schedule_async_call(vm, fn, argc, argv, NULL, e, ok);
                                        } else {
                                            cs_env* callenv = env_new(fn->closure);
                                            if (!callenv) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }
                                            if (*ok) {
                                                if (!bind_params_with_defaults(vm, callenv, fn, argc, argv, ok)) {
                                                    env_decref(callenv);
                                                } else {
                                                    vm_frames_push(vm, name, e->source_name, e->line, e->col);
                                                    exec_result r = exec_block(vm, callenv, fn->body);
                                                    if (r.did_throw) {
                                                        vm_set_pending_throw(vm, r.thrown);
                                                        r.thrown = cs_nil();
                                                        *ok = 0;
                                                    } else if (r.did_break) {
                                                        vm_set_err(vm, "break used outside of a loop", e->source_name, e->line, e->col);
                                                        *ok = 0;
                                                    } else if (r.did_continue) {
                                                        vm_set_err(vm, "continue used outside of a loop", e->source_name, e->line, e->col);
                                                        *ok = 0;
                                                    } else if (!r.ok) {
                                                        *ok = 0;
                                                    } else if (r.did_return) {
                                                        out = cs_value_copy(r.ret);
                                                    }
                                                    cs_value_release(r.ret);
                                                    cs_value_release(r.thrown);
                                                    if (!r.did_throw) vm_frames_pop(vm);
                                                }
                                            }
                                            env_decref(callenv);
                                        }
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

                // Build argv for method call args.
                int argc0 = 0;
                cs_value* argv0 = NULL;
                if (!build_call_argv(vm, env, e->as.call.args, e->as.call.argc, &argv0, &argc0, ok, e->source_name, e->line, e->col)) {
                    cs_value_release(self);
                    return cs_nil();
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
                    } else if (self.type == CS_T_MAP) {
                        cs_value f = cs_nil();
                        cs_value owner_class = cs_nil();
                        int from_class = 0;
                        if (map_is_class(self)) {
                            if (!class_find_method(self, field, &f, &owner_class)) {
                                vm_set_err(vm, "unknown class method", e->source_name, e->line, e->col);
                                *ok = 0;
                            } else {
                                from_class = 1;
                            }
                        } else {
                            cs_map_obj* sm = as_map(self);
                            if (map_has_cstr(sm, field)) {
                                f = map_get_cstr(sm, field);
                            } else {
                                cs_string* k_class = key_class();
                                cs_value cls = k_class ? map_get_strkey(sm, k_class)
                                                       : map_get_cstr(sm, "__class");
                                if (map_is_class(cls) && class_find_method(cls, field, &f, &owner_class)) {
                                    from_class = 1;
                                }
                                cs_value_release(cls);
                            }
                        }

                        if (*ok) {
                            if (f.type == CS_T_NATIVE) {
                                cs_native* nf = as_native(f);
                                vm_frames_push(vm, field, e->source_name, e->line, e->col);
                                if (nf->fn(vm, nf->userdata, argc0, argv0, &out) != 0) {
                                    if (!vm->last_error) vm_set_err(vm, "native call failed", e->source_name, e->line, e->col);
                                    *ok = 0;
                                }
                                vm_frames_pop(vm);
                            } else if (f.type == CS_T_FUNC) {
                                struct cs_func* fn = as_func(f);
                                if (fn->is_async) {
                                    cs_env* callenv = env_new(fn->closure);
                                    if (!callenv) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }
                                    if (*ok && from_class) {
                                        cs_value self_val = cs_nil();
                                        if (map_is_class(self)) {
                                            if (!env_get(env, "self", &self_val)) {
                                                vm_set_err(vm, "super used outside of method", e->source_name, e->line, e->col);
                                                *ok = 0;
                                            }
                                        } else {
                                            self_val = cs_value_copy(self);
                                        }
                                        if (*ok) {
                                            env_set_here(callenv, "self", self_val);
                                            cs_value_release(self_val);
                                            cs_value super_val = cs_nil();
                                            if (owner_class.type == CS_T_MAP) {
                                                cs_string* k_parent = key_parent();
                                                super_val = k_parent ? map_get_strkey(as_map(owner_class), k_parent)
                                                                     : map_get_cstr(as_map(owner_class), "__parent");
                                            }
                                            env_set_here(callenv, "super", super_val);
                                            cs_value_release(super_val);
                                        }
                                    }
                                    if (*ok) {
                                        out = schedule_async_call(vm, fn, argc0, argv0, callenv, e, ok);
                                    }
                                    if (callenv) env_decref(callenv);
                                } else {
                                    cs_env* callenv = env_new(fn->closure);
                                    if (!callenv) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }

                                    if (*ok) {
                                        if (from_class) {
                                            cs_value self_val = cs_nil();
                                            if (map_is_class(self)) {
                                                if (!env_get(env, "self", &self_val)) {
                                                    vm_set_err(vm, "super used outside of method", e->source_name, e->line, e->col);
                                                    *ok = 0;
                                                }
                                            } else {
                                                self_val = cs_value_copy(self);
                                            }
                                            if (*ok) {
                                                env_set_here(callenv, "self", self_val);
                                                cs_value_release(self_val);
                                                cs_value super_val = cs_nil();
                                                if (owner_class.type == CS_T_MAP) {
                                                    cs_string* k_parent = key_parent();
                                                    super_val = k_parent ? map_get_strkey(as_map(owner_class), k_parent)
                                                                         : map_get_cstr(as_map(owner_class), "__parent");
                                                }
                                                env_set_here(callenv, "super", super_val);
                                                cs_value_release(super_val);
                                            }
                                        }

                                        if (*ok) {
                                            if (!bind_params_with_defaults(vm, callenv, fn, argc0, argv0, ok)) {
                                                env_decref(callenv);
                                            } else {
                                                vm_frames_push(vm, field, e->source_name, e->line, e->col);
                                                exec_result r = exec_block(vm, callenv, fn->body);
                                                if (r.did_throw) {
                                                    vm_set_pending_throw(vm, r.thrown);
                                                    r.thrown = cs_nil();
                                                    *ok = 0;
                                                } else if (r.did_break) {
                                                    vm_set_err(vm, "break used outside of a loop", e->source_name, e->line, e->col);
                                                    *ok = 0;
                                                } else if (r.did_continue) {
                                                    vm_set_err(vm, "continue used outside of a loop", e->source_name, e->line, e->col);
                                                    *ok = 0;
                                                } else if (!r.ok) {
                                                    *ok = 0;
                                                } else if (r.did_return) {
                                                    out = cs_value_copy(r.ret);
                                                }
                                                cs_value_release(r.ret);
                                                cs_value_release(r.thrown);
                                                if (!r.did_throw) vm_frames_pop(vm);
                                            }
                                        }
                                    }
                                    env_decref(callenv);
                                }
                            } else {
                                vm_set_err(vm, "attempted to call non-function", e->source_name, e->line, e->col);
                                *ok = 0;
                            }
                        }
                        cs_value_release(f);
                        cs_value_release(owner_class);
                    } else {
                        vm_set_err(vm, "method call expects map or strbuf", e->source_name, e->line, e->col);
                        *ok = 0;
                    }
                }

                if (argv0) { for (int i = 0; i < argc0; i++) cs_value_release(argv0[i]); free(argv0); }
                cs_value_release(self);
                return out;
            }

            // Optional chaining call: `obj?.method(a,b)` where callee is OPTGETFIELD.
            if (e->as.call.callee && e->as.call.callee->type == N_OPTGETFIELD) {
                ast* gf = e->as.call.callee;
                const char* field = gf->as.getfield.field;

                cs_value self = eval_expr(vm, env, gf->as.getfield.target, ok);
                if (!*ok) return cs_nil();

                uint64_t opt_call_start = get_time_ms();

                if (self.type == CS_T_NIL) {
                    cs_value_release(self);
                    if (vm) { vm->prof_optchain_ops++; vm->prof_optchain_ms += get_time_ms() - opt_call_start; }
                    return cs_nil();
                }

                int argc0 = 0;
                cs_value* argv0 = NULL;
                if (!build_call_argv(vm, env, e->as.call.args, e->as.call.argc, &argv0, &argc0, ok, e->source_name, e->line, e->col)) {
                    cs_value_release(self);
                    if (vm) { vm->prof_optchain_ops++; vm->prof_optchain_ms += get_time_ms() - opt_call_start; }
                    return cs_nil();
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
                    } else if (self.type == CS_T_MAP) {
                        cs_value f = cs_nil();
                        cs_value owner_class = cs_nil();
                        int from_class = 0;
                        if (map_is_class(self)) {
                            if (!class_find_method(self, field, &f, &owner_class)) {
                                vm_set_err(vm, "unknown class method", e->source_name, e->line, e->col);
                                *ok = 0;
                            } else {
                                from_class = 1;
                            }
                        } else {
                            cs_map_obj* sm = as_map(self);
                            if (map_has_cstr(sm, field)) {
                                f = map_get_cstr(sm, field);
                            } else {
                                cs_string* k_class = key_class();
                                cs_value cls = k_class ? map_get_strkey(sm, k_class)
                                                       : map_get_cstr(sm, "__class");
                                if (map_is_class(cls) && class_find_method(cls, field, &f, &owner_class)) {
                                    from_class = 1;
                                }
                                cs_value_release(cls);
                            }
                        }

                        if (*ok) {
                            if (f.type == CS_T_NATIVE) {
                                cs_native* nf = as_native(f);
                                vm_frames_push(vm, field, e->source_name, e->line, e->col);
                                if (nf->fn(vm, nf->userdata, argc0, argv0, &out) != 0) {
                                    if (!vm->last_error) vm_set_err(vm, "native call failed", e->source_name, e->line, e->col);
                                    *ok = 0;
                                }
                                vm_frames_pop(vm);
                            } else if (f.type == CS_T_FUNC) {
                                struct cs_func* fn = as_func(f);
                                if (fn->is_async) {
                                    cs_env* callenv = env_new(fn->closure);
                                    if (!callenv) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }
                                    if (*ok && from_class) {
                                        cs_value self_val = cs_nil();
                                        if (map_is_class(self)) {
                                            if (!env_get(env, "self", &self_val)) {
                                                vm_set_err(vm, "super used outside of method", e->source_name, e->line, e->col);
                                                *ok = 0;
                                            }
                                        } else {
                                            self_val = cs_value_copy(self);
                                        }
                                        if (*ok) {
                                            env_set_here(callenv, "self", self_val);
                                            cs_value_release(self_val);
                                            cs_value super_val = cs_nil();
                                            if (owner_class.type == CS_T_MAP) {
                                                cs_string* k_parent = key_parent();
                                                super_val = k_parent ? map_get_strkey(as_map(owner_class), k_parent)
                                                                     : map_get_cstr(as_map(owner_class), "__parent");
                                            }
                                            env_set_here(callenv, "super", super_val);
                                            cs_value_release(super_val);
                                        }
                                    }
                                    if (*ok) {
                                        out = schedule_async_call(vm, fn, argc0, argv0, callenv, e, ok);
                                    }
                                    if (callenv) env_decref(callenv);
                                } else {
                                    cs_env* callenv = env_new(fn->closure);
                                    if (!callenv) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }

                                    if (*ok) {
                                        if (from_class) {
                                            cs_value self_val = cs_nil();
                                            if (map_is_class(self)) {
                                                if (!env_get(env, "self", &self_val)) {
                                                    vm_set_err(vm, "super used outside of method", e->source_name, e->line, e->col);
                                                    *ok = 0;
                                                }
                                            } else {
                                                self_val = cs_value_copy(self);
                                            }
                                            if (*ok) {
                                                env_set_here(callenv, "self", self_val);
                                                cs_value_release(self_val);
                                                cs_value super_val = cs_nil();
                                                if (owner_class.type == CS_T_MAP) {
                                                    cs_string* k_parent = key_parent();
                                                    super_val = k_parent ? map_get_strkey(as_map(owner_class), k_parent)
                                                                         : map_get_cstr(as_map(owner_class), "__parent");
                                                }
                                                env_set_here(callenv, "super", super_val);
                                                cs_value_release(super_val);
                                            }
                                        }

                                        if (*ok) {
                                            if (!bind_params_with_defaults(vm, callenv, fn, argc0, argv0, ok)) {
                                                env_decref(callenv);
                                            } else {
                                                vm_frames_push(vm, field, e->source_name, e->line, e->col);
                                                exec_result r = exec_block(vm, callenv, fn->body);
                                                if (r.did_throw) {
                                                    vm_set_pending_throw(vm, r.thrown);
                                                    r.thrown = cs_nil();
                                                    *ok = 0;
                                                } else if (r.did_break) {
                                                    vm_set_err(vm, "break used outside of a loop", e->source_name, e->line, e->col);
                                                    *ok = 0;
                                                } else if (r.did_continue) {
                                                    vm_set_err(vm, "continue used outside of a loop", e->source_name, e->line, e->col);
                                                    *ok = 0;
                                                } else if (!r.ok) {
                                                    *ok = 0;
                                                } else if (r.did_return) {
                                                    out = cs_value_copy(r.ret);
                                                }
                                                cs_value_release(r.ret);
                                                cs_value_release(r.thrown);
                                                if (!r.did_throw) vm_frames_pop(vm);
                                            }
                                        }
                                    }
                                    env_decref(callenv);
                                }
                            } else {
                                vm_set_err(vm, "attempted to call non-function", e->source_name, e->line, e->col);
                                *ok = 0;
                            }
                        }
                        cs_value_release(f);
                        cs_value_release(owner_class);
                    } else {
                        vm_set_err(vm, "method call expects map or strbuf", e->source_name, e->line, e->col);
                        *ok = 0;
                    }
                }

                if (argv0) { for (int i = 0; i < argc0; i++) cs_value_release(argv0[i]); free(argv0); }
                cs_value_release(self);
                if (vm) { vm->prof_optchain_ops++; vm->prof_optchain_ms += get_time_ms() - opt_call_start; }
                return out;
            }

            cs_value callee = eval_expr(vm, env, e->as.call.callee, ok);
            if (!*ok) return cs_nil();

            int argc = 0;
            cs_value* argv = NULL;
            if (!build_call_argv(vm, env, e->as.call.args, e->as.call.argc, &argv, &argc, ok, e->source_name, e->line, e->col)) {
                cs_value_release(callee);
                return cs_nil();
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
                    if (fn->is_async) {
                        out = schedule_async_call(vm, fn, argc, argv, NULL, e, ok);
                    } else {
                    cs_env* callenv = env_new(fn->closure);
                    if (!callenv) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }

                    if (*ok) {
                        if (!bind_params_with_defaults(vm, callenv, fn, argc, argv, ok)) {
                            env_decref(callenv);
                        } else {
                            vm_frames_push(vm, call_name ? call_name : (fn->name ? fn->name : "<fn>"), e->source_name, e->line, e->col);
                            cs_value listv = cs_list(vm);
                            cs_list_obj* yl = as_list(listv);
                            int did_throw = 0;
                            if (!yl) {
                                vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                                *ok = 0;
                            } else {
                                int prev_active = vm->yield_active;
                                int prev_used = vm->yield_used;
                                cs_list_obj* prev_list = vm->yield_list;
                                vm->yield_active = 1;
                                vm->yield_used = 0;
                                vm->yield_list = yl;

                                exec_result r = exec_block(vm, callenv, fn->body);

                                int used = vm->yield_used;
                                vm->yield_active = prev_active;
                                vm->yield_used = prev_used;
                                vm->yield_list = prev_list;

                                if (r.did_throw) {
                                    vm_set_pending_throw(vm, r.thrown);
                                    r.thrown = cs_nil();
                                    *ok = 0;
                                } else if (r.did_break) {
                                    vm_set_err(vm, "break used outside of a loop", e->source_name, e->line, e->col);
                                    *ok = 0;
                                } else if (r.did_continue) {
                                    vm_set_err(vm, "continue used outside of a loop", e->source_name, e->line, e->col);
                                    *ok = 0;
                                } else if (!r.ok) {
                                    *ok = 0;
                                } else if (fn->is_generator || used) {
                                    out = cs_value_copy(listv);
                                } else if (r.did_return) {
                                    out = cs_value_copy(r.ret);
                                }
                                did_throw = r.did_throw;
                                cs_value_release(r.ret);
                                cs_value_release(r.thrown);
                            }
                            cs_value_release(listv);
                            if (!did_throw) vm_frames_pop(vm);
                        }
                    }
                    env_decref(callenv);
                    }
                } else if (callee.type == CS_T_MAP && map_is_class(callee)) {
                    cs_value instance = cs_map(vm);
                    if (!instance.as.p) {
                        vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                        *ok = 0;
                    } else {
                        cs_string* k_class = key_class();
                        int set_ok = k_class ? map_set_strkey(as_map(instance), k_class, callee)
                                             : map_set_cstr(as_map(instance), "__class", callee);
                        if (!set_ok) {
                            cs_value_release(instance);
                            vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                            *ok = 0;
                            goto class_done_call;
                        }
                        cs_value ctor = cs_nil();
                        cs_value owner_class = cs_nil();
                        if (class_find_method(callee, "new", &ctor, &owner_class)) {
                            if (ctor.type == CS_T_FUNC) {
                                struct cs_func* fn = as_func(ctor);
                                cs_env* callenv = env_new(fn->closure);
                                if (!callenv) { vm_set_err(vm, "out of memory", e->source_name, e->line, e->col); *ok = 0; }
                                if (*ok) {
                                    env_set_here(callenv, "self", instance);
                                    cs_value super_val = cs_nil();
                                    if (owner_class.type == CS_T_MAP) {
                                        cs_string* k_parent = key_parent();
                                        super_val = k_parent ? map_get_strkey(as_map(owner_class), k_parent)
                                                             : map_get_cstr(as_map(owner_class), "__parent");
                                    }
                                    env_set_here(callenv, "super", super_val);
                                    cs_value_release(super_val);
                                    if (!bind_params_with_defaults(vm, callenv, fn, argc, argv, ok)) {
                                        env_decref(callenv);
                                    } else {
                                        vm_frames_push(vm, call_name ? call_name : (fn->name ? fn->name : "<new>"), e->source_name, e->line, e->col);
                                        exec_result r = exec_block(vm, callenv, fn->body);
                                        if (r.did_throw) {
                                            vm_set_pending_throw(vm, r.thrown);
                                            r.thrown = cs_nil();
                                            *ok = 0;
                                        } else if (r.did_break) {
                                            vm_set_err(vm, "break used outside of a loop", e->source_name, e->line, e->col);
                                            *ok = 0;
                                        } else if (r.did_continue) {
                                            vm_set_err(vm, "continue used outside of a loop", e->source_name, e->line, e->col);
                                            *ok = 0;
                                        } else if (!r.ok) {
                                            *ok = 0;
                                        }
                                        cs_value_release(r.ret);
                                        cs_value_release(r.thrown);
                                        if (!r.did_throw) vm_frames_pop(vm);
                                    }
                                }
                                env_decref(callenv);
                            }
                            cs_value_release(ctor);
                            cs_value_release(owner_class);
                        }
                        if (*ok) out = cs_value_copy(instance);
                        cs_value_release(instance);
                    }
class_done_call:
                } else if (callee.type == CS_T_MAP && map_is_struct(callee)) {
                    cs_value fields_v = cs_nil();
                    cs_value defaults_v = cs_nil();
                    cs_string* k_fields = key_fields();
                    cs_string* k_defaults = key_defaults();
                    fields_v = k_fields ? map_get_strkey(as_map(callee), k_fields)
                                        : map_get_cstr(as_map(callee), "__fields");
                    defaults_v = k_defaults ? map_get_strkey(as_map(callee), k_defaults)
                                            : map_get_cstr(as_map(callee), "__defaults");
                    if (fields_v.type != CS_T_LIST || defaults_v.type != CS_T_LIST) {
                        vm_set_err(vm, "invalid struct metadata", e->source_name, e->line, e->col);
                        *ok = 0;
                    } else {
                        cs_list_obj* fl = as_list(fields_v);
                        cs_list_obj* dl = as_list(defaults_v);
                        size_t field_count = fl ? fl->len : 0;
                        if ((size_t)argc > field_count) {
                            vm_set_err(vm, "too many arguments for struct", e->source_name, e->line, e->col);
                            *ok = 0;
                        } else {
                            cs_value instance = cs_map(vm);
                            if (!instance.as.p) {
                                vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                                *ok = 0;
                            } else {
                                cs_string* k_struct = key_struct();
                                int set_ok = k_struct ? map_set_strkey(as_map(instance), k_struct, callee)
                                                      : map_set_cstr(as_map(instance), "__struct", callee);
                                if (!set_ok) {
                                    cs_value_release(instance);
                                    vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                                    *ok = 0;
                                    goto struct_done_call;
                                }
                                cs_map_obj* im = as_map(instance);
                                for (size_t i = 0; i < field_count; i++) {
                                    cs_value namev = list_get(fl, (int64_t)i);
                                    cs_value val = cs_nil();
                                    int val_needs_release = 0;
                                    if ((int)i < argc) {
                                        val = argv[i];
                                    } else if (dl && i < dl->len) {
                                        val = list_get(dl, (int64_t)i);
                                        val_needs_release = 1;
                                    }
                                    if (!map_set_value(im, namev, val)) {
                                        if (val_needs_release) cs_value_release(val);
                                        cs_value_release(namev);
                                        cs_value_release(instance);
                                        vm_set_err(vm, "out of memory", e->source_name, e->line, e->col);
                                        *ok = 0;
                                        goto struct_done_call;
                                    }
                                    if (val_needs_release) cs_value_release(val);
                                    cs_value_release(namev);
                                }
                                out = cs_value_copy(instance);
                                cs_value_release(instance);
                            }
                        }
                    }
struct_done_call:
                    cs_value_release(fields_v);
                    cs_value_release(defaults_v);
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
    r.did_break = 0;
    r.did_continue = 0;
    r.did_throw = 0;
    r.ret = cs_nil();
    r.thrown = cs_nil();
    r.ok = 1;

    if (!b || b->type != N_BLOCK) return r;

    size_t defer_cap = 0;
    size_t defer_count = 0;
    ast** defers = NULL;

    exec_result base = r;

    for (size_t i = 0; i < b->as.block.count; i++) {
        ast* stmt = b->as.block.items[i];
        if (stmt && stmt->type == N_DEFER) {
            if (defer_count == defer_cap) {
                defer_cap = defer_cap ? defer_cap * 2 : 4;
                defers = (ast**)realloc(defers, sizeof(ast*) * defer_cap);
            }
            if (!defers) {
                vm_set_err(vm, "out of memory", stmt->source_name, stmt->line, stmt->col);
                base.ok = 0;
                break;
            }
            defers[defer_count++] = stmt->as.defer_stmt.stmt;
            continue;
        }

        base = exec_stmt(vm, env, stmt);
        if (!base.ok || base.did_return || base.did_break || base.did_continue || base.did_throw) break;
    }

    for (size_t i = defer_count; i > 0; i--) {
        exec_result dr = exec_stmt(vm, env, defers[i - 1]);
        if (!dr.ok || dr.did_return || dr.did_break || dr.did_continue || dr.did_throw) {
            free(defers);
            return dr;
        }
    }

    free(defers);
    return base;
}

static exec_result exec_stmt(cs_vm* vm, cs_env* env, ast* s) {
    exec_result r;
    r.did_return = 0;
    r.did_break = 0;
    r.did_continue = 0;
    r.did_throw = 0;
    r.ret = cs_nil();
    r.thrown = cs_nil();
    r.ok = 1;
    if (!s) return r;

    switch (s->type) {
        case N_DEFER: {
            return r;
        }

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
                if (!ok) {
                    if (exec_take_vm_throw(vm, &r)) return r;
                    r.ok = 0;
                    return r;
                }
            }
            if (s->as.let_stmt.pattern) {
                if (!s->as.let_stmt.init) {
                    cs_value_release(v);
                    vm_set_err(vm, "destructuring requires initializer", s->source_name, s->line, s->col);
                    r.ok = 0;
                    return r;
                }

                ast* pat = s->as.let_stmt.pattern;
                if (pat->type == N_PATTERN_LIST) {
                    if (v.type != CS_T_LIST) {
                        cs_value_release(v);
                        vm_set_err(vm, "list destructuring expects list", s->source_name, s->line, s->col);
                        r.ok = 0;
                        return r;
                    }
                    cs_list_obj* l = as_list(v);
                    for (size_t i = 0; i < pat->as.list_pattern.count; i++) {
                        const char* name = pat->as.list_pattern.names[i];
                        if (name && strcmp(name, "_") == 0) continue;
                        cs_value item = cs_nil();
                        if (l && i < l->len) item = cs_value_copy(l->items[i]);
                        env_set_here(env, name, item);
                        cs_value_release(item);
                    }
                    if (pat->as.list_pattern.rest_name && strcmp(pat->as.list_pattern.rest_name, "_") != 0) {
                        cs_value rest = cs_list(vm);
                        if (!rest.as.p) {
                            cs_value_release(v);
                            vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                            r.ok = 0;
                            return r;
                        }
                        if (l) {
                            for (size_t i = pat->as.list_pattern.count; i < l->len; i++) {
                                if (cs_list_push(rest, l->items[i]) != 0) {
                                    cs_value_release(rest);
                                    cs_value_release(v);
                                    vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                                    r.ok = 0;
                                    return r;
                                }
                            }
                        }
                        env_set_here(env, pat->as.list_pattern.rest_name, rest);
                        cs_value_release(rest);
                    }
                } else if (pat->type == N_PATTERN_MAP) {
                    if (v.type != CS_T_MAP) {
                        cs_value_release(v);
                        vm_set_err(vm, "map destructuring expects map", s->source_name, s->line, s->col);
                        r.ok = 0;
                        return r;
                    }
                    cs_map_obj* m = as_map(v);
                    for (size_t i = 0; i < pat->as.map_pattern.count; i++) {
                        const char* key = pat->as.map_pattern.keys[i];
                        const char* name = pat->as.map_pattern.names[i];
                        if (name && strcmp(name, "_") == 0) continue;
                        cs_value item = cs_nil();
                        if (m) item = map_get_cstr(m, key);
                        env_set_here(env, name, item);
                        cs_value_release(item);
                    }
                    if (pat->as.map_pattern.rest_name && strcmp(pat->as.map_pattern.rest_name, "_") != 0) {
                        cs_value rest = cs_map(vm);
                        if (!rest.as.p) {
                            cs_value_release(v);
                            vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                            r.ok = 0;
                            return r;
                        }
                        if (m) {
                            for (size_t i = 0; i < m->cap; i++) {
                                if (!m->entries[i].in_use) continue;
                                int skip = 0;
                                if (m->entries[i].key.type == CS_T_STR) {
                                    const char* k = as_str(m->entries[i].key)->data;
                                    if (k) {
                                        for (size_t j = 0; j < pat->as.map_pattern.count; j++) {
                                            if (strcmp(k, pat->as.map_pattern.keys[j]) == 0) { skip = 1; break; }
                                        }
                                    }
                                }
                                if (skip) continue;
                                if (!map_set_value(as_map(rest), m->entries[i].key, m->entries[i].val)) {
                                    cs_value_release(rest);
                                    cs_value_release(v);
                                    vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                                    r.ok = 0;
                                    return r;
                                }
                            }
                        }
                        env_set_here(env, pat->as.map_pattern.rest_name, rest);
                        cs_value_release(rest);
                    }
                }

                cs_value_release(v);
                return r;
            }

            if (s->as.let_stmt.is_const) {
                env_set_here_ex(env, s->as.let_stmt.name, v, 1);
            } else {
                env_set_here(env, s->as.let_stmt.name, v);
            }
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

                if (env_is_const(env, s->as.assign_stmt.name)) {
                    vm_set_err(vm, "assignment to const variable", s->source_name, s->line, s->col);
                    r.ok = 0;
                    return r;
                }

                cs_value* slot = env_get_slot(env, s->as.assign_stmt.name);
                if (slot && slot->type == CS_T_STR) {
                    cs_string* base = (cs_string*)slot->as.p;
                    if (base && base->ref == 1) {
                        int ok = 1;
                        cs_value rv = eval_expr(vm, env, rhs->as.binop.right, &ok);
                        if (!ok) {
                            if (exec_take_vm_throw(vm, &r)) return r;
                            r.ok = 0;
                            return r;
                        }

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
            if (!ok) {
                if (exec_take_vm_throw(vm, &r)) return r;
                r.ok = 0;
                return r;
            }
            int ar = env_assign_existing(env, s->as.assign_stmt.name, v);
            if (ar <= 0) {
                cs_value_release(v);
                if (ar < 0) {
                    vm_set_err(vm, "assignment to const variable", s->source_name, s->line, s->col);
                } else {
                    vm_set_err(vm, "assignment to undefined variable", s->source_name, s->line, s->col);
                }
                r.ok = 0;
                return r;
            }
            cs_value_release(v);
            return r;
        }

        case N_SETINDEX: {
            int ok = 1;
            cs_value target = eval_expr(vm, env, s->as.setindex_stmt.target, &ok);
            if (!ok) {
                if (exec_take_vm_throw(vm, &r)) return r;
                r.ok = 0;
                return r;
            }
            cs_value index = eval_expr(vm, env, s->as.setindex_stmt.index, &ok);
            if (!ok) {
                cs_value_release(target);
                if (exec_take_vm_throw(vm, &r)) return r;
                r.ok = 0;
                return r;
            }
            cs_value value = cs_nil();
            if (s->as.setindex_stmt.op == TK_ASSIGN) {
                value = eval_expr(vm, env, s->as.setindex_stmt.value, &ok);
                if (!ok) {
                    cs_value_release(target);
                    cs_value_release(index);
                    if (exec_take_vm_throw(vm, &r)) return r;
                    r.ok = 0;
                    return r;
                }
            } else {
                cs_value rhs = eval_expr(vm, env, s->as.setindex_stmt.value, &ok);
                if (!ok) {
                    cs_value_release(target);
                    cs_value_release(index);
                    if (exec_take_vm_throw(vm, &r)) return r;
                    r.ok = 0;
                    return r;
                }

                cs_value current = cs_nil();
                if (target.type == CS_T_LIST && index.type == CS_T_INT) {
                    current = list_get(as_list(target), index.as.i);
                } else if (target.type == CS_T_MAP) {
                    current = map_get_value(as_map(target), index);
                } else {
                    cs_value_release(rhs);
                    cs_value_release(target);
                    cs_value_release(index);
                    vm_set_err(vm, "index assignment expects list[int] or map[key]", s->source_name, s->line, s->col);
                    r.ok = 0;
                    return r;
                }

                ast tmp;
                memset(&tmp, 0, sizeof(tmp));
                tmp.type = N_BINOP;
                tmp.source_name = s->source_name;
                tmp.line = s->line;
                tmp.col = s->col;
                tmp.as.binop.op = s->as.setindex_stmt.op == TK_PLUSEQ ? TK_PLUS :
                                 s->as.setindex_stmt.op == TK_MINUSEQ ? TK_MINUS :
                                 s->as.setindex_stmt.op == TK_STAREQ ? TK_STAR :
                                 TK_SLASH;
                value = eval_binop(vm, &tmp, current, rhs, &ok);
                cs_value_release(current);
                cs_value_release(rhs);
                if (!ok) {
                    cs_value_release(target);
                    cs_value_release(index);
                    r.ok = 0;
                    return r;
                }
            }

            int wrote = 0;
            if (target.type == CS_T_LIST && index.type == CS_T_INT) {
                wrote = list_set(as_list(target), index.as.i, value);
                if (!wrote) vm_set_err(vm, "list index assignment failed", s->source_name, s->line, s->col);
            } else if (target.type == CS_T_MAP) {
                wrote = map_set_value(as_map(target), index, value);
                if (!wrote) vm_set_err(vm, "map assignment failed", s->source_name, s->line, s->col);
            } else {
                vm_set_err(vm, "index assignment expects list[int] or map[key]", s->source_name, s->line, s->col);
            }

            cs_value_release(target);
            cs_value_release(index);
            cs_value_release(value);
            if (!wrote) { r.ok = 0; }
            return r;
        }

        case N_SWITCH: {
            int ok = 1;
            cs_value sw = eval_expr(vm, env, s->as.switch_stmt.expr, &ok);
            if (!ok) {
                if (exec_take_vm_throw(vm, &r)) return r;
                r.ok = 0;
                return r;
            }

            int match_index = -1;
            int default_index = -1;
            cs_env* match_env = NULL;

            for (size_t i = 0; i < s->as.switch_stmt.case_count; i++) {
                unsigned char kind = s->as.switch_stmt.case_kinds ? s->as.switch_stmt.case_kinds[i] : 0;
                if (kind == 2) {
                    default_index = (int)i;
                    continue;
                }

                if (kind == 1) {
                    cs_env* cand_env = env_new(env);
                    if (!cand_env) { cs_value_release(sw); vm_set_err(vm, "out of memory", s->source_name, s->line, s->col); r.ok = 0; return r; }
                    int matched = match_pattern(vm, cand_env, s->as.switch_stmt.case_patterns[i], sw, &ok);
                    if (!ok) {
                        env_decref(cand_env);
                        cs_value_release(sw);
                        if (exec_take_vm_throw(vm, &r)) return r;
                        r.ok = 0;
                        return r;
                    }
                    if (matched) {
                        match_index = (int)i;
                        match_env = cand_env;
                        break;
                    }
                    env_decref(cand_env);
                } else {
                    ast* cexpr = s->as.switch_stmt.case_exprs[i];
                    cs_value cv = eval_expr(vm, env, cexpr, &ok);
                    if (!ok) {
                        cs_value_release(sw);
                        if (exec_take_vm_throw(vm, &r)) return r;
                        r.ok = 0;
                        return r;
                    }

                    if (vm_value_equals(sw, cv)) {
                        match_index = (int)i;
                        cs_value_release(cv);
                        break;
                    }
                    cs_value_release(cv);
                }
            }

            int start = match_index >= 0 ? match_index : default_index;
            cs_env* exec_env = match_env ? match_env : env;
            if (start >= 0) {
                for (size_t i = (size_t)start; i < s->as.switch_stmt.case_count; i++) {
                    r = exec_stmt(vm, exec_env, s->as.switch_stmt.case_blocks[i]);
                    if (r.did_break) {
                        r.did_break = 0; // break exits switch
                        break;
                    }
                    if (r.did_return || r.did_continue || r.did_throw || !r.ok) break;
                }
            }

            if (match_env) env_decref(match_env);
            cs_value_release(sw);
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
            f->defaults = s->as.fndef.defaults;
            f->param_count = s->as.fndef.param_count;
            f->rest_param = s->as.fndef.rest_param;
            f->body = s->as.fndef.body;
            f->closure = env;
            f->is_async = s->as.fndef.is_async;
            f->is_generator = s->as.fndef.is_generator;
            env_incref(env);

            cs_value fv; fv.type = CS_T_FUNC; fv.as.p = f;
            env_set_here(env, s->as.fndef.name, fv);
            cs_value_release(fv);
            return r;
        }

        case N_CLASS: {
            cs_value cv = cs_map(vm);
            if (!cv.as.p) { vm_set_err(vm, "out of memory", s->source_name, s->line, s->col); r.ok = 0; return r; }
            cs_map_obj* cm = as_map(cv);

            cs_value is_cls = cs_bool(1);
            {
                cs_string* k_is_class = key_is_class();
                int set_ok = k_is_class ? map_set_strkey(cm, k_is_class, is_cls)
                                        : map_set_cstr(cm, "__is_class", is_cls);
                if (!set_ok) {
                    cs_value_release(cv);
                    vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                    r.ok = 0;
                    return r;
                }
            }

            cs_value namev = cs_str(vm, s->as.class_stmt.name ? s->as.class_stmt.name : "<class>");
            map_set_cstr(cm, "__name", namev);
            cs_value_release(namev);

            if (s->as.class_stmt.parent) {
                cs_value parent = cs_nil();
                if (!env_get(env, s->as.class_stmt.parent, &parent)) {
                    cs_value_release(cv);
                    vm_set_err(vm, "unknown parent class", s->source_name, s->line, s->col);
                    r.ok = 0;
                    return r;
                }
                if (!map_is_class(parent)) {
                    cs_value_release(parent);
                    cs_value_release(cv);
                    vm_set_err(vm, "parent is not a class", s->source_name, s->line, s->col);
                    r.ok = 0;
                    return r;
                }
                {
                    cs_string* k_parent = key_parent();
                    if (k_parent) map_set_strkey(cm, k_parent, parent);
                    else map_set_cstr(cm, "__parent", parent);
                }
                cs_value_release(parent);
            } else {
                cs_string* k_parent = key_parent();
                if (k_parent) map_set_strkey(cm, k_parent, cs_nil());
                else map_set_cstr(cm, "__parent", cs_nil());
            }

            for (size_t i = 0; i < s->as.class_stmt.method_count; i++) {
                ast* m = s->as.class_stmt.methods[i];
                if (!m || m->type != N_FNDEF) continue;
                struct cs_func* f = (struct cs_func*)calloc(1, sizeof(struct cs_func));
                if (!f) { cs_value_release(cv); vm_set_err(vm, "out of memory", s->source_name, s->line, s->col); r.ok = 0; return r; }
                f->ref = 1;
                f->name = m->as.fndef.name;
                f->def_source = m->source_name;
                f->def_line = m->line;
                f->def_col = m->col;
                f->params = m->as.fndef.params;
                f->defaults = m->as.fndef.defaults;
                f->param_count = m->as.fndef.param_count;
                f->rest_param = m->as.fndef.rest_param;
                f->body = m->as.fndef.body;
                f->closure = env;
                f->is_async = m->as.fndef.is_async;
                f->is_generator = m->as.fndef.is_generator;
                env_incref(env);
                cs_value fv; fv.type = CS_T_FUNC; fv.as.p = f;
                map_set_cstr(cm, f->name ? f->name : "<method>", fv);
            }

            env_set_here(env, s->as.class_stmt.name, cv);
            cs_value_release(cv);
            return r;
        }

        case N_STRUCT: {
            cs_value sv = cs_map(vm);
            if (!sv.as.p) { vm_set_err(vm, "out of memory", s->source_name, s->line, s->col); r.ok = 0; return r; }
            cs_map_obj* sm = as_map(sv);

            {
                cs_string* k_is_struct = key_is_struct();
                int set_ok = k_is_struct ? map_set_strkey(sm, k_is_struct, cs_bool(1))
                                         : map_set_cstr(sm, "__is_struct", cs_bool(1));
                if (!set_ok) {
                    cs_value_release(sv);
                    vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                    r.ok = 0;
                    return r;
                }
            }

            cs_value namev = cs_str(vm, s->as.struct_stmt.name ? s->as.struct_stmt.name : "<struct>");
            map_set_cstr(sm, "__name", namev);
            cs_value_release(namev);

            cs_value fields = cs_list(vm);
            cs_value defaults = cs_list(vm);
            if (!fields.as.p || !defaults.as.p) {
                cs_value_release(fields);
                cs_value_release(defaults);
                cs_value_release(sv);
                vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                r.ok = 0;
                return r;
            }

            cs_list_obj* fl = as_list(fields);
            cs_list_obj* dl = as_list(defaults);
            for (size_t i = 0; i < s->as.struct_stmt.field_count; i++) {
                const char* fname = s->as.struct_stmt.field_names[i];
                cs_value fnv = cs_str(vm, fname ? fname : "");
                if (!list_push(fl, fnv)) {
                    cs_value_release(fnv);
                    cs_value_release(fields);
                    cs_value_release(defaults);
                    cs_value_release(sv);
                    vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                    r.ok = 0;
                    return r;
                }
                cs_value_release(fnv);

                cs_value dv = cs_nil();
                if (s->as.struct_stmt.field_defaults && s->as.struct_stmt.field_defaults[i]) {
                    int ok = 1;
                    dv = eval_expr(vm, env, s->as.struct_stmt.field_defaults[i], &ok);
                    if (!ok) {
                        cs_value_release(fields);
                        cs_value_release(defaults);
                        cs_value_release(sv);
                        if (exec_take_vm_throw(vm, &r)) return r;
                        r.ok = 0;
                        return r;
                    }
                }
                if (!list_push(dl, dv)) {
                    cs_value_release(dv);
                    cs_value_release(fields);
                    cs_value_release(defaults);
                    cs_value_release(sv);
                    vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                    r.ok = 0;
                    return r;
                }
                cs_value_release(dv);
            }

            {
                cs_string* k_fields = key_fields();
                cs_string* k_defaults = key_defaults();
                if (k_fields) map_set_strkey(sm, k_fields, fields);
                else map_set_cstr(sm, "__fields", fields);
                if (k_defaults) map_set_strkey(sm, k_defaults, defaults);
                else map_set_cstr(sm, "__defaults", defaults);
            }
            cs_value_release(fields);
            cs_value_release(defaults);

            env_set_here(env, s->as.struct_stmt.name, sv);
            cs_value_release(sv);
            return r;
        }

        case N_ENUM: {
            cs_value ev = cs_map(vm);
            if (!ev.as.p) { vm_set_err(vm, "out of memory", s->source_name, s->line, s->col); r.ok = 0; return r; }
            cs_map_obj* em = as_map(ev);

            if (!map_set_cstr(em, "__is_enum", cs_bool(1))) {
                cs_value_release(ev);
                vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                r.ok = 0;
                return r;
            }

            cs_value namev = cs_str(vm, s->as.enum_stmt.name ? s->as.enum_stmt.name : "<enum>");
            map_set_cstr(em, "__name", namev);
            cs_value_release(namev);

            long long cur = 0;
            for (size_t i = 0; i < s->as.enum_stmt.count; i++) {
                if (s->as.enum_stmt.values && s->as.enum_stmt.values[i]) {
                    int ok = 1;
                    cs_value vv = eval_expr(vm, env, s->as.enum_stmt.values[i], &ok);
                    if (!ok) {
                        cs_value_release(ev);
                        if (exec_take_vm_throw(vm, &r)) return r;
                        r.ok = 0;
                        return r;
                    }
                    if (vv.type != CS_T_INT) {
                        cs_value_release(vv);
                        cs_value_release(ev);
                        vm_set_err(vm, "enum value must be int", s->source_name, s->line, s->col);
                        r.ok = 0;
                        return r;
                    }
                    cur = vv.as.i;
                    cs_value_release(vv);
                }

                cs_value iv = cs_int(cur);
                if (!map_set_cstr(em, s->as.enum_stmt.names[i], iv)) {
                    cs_value_release(iv);
                    cs_value_release(ev);
                    vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                    r.ok = 0;
                    return r;
                }
                cs_value_release(iv);
                cur++;
            }

            env_set_here(env, s->as.enum_stmt.name, ev);
            cs_value_release(ev);
            return r;
        }

        case N_IF: {
            int ok = 1;
            cs_value c = eval_expr(vm, env, s->as.if_stmt.cond, &ok);
            if (!ok) {
                if (exec_take_vm_throw(vm, &r)) return r;
                r.ok = 0;
                return r;
            }
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
                if (!ok) {
                    if (exec_take_vm_throw(vm, &r)) return r;
                    r.ok = 0;
                    return r;
                }
                int t = is_truthy(c);
                cs_value_release(c);
                if (!t) break;

                r = exec_stmt(vm, env, s->as.while_stmt.body);
                if (!r.ok || r.did_return || r.did_throw) return r;
                if (r.did_break) {
                    r.did_break = 0;
                    cs_value_release(r.thrown);
                    r.thrown = cs_nil();
                    break;
                }
                if (r.did_continue) {
                    r.did_continue = 0;
                    cs_value_release(r.thrown);
                    r.thrown = cs_nil();
                    continue;
                }
            }
            return r;
        }

        case N_YIELD: {
            if (!vm->yield_active || !vm->yield_list) {
                vm_set_err(vm, "yield used outside of generator", s->source_name, s->line, s->col);
                r.ok = 0;
                return r;
            }
            cs_value v = cs_nil();
            if (s->as.yield_stmt.value) {
                int ok = 1;
                v = eval_expr(vm, env, s->as.yield_stmt.value, &ok);
                if (!ok) {
                    if (exec_take_vm_throw(vm, &r)) return r;
                    r.ok = 0;
                    return r;
                }
            }
            if (!list_push(vm->yield_list, v)) {
                cs_value_release(v);
                vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                r.ok = 0;
                return r;
            }
            vm->yield_used = 1;
            cs_value_release(v);
            return r;
        }

        case N_FORIN: {
            int ok = 1;
            cs_value it = eval_expr(vm, env, s->as.forin_stmt.iterable, &ok);
            if (!ok) {
                if (exec_take_vm_throw(vm, &r)) return r;
                r.ok = 0;
                return r;
            }

            cs_env* loopenv = env_new(env);
            if (!loopenv) { cs_value_release(it); vm_set_err(vm, "out of memory", s->source_name, s->line, s->col); r.ok = 0; return r; }

            env_set_here(loopenv, s->as.forin_stmt.name, cs_nil());

            if (it.type == CS_T_LIST) {
                cs_list_obj* l = as_list(it);
                for (size_t i = 0; i < (l ? l->len : 0); i++) {
                    cs_value v = cs_value_copy(l->items[i]);
                    env_set_here(loopenv, s->as.forin_stmt.name, v);
                    cs_value_release(v);

                    r = exec_stmt(vm, loopenv, s->as.forin_stmt.body);
                    if (!r.ok || r.did_return || r.did_throw) break;
                    if (r.did_break) { r.did_break = 0; break; }
                    if (r.did_continue) { r.did_continue = 0; continue; }
                }
            } else if (it.type == CS_T_MAP) {
                cs_map_obj* m = as_map(it);
                for (size_t i = 0; m && i < m->cap; i++) {
                    if (!m->entries[i].in_use) continue;
                    cs_value keyv = cs_value_copy(m->entries[i].key);
                    env_set_here(loopenv, s->as.forin_stmt.name, keyv);
                    cs_value_release(keyv);

                    r = exec_stmt(vm, loopenv, s->as.forin_stmt.body);
                    if (!r.ok || r.did_return || r.did_throw) break;
                    if (r.did_break) { r.did_break = 0; break; }
                    if (r.did_continue) { r.did_continue = 0; continue; }
                }
            } else if (it.type == CS_T_RANGE) {
                cs_range_obj* rg = as_range(it);
                if (rg) {
                    int64_t step = (rg->step == 0) ? ((rg->start <= rg->end) ? 1 : -1) : rg->step;
                    if (step > 0) {
                        int64_t limit = rg->inclusive ? rg->end : (rg->end - 1);
                        for (int64_t i = rg->start; i <= limit; i += step) {
                            cs_value v = cs_int(i);
                            env_set_here(loopenv, s->as.forin_stmt.name, v);
                            cs_value_release(v);

                            r = exec_stmt(vm, loopenv, s->as.forin_stmt.body);
                            if (!r.ok || r.did_return || r.did_throw) break;
                            if (r.did_break) { r.did_break = 0; break; }
                            if (r.did_continue) { r.did_continue = 0; continue; }
                        }
                    } else {
                        int64_t limit = rg->inclusive ? rg->end : (rg->end + 1);
                        for (int64_t i = rg->start; i >= limit; i += step) {
                            cs_value v = cs_int(i);
                            env_set_here(loopenv, s->as.forin_stmt.name, v);
                            cs_value_release(v);

                            r = exec_stmt(vm, loopenv, s->as.forin_stmt.body);
                            if (!r.ok || r.did_return || r.did_throw) break;
                            if (r.did_break) { r.did_break = 0; break; }
                            if (r.did_continue) { r.did_continue = 0; continue; }
                        }
                    }
                }
            } else {
                vm_set_err(vm, "for-in expects list, map, or range", s->source_name, s->line, s->col);
                r.ok = 0;
            }

            env_decref(loopenv);
            cs_value_release(it);
            return r;
        }

        case N_FOR_C_STYLE: {
            // Execute init (can be statement or expression)
            if (s->as.for_c_style_stmt.init) {
                node_type it = s->as.for_c_style_stmt.init->type;
                if (it == N_ASSIGN || it == N_SETINDEX || it == N_LET) {
                    // It's a statement
                    exec_result init_r = exec_stmt(vm, env, s->as.for_c_style_stmt.init);
                    if (!init_r.ok || init_r.did_throw) {
                        return init_r;
                    }
                    cs_value_release(init_r.ret);
                    cs_value_release(init_r.thrown);
                } else {
                    // It's an expression
                    int ok = 1;
                    cs_value init_result = eval_expr(vm, env, s->as.for_c_style_stmt.init, &ok);
                    cs_value_release(init_result);
                    if (!ok) {
                        if (exec_take_vm_throw(vm, &r)) return r;
                        r.ok = 0;
                        return r;
                    }
                }
            }
            
            // Loop: condition -> body -> increment
            while (1) {
                // Check condition
                if (s->as.for_c_style_stmt.cond) {
                    int ok = 1;
                    cs_value cond_result = eval_expr(vm, env, s->as.for_c_style_stmt.cond, &ok);
                    if (!ok) {
                        cs_value_release(cond_result);
                        if (exec_take_vm_throw(vm, &r)) return r;
                        r.ok = 0;
                        return r;
                    }
                    int is_true = is_truthy(cond_result);
                    cs_value_release(cond_result);
                    if (!is_true) break;
                }
                // else: no condition = infinite loop
                
                // Execute body
                r = exec_stmt(vm, env, s->as.for_c_style_stmt.body);
                if (!r.ok || r.did_return || r.did_throw) break;
                if (r.did_break) { r.did_break = 0; break; }
                if (r.did_continue) { r.did_continue = 0; /* fall through to increment */ }
                
                // Execute increment (can be statement or expression)
                if (s->as.for_c_style_stmt.incr) {
                    node_type it = s->as.for_c_style_stmt.incr->type;
                    if (it == N_ASSIGN || it == N_SETINDEX || it == N_LET) {
                        // It's a statement
                        exec_result incr_r = exec_stmt(vm, env, s->as.for_c_style_stmt.incr);
                        if (!incr_r.ok || incr_r.did_throw) {
                            return incr_r;
                        }
                        cs_value_release(incr_r.ret);
                        cs_value_release(incr_r.thrown);
                    } else {
                        // It's an expression
                        int ok = 1;
                        cs_value incr_result = eval_expr(vm, env, s->as.for_c_style_stmt.incr, &ok);
                        cs_value_release(incr_result);
                        if (!ok) {
                            if (exec_take_vm_throw(vm, &r)) return r;
                            r.ok = 0;
                            return r;
                        }
                    }
                }
            }
            
            return r;
        }

        case N_RETURN: {
            r.did_return = 1;
            if (s->as.ret_stmt.value) {
                int ok = 1;
                r.ret = eval_expr(vm, env, s->as.ret_stmt.value, &ok);
                if (!ok) {
                    cs_value_release(r.ret);
                    r.ret = cs_nil();
                    r.did_return = 0;
                    if (exec_take_vm_throw(vm, &r)) return r;
                    r.ok = 0;
                    return r;
                }
            } else {
                r.ret = cs_nil();
            }
            return r;
        }

        case N_EXPR_STMT: {
            int ok = 1;
            cs_value v = eval_expr(vm, env, s->as.expr_stmt.expr, &ok);
            cs_value_release(v);
            if (!ok) {
                if (exec_take_vm_throw(vm, &r)) return r;
                r.ok = 0;
            }
            return r;
        }

        case N_BREAK: {
            r.did_break = 1;
            return r;
        }

        case N_CONTINUE: {
            r.did_continue = 1;
            return r;
        }

        case N_THROW: {
            int ok = 1;
            cs_value v = eval_expr(vm, env, s->as.throw_stmt.value, &ok);
            if (!ok) {
                if (exec_take_vm_throw(vm, &r)) return r;
                r.ok = 0;
                return r;
            }
            r.did_throw = 1;
            r.thrown = v; // already a new value
            return r;
        }

        case N_EXPORT: {
            cs_value exports = cs_nil();
            if (!get_exports_map(vm, env, s, &exports)) {
                r.ok = 0;
                return r;
            }

            int ok = 1;
            cs_value v = eval_expr(vm, env, s->as.export_stmt.value, &ok);
            if (!ok) {
                cs_value_release(exports);
                if (exec_take_vm_throw(vm, &r)) return r;
                r.ok = 0;
                return r;
            }

            if (cs_map_set(exports, s->as.export_stmt.name, v) != 0) {
                cs_value_release(v);
                cs_value_release(exports);
                vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                r.ok = 0;
                return r;
            }

            cs_value_release(v);
            cs_value_release(exports);
            return r;
        }

        case N_EXPORT_LIST: {
            cs_value exports = cs_nil();
            if (!get_exports_map(vm, env, s, &exports)) {
                r.ok = 0;
                return r;
            }

            for (size_t i = 0; i < s->as.export_list.count; i++) {
                const char* local = s->as.export_list.local_names[i];
                const char* name = s->as.export_list.export_names[i];
                cs_value v = cs_nil();
                if (!env_get(env, local, &v)) {
                    if (cs_map_has(exports, local)) {
                        v = cs_map_get(exports, local);
                    } else {
                        cs_value_release(exports);
                        vm_set_err(vm, "export name not defined", s->source_name, s->line, s->col);
                        r.ok = 0;
                        return r;
                    }
                }
                if (cs_map_set(exports, name, v) != 0) {
                    cs_value_release(v);
                    cs_value_release(exports);
                    vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                    r.ok = 0;
                    return r;
                }
                cs_value_release(v);
            }

            cs_value_release(exports);
            return r;
        }

        case N_IMPORT: {
            int ok = 1;
            cs_value path_val = eval_expr(vm, env, s->as.import_stmt.path, &ok);
            if (!ok) {
                if (exec_take_vm_throw(vm, &r)) return r;
                r.ok = 0;
                return r;
            }

            if (path_val.type != CS_T_STR) {
                cs_value_release(path_val);
                vm_set_err(vm, "import expects string path", s->source_name, s->line, s->col);
                r.ok = 0;
                return r;
            }

            cs_value exports = cs_nil();
            if (cs_call(vm, "require", 1, &path_val, &exports) != 0) {
                cs_value_release(path_val);
                r.ok = 0;
                return r;
            }
            cs_value_release(path_val);

            if (exports.type != CS_T_MAP) {
                cs_value_release(exports);
                vm_set_err(vm, "import target is not a map", s->source_name, s->line, s->col);
                r.ok = 0;
                return r;
            }

            if (s->as.import_stmt.default_name) {
                env_set_here(env, s->as.import_stmt.default_name, exports);
            }

            for (size_t i = 0; i < s->as.import_stmt.count; i++) {
                const char* import_name = s->as.import_stmt.import_names[i];
                const char* local_name = s->as.import_stmt.local_names[i];
                cs_value v = cs_map_get(exports, import_name);
                env_set_here(env, local_name, v);
                cs_value_release(v);
            }

            cs_value_release(exports);
            return r;
        }

        case N_TRY: {
            size_t base_depth = vm ? vm->frame_count : 0;
            exec_result tr = exec_stmt(vm, env, s->as.try_stmt.try_b);
            if (!tr.ok) return tr;
            exec_result result = tr;

            if (tr.did_throw) {
                if (vm && vm->frame_count > base_depth) vm->frame_count = base_depth;
                cs_env* catchenv = env_new(env);
                if (!catchenv) {
                    cs_value_release(tr.thrown);
                    vm_set_err(vm, "out of memory", s->source_name, s->line, s->col);
                    tr.ok = 0;
                    tr.did_throw = 0;
                    tr.thrown = cs_nil();
                    return tr;
                }

                env_set_here(catchenv, s->as.try_stmt.catch_name, tr.thrown);
                cs_value_release(tr.thrown);
                tr.thrown = cs_nil();
                tr.did_throw = 0;

                exec_result cr = exec_stmt(vm, catchenv, s->as.try_stmt.catch_b);
                env_decref(catchenv);
                result = cr;
            }

            if (s->as.try_stmt.finally_b) {
                exec_result fr = exec_stmt(vm, env, s->as.try_stmt.finally_b);
                if (!fr.ok || fr.did_return || fr.did_break || fr.did_continue || fr.did_throw) {
                    cs_value_release(result.ret);
                    cs_value_release(result.thrown);
                    return fr;
                }
            }

            return result;
        }

        default:
            vm_set_err(vm, "invalid statement node", s->source_name, s->line, s->col);
            r.ok = 0;
            return r;
    }
}

void cs_vm_free(cs_vm* vm) {
    if (!vm) return;
    while (vm->task_head) {
        cs_task* t = vm->task_head;
        vm->task_head = t->next;
        for (int i = 0; i < t->argc; i++) cs_value_release(t->argv[i]);
        free(t->argv);
        promise_decref(t->promise);
        free(t);
    }
    vm->task_tail = NULL;
    while (vm->timers) {
        cs_timer* t = vm->timers;
        vm->timers = t->next;
        promise_decref(t->promise);
        free(t);
    }
    while (vm->pending_io) {
        cs_pending_io* io = vm->pending_io;
        vm->pending_io = io->next;
        cs_value_release(io->promise);
        cs_value_release(io->context);
        free(io);
    }
    env_decref(vm->globals);
    free(vm->last_error);
    cs_value_release(vm->pending_thrown);
    free(vm->frames);
    for (size_t i = 0; i < vm->ast_count; i++) {
        ast_free(vm->asts[i]);
    }
    free(vm->asts);
    for (size_t i = 0; i < vm->source_count; i++) free(vm->sources[i]);
    free(vm->sources);
    for (size_t i = 0; i < vm->dir_count; i++) free(vm->dir_stack[i]);
    free(vm->dir_stack);
    for (size_t i = 0; i < vm->module_count; i++) {
        free(vm->modules[i].path);
        cs_value_release(vm->modules[i].exports);
    }
    free(vm->modules);
    while (vm->tracked) {
        cs_track_node* n = vm->tracked;
        vm->tracked = n->next;
        free(n);
    }
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

void cs_register_global(cs_vm* vm, const char* name, cs_value value) {
    if (!vm || !name) return;
    env_set_here(vm->globals, name, value);
}

static int run_ast_in_env(cs_vm* vm, ast* prog, cs_env* env) {
    size_t base_depth = vm ? vm->frame_count : 0;
    
    // Reset safety controls for this execution
    if (vm) {
        vm->instruction_count = 0;
        vm->exec_start_ms = get_time_ms();
        vm->interrupt_requested = 0;
    }
    
    exec_result r = exec_block(vm, env, prog);
    if (r.did_throw) {
        vm_report_uncaught_throw(vm, r.thrown);
        r.ok = 0;
    } else if (r.did_break) {
        cs_error(vm, "break used outside of a loop");
        r.ok = 0;
    } else if (r.did_continue) {
        cs_error(vm, "continue used outside of a loop");
        r.ok = 0;
    }
    cs_value_release(r.ret);
    cs_value_release(r.thrown);
    if (vm && vm->frame_count > base_depth) vm->frame_count = base_depth;
    return r.ok ? 0 : -1;
}

static void vm_keep_ast(cs_vm* vm, ast* prog) {
    if (!vm || !prog) return;
    if (vm->ast_count == vm->ast_cap) {
        size_t nc = vm->ast_cap ? vm->ast_cap * 2 : 16;
        ast** na = (ast**)realloc(vm->asts, nc * sizeof(ast*));
        if (!na) {
            // Best-effort: if we can't track it, keep it leaked/alive.
            // Function closures may reference it.
            return;
        }
        vm->asts = na;
        vm->ast_cap = nc;
    }
    vm->asts[vm->ast_count++] = prog;
}

int cs_vm_run_string(cs_vm* vm, const char* code, const char* virtual_name) {
    if (!vm || !code) return -1;

    // Clear stale errors before running new code
    free(vm->last_error);
    vm->last_error = NULL;
    vm_clear_pending_throw(vm);

    parser P;
    const char* srcname = vm_intern_source(vm, virtual_name ? virtual_name : "<input>");
    parser_init(&P, code, srcname);
    ast* prog = parse_program(&P);

    if (P.error) {
        free(vm->last_error);
        vm->last_error = cs_strdup2(P.error);
        parse_free_error(&P);
        ast_free(prog);  // Free AST on parse error
        return -1;
    }
    int rc = run_ast_in_env(vm, prog, vm->globals);

    // Keep AST alive for closures/functions (freed in cs_vm_free).
    vm_keep_ast(vm, prog);
    
    // Trigger auto-GC after script execution
    vm_maybe_auto_gc(vm);
    
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
    vm_clear_pending_throw(vm);
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
    
    // Trigger auto-GC after file execution
    vm_maybe_auto_gc(vm);
    
    return rc;
}

static size_t gc_find_index(void** keys, unsigned char* types, size_t* vals, size_t cap, cs_track_type t, void* p) {
    if (!p || !keys || !types || !vals || cap == 0) return (size_t)-1;
    uintptr_t h = ((uintptr_t)p >> 3) ^ (uintptr_t)t;
    size_t pos = (size_t)(h & (cap - 1));
    while (keys[pos]) {
        if (keys[pos] == p && types[pos] == (unsigned char)t) return vals[pos];
        pos = (pos + 1) & (cap - 1);
    }
    return (size_t)-1;
}

size_t cs_vm_collect_cycles(cs_vm* vm) {
    if (!vm || vm->tracked_count == 0) return 0;

    typedef struct {
        cs_track_type type;
        void* ptr;
        int gc_refs;
        unsigned char marked;
    } gc_item;

    size_t n = vm->tracked_count;
    gc_item* items = (gc_item*)calloc(n, sizeof(gc_item));
    if (!items) return 0;

    size_t idx = 0;
    for (cs_track_node* cur = vm->tracked; cur && idx < n; cur = cur->next) {
        items[idx].type = cur->type;
        items[idx].ptr = cur->ptr;
        if (cur->type == CS_TRACK_LIST) items[idx].gc_refs = ((cs_list_obj*)cur->ptr)->ref;
        else if (cur->type == CS_TRACK_MAP) items[idx].gc_refs = ((cs_map_obj*)cur->ptr)->ref;
        idx++;
    }
    n = idx;
    if (n == 0) { free(items); return 0; }

    // Build ptr/type -> index map (open addressing).
    size_t cap = 1;
    while (cap < n * 2) cap <<= 1;
    void** keys = (void**)calloc(cap, sizeof(void*));
    unsigned char* types = (unsigned char*)calloc(cap, 1);
    size_t* vals = (size_t*)calloc(cap, sizeof(size_t));
    if (!keys || !types || !vals) { free(vals); free(types); free(keys); free(items); return 0; }

    for (size_t i = 0; i < n; i++) {
        uintptr_t h = ((uintptr_t)items[i].ptr >> 3) ^ (uintptr_t)items[i].type;
        size_t p = (size_t)(h & (cap - 1));
        while (keys[p]) p = (p + 1) & (cap - 1);
        keys[p] = items[i].ptr;
        types[p] = (unsigned char)items[i].type;
        vals[p] = i;
    }

    // Subtract internal list/map references.
    for (size_t i = 0; i < n; i++) {
        if (items[i].type == CS_TRACK_LIST) {
            cs_list_obj* l = (cs_list_obj*)items[i].ptr;
            for (size_t j = 0; l && j < l->len; j++) {
                cs_value v = l->items[j];
                if (v.type == CS_T_LIST) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_LIST, v.as.p);
                    if (k != (size_t)-1) items[k].gc_refs--;
                } else if (v.type == CS_T_MAP) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_MAP, v.as.p);
                    if (k != (size_t)-1) items[k].gc_refs--;
                }
            }
        } else if (items[i].type == CS_TRACK_MAP) {
            cs_map_obj* m = (cs_map_obj*)items[i].ptr;
            for (size_t j = 0; m && j < m->cap; j++) {
                if (!m->entries[j].in_use) continue;
                cs_value k = m->entries[j].key;
                cs_value v = m->entries[j].val;
                if (k.type == CS_T_LIST) {
                    size_t kk = gc_find_index(keys, types, vals, cap, CS_TRACK_LIST, k.as.p);
                    if (kk != (size_t)-1) items[kk].gc_refs--;
                } else if (k.type == CS_T_MAP) {
                    size_t kk = gc_find_index(keys, types, vals, cap, CS_TRACK_MAP, k.as.p);
                    if (kk != (size_t)-1) items[kk].gc_refs--;
                }
                if (v.type == CS_T_LIST) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_LIST, v.as.p);
                    if (k != (size_t)-1) items[k].gc_refs--;
                } else if (v.type == CS_T_MAP) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_MAP, v.as.p);
                    if (k != (size_t)-1) items[k].gc_refs--;
                }
            }
        }
    }

    // Mark reachable from externally-referenced candidates (gc_refs > 0).
    size_t* stack = (size_t*)malloc(sizeof(size_t) * n);
    if (!stack) { free(vals); free(types); free(keys); free(items); return 0; }
    size_t sp = 0;
    for (size_t i = 0; i < n; i++) {
        if (items[i].gc_refs > 0 && !items[i].marked) {
            items[i].marked = 1;
            stack[sp++] = i;
        }
    }

    while (sp) {
        size_t i = stack[--sp];
        if (items[i].type == CS_TRACK_LIST) {
            cs_list_obj* l = (cs_list_obj*)items[i].ptr;
            for (size_t j = 0; l && j < l->len; j++) {
                cs_value v = l->items[j];
                if (v.type == CS_T_LIST) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_LIST, v.as.p);
                    if (k != (size_t)-1 && !items[k].marked) { items[k].marked = 1; stack[sp++] = k; }
                } else if (v.type == CS_T_MAP) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_MAP, v.as.p);
                    if (k != (size_t)-1 && !items[k].marked) { items[k].marked = 1; stack[sp++] = k; }
                }
            }
        } else if (items[i].type == CS_TRACK_MAP) {
            cs_map_obj* m = (cs_map_obj*)items[i].ptr;
            for (size_t j = 0; m && j < m->cap; j++) {
                if (!m->entries[j].in_use) continue;
                cs_value k = m->entries[j].key;
                cs_value v = m->entries[j].val;
                if (k.type == CS_T_LIST) {
                    size_t kk = gc_find_index(keys, types, vals, cap, CS_TRACK_LIST, k.as.p);
                    if (kk != (size_t)-1 && !items[kk].marked) { items[kk].marked = 1; stack[sp++] = kk; }
                } else if (k.type == CS_T_MAP) {
                    size_t kk = gc_find_index(keys, types, vals, cap, CS_TRACK_MAP, k.as.p);
                    if (kk != (size_t)-1 && !items[kk].marked) { items[kk].marked = 1; stack[sp++] = kk; }
                }
                if (v.type == CS_T_LIST) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_LIST, v.as.p);
                    if (k != (size_t)-1 && !items[k].marked) { items[k].marked = 1; stack[sp++] = k; }
                } else if (v.type == CS_T_MAP) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_MAP, v.as.p);
                    if (k != (size_t)-1 && !items[k].marked) { items[k].marked = 1; stack[sp++] = k; }
                }
            }
        }
    }

    size_t collected = 0;
    for (size_t i = 0; i < n; i++) {
        if (items[i].marked) continue;
        collected++;
        if (items[i].type == CS_TRACK_LIST) {
            cs_list_obj* l = (cs_list_obj*)items[i].ptr;
            for (size_t j = 0; l && j < l->len; j++) {
                cs_value v = l->items[j];
                int is_garbage = 0;
                if (v.type == CS_T_LIST) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_LIST, v.as.p);
                    is_garbage = (k != (size_t)-1 && !items[k].marked);
                } else if (v.type == CS_T_MAP) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_MAP, v.as.p);
                    is_garbage = (k != (size_t)-1 && !items[k].marked);
                }
                if (!is_garbage) cs_value_release(v);
                l->items[j] = cs_nil();
            }
            free(l->items);
            vm_track_remove(vm, CS_TRACK_LIST, l);
            free(l);
        } else if (items[i].type == CS_TRACK_MAP) {
            cs_map_obj* m = (cs_map_obj*)items[i].ptr;
            for (size_t j = 0; m && j < m->cap; j++) {
                if (!m->entries[j].in_use) continue;
                cs_value_release(m->entries[j].key);
                cs_value v = m->entries[j].val;
                int is_garbage = 0;
                if (v.type == CS_T_LIST) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_LIST, v.as.p);
                    is_garbage = (k != (size_t)-1 && !items[k].marked);
                } else if (v.type == CS_T_MAP) {
                    size_t k = gc_find_index(keys, types, vals, cap, CS_TRACK_MAP, v.as.p);
                    is_garbage = (k != (size_t)-1 && !items[k].marked);
                }
                if (!is_garbage) cs_value_release(v);
                m->entries[j].key = cs_nil();
                m->entries[j].val = cs_nil();
                m->entries[j].in_use = 0;
            }
            free(m->entries);
            vm_track_remove(vm, CS_TRACK_MAP, m);
            free(m);
        }
    }

    free(stack);
    free(vals);
    free(types);
    free(keys);
    free(items);
    return collected;
}

static int module_find(cs_vm* vm, const char* path) {
    if (!vm || !path) return -1;
    for (size_t i = 0; i < vm->module_count; i++) {
        if (strcmp(vm->modules[i].path, path) == 0) return (int)i;
    }
    return -1;
}

static int module_add(cs_vm* vm, const char* path, cs_value exports) {
    if (!vm || !path) return 0;
    if (vm->module_count == vm->module_cap) {
        size_t nc = vm->module_cap ? vm->module_cap * 2 : 16;
        cs_module* nm = (cs_module*)realloc(vm->modules, nc * sizeof(cs_module));
        if (!nm) return 0;
        vm->modules = nm;
        vm->module_cap = nc;

        if (vm->pending_io_count > 0) {
            int timeout = 100;
            if (vm->timers) {
                uint64_t now = get_time_ms();
                uint64_t due = vm->timers->due_ms;
                if (due <= now) timeout = 0;
                else {
                    uint64_t delta = due - now;
                    if (delta < (uint64_t)timeout) timeout = (int)delta;
                }
            }
            cs_poll_pending_io(vm, timeout);
            return 1;
        }
    }
    vm->modules[vm->module_count].path = cs_strdup2(path);
    if (!vm->modules[vm->module_count].path) return 0;
    vm->modules[vm->module_count].exports = cs_value_copy(exports);
    vm->module_count++;
    return 1;
}

int cs_vm_require_module(cs_vm* vm, const char* path, cs_value* exports_out) {
    if (exports_out) *exports_out = cs_nil();
    if (!vm || !path) return -1;

    // Clear stale errors before requiring.
    free(vm->last_error);
    vm->last_error = NULL;
    vm_clear_pending_throw(vm);

    // Normalize path to avoid duplicate modules from "./a.cs" vs "a.cs"
    char* norm_path = path_normalize_alloc(path);
    if (!norm_path) { cs_error(vm, "out of memory"); return -1; }

    int idx = module_find(vm, norm_path);
    if (idx >= 0) {
        if (exports_out) *exports_out = cs_value_copy(vm->modules[(size_t)idx].exports);
        free(norm_path);
        return 0;
    }

    char* code = read_file_all(norm_path);
    if (!code) { 
        free(norm_path);
        cs_error(vm, "could not read file"); 
        return -1; 
    }

    char* dir = path_dirname_alloc(norm_path);
    vm_dir_push_owned(vm, dir);

    const char* srcname = vm_intern_source(vm, norm_path);
    parser P;
    parser_init(&P, code, srcname);
    ast* prog = parse_program(&P);
    free(code);

    if (P.error) {
        cs_error(vm, P.error);
        parse_free_error(&P);
        ast_free(prog);  // Free AST on parse error
        vm_dir_pop(vm);
        free(norm_path);
        return -1;
    }

    cs_env* menv = env_new(vm->globals);
    if (!menv) {
        ast_free(prog);
        vm_dir_pop(vm);
        free(norm_path);
        cs_error(vm, "out of memory");
        return -1;
    }

    cs_value exports = cs_map(vm);
    if (!exports.as.p) {
        env_decref(menv);
        ast_free(prog);
        vm_dir_pop(vm);
        free(norm_path);
        cs_error(vm, "out of memory");
        return -1;
    }

    // Make exports available to the module.
    env_set_here(menv, "exports", exports);
    cs_value_release(exports);
    
    // Set __file__ and __dir__ for module introspection
    cs_value file_val = cs_str(vm, norm_path);
    env_set_here(menv, "__file__", file_val);
    cs_value_release(file_val);
    
    if (dir) {
        cs_value dir_val = cs_str(vm, dir);
        env_set_here(menv, "__dir__", dir_val);
        cs_value_release(dir_val);
    }

    int rc = run_ast_in_env(vm, prog, menv);

    // Keep AST alive for closures/functions (freed in cs_vm_free).
    vm_keep_ast(vm, prog);

    if (rc != 0) {
        env_decref(menv);
        vm_dir_pop(vm);
        free(norm_path);
        return -1;
    }

    // Fetch exports after module executed.
    cs_value exv = cs_nil();
    (void)env_get(menv, "exports", &exv);

    if (!module_add(vm, norm_path, exv)) {
        cs_value_release(exv);
        env_decref(menv);
        vm_dir_pop(vm);
        free(norm_path);
        cs_error(vm, "out of memory");
        return -1;
    }

    if (exports_out) *exports_out = cs_value_copy(exv);
    cs_value_release(exv);
    env_decref(menv);
    vm_dir_pop(vm);
    free(norm_path);
    return 0;
}

int cs_call(cs_vm* vm, const char* func_name, int argc, const cs_value* argv, cs_value* out) {
    if (out) *out = cs_nil();
    if (!vm || !func_name) return -1;

    cs_value f;
    if (!env_get(vm->globals, func_name, &f)) return -1;

    int ok = 1;
    cs_value result = cs_nil();

    const char* host_src = vm_intern_source(vm, "(host)");
    size_t base_depth = vm->frame_count;
    vm_frames_push(vm, func_name, host_src, 0, 0);
    vm_clear_pending_throw(vm);

    if (f.type == CS_T_NATIVE) {
        cs_native* nf = as_native(f);
        if (nf->fn(vm, nf->userdata, argc, argv, &result) != 0) {
            if (!vm->last_error) cs_error(vm, "native call failed");
            ok = 0;
        }
    } else if (f.type == CS_T_FUNC) {
        struct cs_func* fn = as_func(f);
        if (fn->is_async) {
            result = schedule_async_call(vm, fn, argc, (cs_value*)argv, NULL, NULL, &ok);
        } else {
            cs_env* callenv = env_new(fn->closure);
            if (!callenv) ok = 0;
            else if (!bind_params_with_defaults(vm, callenv, fn, argc, argv, &ok)) {
                env_decref(callenv);
            } else {
                exec_result r = exec_block(vm, callenv, fn->body);
                if (r.did_throw) {
                    vm_report_uncaught_throw(vm, r.thrown);
                    ok = 0;
                } else if (r.did_break) {
                    cs_error(vm, "break used outside of a loop");
                    ok = 0;
                } else if (r.did_continue) {
                    cs_error(vm, "continue used outside of a loop");
                    ok = 0;
                } else if (!r.ok) {
                    ok = 0;
                } else if (r.did_return) {
                    result = cs_value_copy(r.ret);
                }
                cs_value_release(r.ret);
                cs_value_release(r.thrown);
                env_decref(callenv);
            }
        }
    } else {
        ok = 0;
    }

    if (vm->frame_count > base_depth) vm->frame_count = base_depth;

    cs_value_release(f);
    if (!ok) { cs_value_release(result); return -1; }
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
    vm_clear_pending_throw(vm);

    const char* host_src = vm_intern_source(vm, "(host)");
    size_t base_depth = vm->frame_count;
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
        if (fn->is_async) {
            result = schedule_async_call(vm, fn, argc, (cs_value*)argv, NULL, NULL, &ok);
        } else {
            cs_env* callenv = env_new(fn->closure);
            if (!callenv) ok = 0;
            else if (!bind_params_with_defaults(vm, callenv, fn, argc, argv, &ok)) {
                env_decref(callenv);
            } else {
                exec_result r = exec_block(vm, callenv, fn->body);
                if (r.did_throw) {
                    vm_report_uncaught_throw(vm, r.thrown);
                    ok = 0;
                } else if (r.did_break) {
                    cs_error(vm, "break used outside of a loop");
                    ok = 0;
                } else if (r.did_continue) {
                    cs_error(vm, "continue used outside of a loop");
                    ok = 0;
                } else if (!r.ok) {
                    ok = 0;
                } else if (r.did_return) {
                    result = cs_value_copy(r.ret);
                }
                cs_value_release(r.ret);
                cs_value_release(r.thrown);
                env_decref(callenv);
            }
        }
    } else {
        ok = 0;
    }

    if (vm->frame_count > base_depth) vm->frame_count = base_depth;

    if (!ok) { cs_value_release(result); return -1; }
    if (out) *out = result;
    else cs_value_release(result);
    return 0;
}

// ========== Safety Control API ==========

void cs_vm_set_instruction_limit(cs_vm* vm, uint64_t limit) {
    if (!vm) return;
    vm->instruction_limit = limit;
}

void cs_vm_set_timeout(cs_vm* vm, uint64_t timeout_ms) {
    if (!vm) return;
    vm->exec_timeout_ms = timeout_ms;
}

void cs_vm_interrupt(cs_vm* vm) {
    if (!vm) return;
    vm->interrupt_requested = 1;
}

uint64_t cs_vm_get_instruction_count(cs_vm* vm) {
    return vm ? vm->instruction_count : 0;
}

// ========== GC Auto-Collect Configuration ==========

void cs_vm_set_gc_threshold(cs_vm* vm, size_t threshold) {
    if (!vm) return;
    vm->gc_threshold = threshold;
}

void cs_vm_set_gc_alloc_trigger(cs_vm* vm, size_t interval) {
    if (!vm) return;
    vm->gc_alloc_trigger = interval;
}

// Accessors for stdlib gc_stats/gc_config functions
size_t vm_get_tracked_count(cs_vm* vm) {
    return vm ? vm->tracked_count : 0;
}

size_t vm_get_gc_collections(cs_vm* vm) {
    return vm ? vm->gc_collections : 0;
}

size_t vm_get_gc_objects_collected(cs_vm* vm) {
    return vm ? vm->gc_objects_collected : 0;
}

size_t vm_get_gc_allocations(cs_vm* vm) {
    return vm ? vm->gc_allocations : 0;
}

size_t vm_get_gc_threshold(cs_vm* vm) {
    return vm ? vm->gc_threshold : 0;
}

size_t vm_get_gc_alloc_trigger(cs_vm* vm) {
    return vm ? vm->gc_alloc_trigger : 0;
}

uint64_t vm_get_timeout(cs_vm* vm) {
    return vm ? vm->exec_timeout_ms : 0;
}

uint64_t vm_get_instruction_limit(cs_vm* vm) {
    return vm ? vm->instruction_limit : 0;
}

static void vm_maybe_auto_gc(cs_vm* vm) {
    if (!vm) return;
    
    int should_collect = 0;
    
    // Check tracked object threshold
    if (vm->gc_threshold > 0 && vm->tracked_count >= vm->gc_threshold) {
        should_collect = 1;
    }
    
    // Check allocation interval
    if (vm->gc_alloc_trigger > 0 && vm->gc_allocations >= vm->gc_alloc_trigger) {
        should_collect = 1;
        vm->gc_allocations = 0;  // Reset counter
    }
    
    if (should_collect) {
        size_t collected = cs_vm_collect_cycles(vm);
        vm->gc_collections++;
        vm->gc_objects_collected += collected;
    }
}

// ========== C API for List/Map Operations ==========

size_t cs_list_len(cs_value list_val) {
    if (list_val.type != CS_T_LIST) return 0;
    cs_list_obj* list = as_list(list_val);
    return list ? list->len : 0;
}

cs_value cs_list_get(cs_value list_val, size_t index) {
    if (list_val.type != CS_T_LIST) return cs_nil();
    cs_list_obj* list = as_list(list_val);
    if (!list || index >= list->len) return cs_nil();
    return cs_value_copy(list->items[index]);
}

int cs_list_set(cs_value list_val, size_t index, cs_value value) {
    if (list_val.type != CS_T_LIST) return -1;
    cs_list_obj* list = as_list(list_val);
    if (!list) return -1;
    
    // Grow if needed
    if (index >= list->len) {
        if (!list_ensure(list, index + 1)) return -1;
        // Fill gaps with nil
        for (size_t i = list->len; i < index; i++) {
            list->items[i] = cs_nil();
        }
        list->len = index + 1;
    }
    
    cs_value_release(list->items[index]);
    list->items[index] = cs_value_copy(value);
    return 0;
}

int cs_list_push(cs_value list_val, cs_value value) {
    if (list_val.type != CS_T_LIST) return -1;
    cs_list_obj* list = as_list(list_val);
    if (!list) return -1;
    return list_push(list, value) ? 0 : -1;
}

cs_value cs_list_pop(cs_value list_val) {
    if (list_val.type != CS_T_LIST) return cs_nil();
    cs_list_obj* list = as_list(list_val);
    if (!list || list->len == 0) return cs_nil();
    cs_value val = list->items[list->len - 1];
    list->len--;
    return val;  // Ownership transferred to caller
}

size_t cs_map_len(cs_value map_val) {
    if (map_val.type != CS_T_MAP) return 0;
    cs_map_obj* map = as_map(map_val);
    return map ? map->len : 0;
}

cs_value cs_map_get(cs_value map_val, const char* key) {
    if (map_val.type != CS_T_MAP || !key) return cs_nil();
    cs_map_obj* map = as_map(map_val);
    if (!map) return cs_nil();
    return map_get_cstr(map, key);
}

int cs_map_set(cs_value map_val, const char* key, cs_value value) {
    if (map_val.type != CS_T_MAP || !key) return -1;
    cs_map_obj* map = as_map(map_val);
    if (!map) return -1;
    int result = map_set_cstr(map, key, value);
    return result ? 0 : -1;
}

int cs_map_has(cs_value map_val, const char* key) {
    if (map_val.type != CS_T_MAP || !key) return 0;
    cs_map_obj* map = as_map(map_val);
    if (!map) return 0;
    cs_string* key_str = cs_str_new(key);
    if (!key_str) return 0;
    cs_value kv; kv.type = CS_T_STR; kv.as.p = key_str;
    int ok = map_has_value(map, kv);
    cs_str_decref(key_str);
    return ok;
}

int cs_map_del(cs_value map_val, const char* key) {
    if (map_val.type != CS_T_MAP || !key) return -1;
    cs_map_obj* map = as_map(map_val);
    if (!map) return -1;
    cs_string* key_str = cs_str_new(key);
    if (!key_str) return -1;
    cs_value kv; kv.type = CS_T_STR; kv.as.p = key_str;
    int ok = map_del_value(map, kv);
    cs_str_decref(key_str);
    return ok ? 0 : -1;
}

cs_value cs_map_get_value(cs_value map_val, cs_value key) {
    if (map_val.type != CS_T_MAP) return cs_nil();
    cs_map_obj* map = as_map(map_val);
    if (!map) return cs_nil();
    return map_get_value(map, key);
}

int cs_map_set_value(cs_value map_val, cs_value key, cs_value value) {
    if (map_val.type != CS_T_MAP) return -1;
    cs_map_obj* map = as_map(map_val);
    if (!map) return -1;
    return map_set_value(map, key, value) ? 0 : -1;
}

int cs_map_has_value(cs_value map_val, cs_value key) {
    if (map_val.type != CS_T_MAP) return 0;
    cs_map_obj* map = as_map(map_val);
    if (!map) return 0;
    return map_has_value(map, key);
}

int cs_map_del_value(cs_value map_val, cs_value key) {
    if (map_val.type != CS_T_MAP) return -1;
    cs_map_obj* map = as_map(map_val);
    if (!map) return -1;
    return map_del_value(map, key) ? 0 : -1;
}

cs_value cs_map_keys(cs_vm* vm, cs_value map_val) {
    if (map_val.type != CS_T_MAP) return cs_nil();
    cs_map_obj* map = as_map(map_val);
    if (!map) return cs_nil();
    
    cs_value list_val = cs_list(vm);
    cs_list_obj* list = as_list(list_val);
    if (!list) return cs_nil();
    
    if (!list_ensure(list, map->len)) { cs_value_release(list_val); return cs_nil(); }
    for (size_t i = 0; i < map->cap; i++) {
        if (!map->entries[i].in_use) continue;
        list->items[list->len++] = cs_value_copy(map->entries[i].key);
    }
    
    return list_val;
}
