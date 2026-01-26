#include "cs_vm.h"
#include "cs_net.h"
#include "cs_tls.h"
#include "cs_http.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#if !defined(_WIN32)
#include <regex.h>
#endif
#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#endif

// ---------- Date/Time helpers ----------
static int64_t wall_clock_ms(void) {
#if !defined(_WIN32)
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    return (int64_t)tv.tv_sec * 1000 + (int64_t)(tv.tv_usec / 1000);
#else
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    const uint64_t EPOCH_100NS = 116444736000000000ULL; // 1601->1970
    if (uli.QuadPart < EPOCH_100NS) return 0;
    return (int64_t)((uli.QuadPart - EPOCH_100NS) / 10000ULL);
#endif
}

static int tm_local_from_time(time_t sec, struct tm* out) {
#if defined(_WIN32)
    return localtime_s(out, &sec) == 0;
#else
    return localtime_r(&sec, out) != NULL;
#endif
}

static int tm_utc_from_time(time_t sec, struct tm* out) {
#if defined(_WIN32)
    return gmtime_s(out, &sec) == 0;
#else
    return gmtime_r(&sec, out) != NULL;
#endif
}

static cs_value datetime_map_from_tm(cs_vm* vm, const struct tm* t, int ms, int is_utc) {
    cs_value m = cs_map(vm);
    if (!m.as.p) { cs_error(vm, "out of memory"); return cs_nil(); }
    cs_map_set(m, "year", cs_int((int64_t)t->tm_year + 1900));
    cs_map_set(m, "month", cs_int((int64_t)t->tm_mon + 1));
    cs_map_set(m, "day", cs_int((int64_t)t->tm_mday));
    cs_map_set(m, "hour", cs_int((int64_t)t->tm_hour));
    cs_map_set(m, "minute", cs_int((int64_t)t->tm_min));
    cs_map_set(m, "second", cs_int((int64_t)t->tm_sec));
    cs_map_set(m, "ms", cs_int((int64_t)ms));
    cs_map_set(m, "wday", cs_int((int64_t)t->tm_wday));
    cs_map_set(m, "yday", cs_int((int64_t)t->tm_yday + 1));
    cs_map_set(m, "is_dst", cs_bool(t->tm_isdst > 0));
    cs_map_set(m, "is_utc", cs_bool(is_utc));
    return m;
}

static int nf_unix_ms(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    *out = cs_int((int64_t)wall_clock_ms());
    return 0;
}

static int nf_unix_s(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    *out = cs_int((int64_t)time(NULL));
    return 0;
}

static int nf_datetime_now(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    int64_t ms = wall_clock_ms();
    time_t sec = (time_t)(ms / 1000);
    int rem = (int)(ms % 1000);
    if (rem < 0) { rem += 1000; sec -= 1; }
    struct tm t;
    if (!tm_local_from_time(sec, &t)) { *out = cs_nil(); return 0; }
    *out = datetime_map_from_tm(vm, &t, rem, 0);
    return 0;
}

static int nf_datetime_utc(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    int64_t ms = wall_clock_ms();
    time_t sec = (time_t)(ms / 1000);
    int rem = (int)(ms % 1000);
    if (rem < 0) { rem += 1000; sec -= 1; }
    struct tm t;
    if (!tm_utc_from_time(sec, &t)) { *out = cs_nil(); return 0; }
    *out = datetime_map_from_tm(vm, &t, rem, 1);
    return 0;
}

static int nf_datetime_from_unix_ms(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || (argv[0].type != CS_T_INT && argv[0].type != CS_T_FLOAT)) { *out = cs_nil(); return 0; }
    int64_t ms = (argv[0].type == CS_T_INT) ? argv[0].as.i : (int64_t)argv[0].as.f;
    time_t sec = (time_t)(ms / 1000);
    int rem = (int)(ms % 1000);
    if (rem < 0) { rem += 1000; sec -= 1; }
    struct tm t;
    if (!tm_local_from_time(sec, &t)) { *out = cs_nil(); return 0; }
    *out = datetime_map_from_tm(vm, &t, rem, 0);
    return 0;
}

static int nf_datetime_from_unix_ms_utc(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || (argv[0].type != CS_T_INT && argv[0].type != CS_T_FLOAT)) { *out = cs_nil(); return 0; }
    int64_t ms = (argv[0].type == CS_T_INT) ? argv[0].as.i : (int64_t)argv[0].as.f;
    time_t sec = (time_t)(ms / 1000);
    int rem = (int)(ms % 1000);
    if (rem < 0) { rem += 1000; sec -= 1; }
    struct tm t;
    if (!tm_utc_from_time(sec, &t)) { *out = cs_nil(); return 0; }
    *out = datetime_map_from_tm(vm, &t, rem, 1);
    return 0;
}

// Forward declarations
static int nf_error(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out);
static int nf_format_error(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out);
static int cs_value_equals(const cs_value* a, const cs_value* b);

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

#if defined(_WIN32)
#define CS_STAT_STRUCT struct _stat
#define CS_STAT_FN _stat
#else
#define CS_STAT_STRUCT struct stat
#define CS_STAT_FN stat
#endif

static int fs_stat(const char* path, CS_STAT_STRUCT* st) {
    if (!path || !st) return 0;
    return CS_STAT_FN(path, st) == 0;
}

static int fs_is_dir(const char* path) {
    CS_STAT_STRUCT st;
    if (!fs_stat(path, &st)) return 0;
#if defined(_WIN32)
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

static int fs_is_file(const char* path) {
    CS_STAT_STRUCT st;
    if (!fs_stat(path, &st)) return 0;
#if defined(_WIN32)
    return (st.st_mode & _S_IFREG) != 0;
#else
    return S_ISREG(st.st_mode);
#endif
}

static int vm_set_current_dir(cs_vm* vm, const char* path) {
    if (!vm || !path) return 0;
    char* dup = cs_strdup2_local(path);
    if (!dup) return 0;

    if (vm->dir_count == 0) {
        if (vm->dir_cap == 0) {
            vm->dir_cap = 4;
            vm->dir_stack = (char**)malloc(sizeof(char*) * vm->dir_cap);
            if (!vm->dir_stack) { free(dup); vm->dir_cap = 0; return 0; }
        }
        vm->dir_stack[0] = dup;
        vm->dir_count = 1;
        return 1;
    }

    free(vm->dir_stack[vm->dir_count - 1]);
    vm->dir_stack[vm->dir_count - 1] = dup;
    return 1;
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
        case CS_T_PROMISE: return "<promise>";
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
            case CS_T_FLOAT: fprintf(stdout, "%g", argv[i].as.f); break;
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
        // Create error object for assertion failure
        const char* msg = (argc >= 2 && argv[1].type == CS_T_STR) ? 
                         cs_to_cstr(argv[1]) : "assertion failed";
        
        cs_value err_args[2];
        err_args[0] = cs_str(vm, msg);
        err_args[1] = cs_str(vm, "ASSERTION");
        
        cs_value err;
        nf_error(vm, NULL, 2, err_args, &err);
        
        cs_value_release(err_args[0]);
        cs_value_release(err_args[1]);
        
        // Format and throw the error
        cs_value formatted;
        nf_format_error(vm, NULL, 1, &err, &formatted);
        if (formatted.type == CS_T_STR) {
            cs_error(vm, cs_to_cstr(formatted));
        }
        cs_value_release(formatted);
        cs_value_release(err);
        
        return 1;
    }
    return 0;
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

    cs_value exports = cs_nil();
    int rc = cs_vm_require_module(vm, path, &exports);
    free(path);
    if (rc != 0) return 1;
    if (out) *out = exports;
    else cs_value_release(exports);
    return 0;
}

static int nf_require_optional(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_nil();
    if (argc != 1 || argv[0].type != CS_T_STR) return 0;

    char* path = resolve_path_alloc(vm, cs_to_cstr(argv[0]));
    if (!path) {
        if (out) *out = cs_nil();
        return 0;  // No error, just return nil for path resolution failure
    }

    // Check if file exists
    FILE* f = fopen(path, "r");
    if (!f) {
        free(path);
        if (out) *out = cs_nil();
        return 0;  // File not found - return nil without error
    }
    fclose(f);

    // File exists, use regular require logic
    // Parse/runtime errors should propagate as normal
    cs_value exports = cs_nil();
    int rc = cs_vm_require_module(vm, path, &exports);
    free(path);
    if (rc != 0) {
        // Parse or runtime error - propagate the error
        return 1;
    }
    if (out) *out = exports;
    else cs_value_release(exports);
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
    (void)ud;
    if (!out) return 0;
    if (argc == 2 && argv[0].type == CS_T_LIST) {
        if (argv[1].type != CS_T_FUNC && argv[1].type != CS_T_NATIVE) { cs_error(vm, "map(): mapper must be a function"); return 1; }
        cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
        cs_value mapper = argv[1];
        cs_value out_list = cs_list(vm);
        if (!out_list.as.p) { cs_error(vm, "out of memory"); return 1; }
        for (size_t i = 0; l && i < l->len; i++) {
            cs_value args[1] = { l->items[i] };
            cs_value ret = cs_nil();
            if (cs_call_value(vm, mapper, 1, args, &ret) != 0) { cs_value_release(out_list); return 1; }
            if (cs_list_push(out_list, ret) != 0) { cs_value_release(ret); cs_value_release(out_list); cs_error(vm, "out of memory"); return 1; }
            cs_value_release(ret);
        }
        *out = out_list;
        return 0;
    }

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

static int nf_extend(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_nil();
    if (argc != 2 || argv[0].type != CS_T_LIST || argv[1].type != CS_T_LIST) {
        cs_error(vm, "extend() requires two lists");
        return 1;
    }

    cs_list_obj* dst = (cs_list_obj*)argv[0].as.p;
    cs_list_obj* src = (cs_list_obj*)argv[1].as.p;
    if (!dst || !src) return 0;
    if (src->len == 0) return 0;

    if (!list_ensure(dst, dst->len + src->len)) {
        cs_error(vm, "out of memory");
        return 1;
    }

    for (size_t i = 0; i < src->len; i++) {
        dst->items[dst->len++] = cs_value_copy(src->items[i]);
    }

    return 0;
}

static int nf_index_of(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_LIST) {
        cs_error(vm, "index_of() requires a list and a value");
        return 1;
    }

    cs_list_obj* list = (cs_list_obj*)argv[0].as.p;
    cs_value item = argv[1];
    for (size_t i = 0; i < list->len; i++) {
        if (cs_value_equals(&list->items[i], &item)) {
            *out = cs_int((int64_t)i);
            return 0;
        }
    }

    *out = cs_int(-1);
    return 0;
}

static int compare_default(cs_vm* vm, cs_value a, cs_value b, int* ok) {
    if (!ok) return 0;
    *ok = 1;

    if ((a.type == CS_T_INT || a.type == CS_T_FLOAT) && (b.type == CS_T_INT || b.type == CS_T_FLOAT)) {
        double av = (a.type == CS_T_INT) ? (double)a.as.i : a.as.f;
        double bv = (b.type == CS_T_INT) ? (double)b.as.i : b.as.f;
        if (av < bv) return -1;
        if (av > bv) return 1;
        return 0;
    }

    if (a.type == CS_T_STR && b.type == CS_T_STR) {
        const char* sa = cs_to_cstr(a);
        const char* sb = cs_to_cstr(b);
        int cmp = strcmp(sa, sb);
        return (cmp < 0) ? -1 : (cmp > 0 ? 1 : 0);
    }

    if (a.type == CS_T_BOOL && b.type == CS_T_BOOL) {
        return (a.as.b < b.as.b) ? -1 : (a.as.b > b.as.b ? 1 : 0);
    }

    if (a.type == CS_T_NIL && b.type == CS_T_NIL) return 0;

    cs_error(vm, "sort(): incompatible types for default comparison");
    *ok = 0;
    return 0;
}

static int compare_with_cmp(cs_vm* vm, cs_value a, cs_value b, cs_value cmp, int* ok) {
    if (!ok) return 0;
    *ok = 1;
    if (cmp.type == CS_T_NIL) return compare_default(vm, a, b, ok);
    if (cmp.type != CS_T_FUNC && cmp.type != CS_T_NATIVE) {
        cs_error(vm, "sort(): comparator must be a function");
        *ok = 0;
        return 0;
    }

    cs_value args[2];
    args[0] = a;
    args[1] = b;
    cs_value ret = cs_nil();
    if (cs_call_value(vm, cmp, 2, args, &ret) != 0) {
        *ok = 0;
        return 0;
    }

    int res = 0;
    if (ret.type == CS_T_INT) res = (ret.as.i < 0) ? -1 : (ret.as.i > 0 ? 1 : 0);
    else if (ret.type == CS_T_FLOAT) res = (ret.as.f < 0.0) ? -1 : (ret.as.f > 0.0 ? 1 : 0);
    else if (ret.type == CS_T_BOOL) res = ret.as.b ? 1 : 0;
    else {
        cs_error(vm, "sort(): comparator must return int/float/bool");
        *ok = 0;
    }
    cs_value_release(ret);
    return res;
}

static int sort_insertion(cs_vm* vm, cs_list_obj* list, cs_value cmp) {
    if (!list || list->len < 2) return 0;
    for (size_t i = 1; i < list->len; i++) {
        cs_value key = list->items[i];
        size_t j = i;
        while (j > 0) {
            int ok = 1;
            int cmp_res = compare_with_cmp(vm, list->items[j - 1], key, cmp, &ok);
            if (!ok) return 1;
            if (cmp_res <= 0) break;
            list->items[j] = list->items[j - 1];
            j--;
        }
        list->items[j] = key;
    }
    return 0;
}

static int sort_quick_impl(cs_vm* vm, cs_list_obj* list, cs_value cmp, int64_t lo, int64_t hi) {
    if (lo >= hi) return 0;
    cs_value pivot = list->items[hi];
    int64_t i = lo;
    for (int64_t j = lo; j < hi; j++) {
        int ok = 1;
        int cmp_res = compare_with_cmp(vm, list->items[j], pivot, cmp, &ok);
        if (!ok) return 1;
        if (cmp_res <= 0) {
            cs_value tmp = list->items[i];
            list->items[i] = list->items[j];
            list->items[j] = tmp;
            i++;
        }
    }
    cs_value tmp = list->items[i];
    list->items[i] = list->items[hi];
    list->items[hi] = tmp;

    if (sort_quick_impl(vm, list, cmp, lo, i - 1) != 0) return 1;
    if (sort_quick_impl(vm, list, cmp, i + 1, hi) != 0) return 1;
    return 0;
}

static int sort_quick(cs_vm* vm, cs_list_obj* list, cs_value cmp) {
    if (!list || list->len < 2) return 0;
    return sort_quick_impl(vm, list, cmp, 0, (int64_t)list->len - 1);
}

static int sort_merge_impl(cs_vm* vm, cs_list_obj* list, cs_value cmp, cs_value* tmp, size_t lo, size_t hi) {
    if (hi - lo <= 1) return 0;
    size_t mid = lo + (hi - lo) / 2;
    if (sort_merge_impl(vm, list, cmp, tmp, lo, mid) != 0) return 1;
    if (sort_merge_impl(vm, list, cmp, tmp, mid, hi) != 0) return 1;

    size_t i = lo, j = mid, k = 0;
    while (i < mid && j < hi) {
        int ok = 1;
        int cmp_res = compare_with_cmp(vm, list->items[i], list->items[j], cmp, &ok);
        if (!ok) return 1;
        if (cmp_res <= 0) tmp[k++] = list->items[i++];
        else tmp[k++] = list->items[j++];
    }
    while (i < mid) tmp[k++] = list->items[i++];
    while (j < hi) tmp[k++] = list->items[j++];
    for (size_t t = 0; t < k; t++) list->items[lo + t] = tmp[t];
    return 0;
}

static int sort_merge(cs_vm* vm, cs_list_obj* list, cs_value cmp) {
    if (!list || list->len < 2) return 0;
    cs_value* tmp = (cs_value*)malloc(sizeof(cs_value) * list->len);
    if (!tmp) { cs_error(vm, "out of memory"); return 1; }
    int rc = sort_merge_impl(vm, list, cmp, tmp, 0, list->len);
    free(tmp);
    return rc;
}

static int nf_sort(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_LIST) {
        cs_error(vm, "sort() requires a list");
        return 1;
    }

    cs_value cmp = cs_nil();
    const char* algo = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i].type == CS_T_STR && !algo) {
            algo = cs_to_cstr(argv[i]);
            continue;
        }
        if ((argv[i].type == CS_T_FUNC || argv[i].type == CS_T_NATIVE || argv[i].type == CS_T_NIL) && cmp.type == CS_T_NIL) {
            cmp = argv[i];
            continue;
        }
        cs_error(vm, "sort() expects optional comparator and/or algorithm name");
        return 1;
    }

    if (!algo) algo = "insertion";

    cs_list_obj* list = (cs_list_obj*)argv[0].as.p;
    if (!list || list->len < 2) { *out = cs_nil(); return 0; }

    int rc = 0;
    if (strcmp(algo, "insertion") == 0) rc = sort_insertion(vm, list, cmp);
    else if (strcmp(algo, "quick") == 0 || strcmp(algo, "quicksort") == 0) rc = sort_quick(vm, list, cmp);
    else if (strcmp(algo, "merge") == 0 || strcmp(algo, "mergesort") == 0) rc = sort_merge(vm, list, cmp);
    else {
        cs_error(vm, "sort(): unknown algorithm");
        return 1;
    }

    if (rc != 0) return 1;
    *out = cs_nil();
    return 0;
}

static int nf_mget(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_MAP) { *out = cs_nil(); return 0; }
    *out = cs_map_get_value(argv[0], argv[1]);
    return 0;
}

static int nf_mset(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_nil();
    if (argc != 3 || argv[0].type != CS_T_MAP) return 0;
    if (cs_map_set_value(argv[0], argv[1], argv[2]) != 0) { cs_error(vm, "out of memory"); return 1; }
    return 0;
}

static int nf_mhas(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_MAP) { *out = cs_bool(0); return 0; }
    *out = cs_bool(cs_map_has_value(argv[0], argv[1]) != 0);
    return 0;
}

static int nf_mdel(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_MAP) { *out = cs_bool(0); return 0; }
    *out = cs_bool(cs_map_del_value(argv[0], argv[1]) == 0);
    return 0;
}

static int nf_keys(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_MAP) { *out = cs_list(vm); return 0; }
    cs_value listv = cs_map_keys(vm, argv[0]);
    if (listv.type == CS_T_NIL) { cs_error(vm, "out of memory"); return 1; }
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

static int nf_str_contains(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { *out = cs_bool(0); return 0; }
    const char* s = ((cs_string*)argv[0].as.p)->data;
    const char* sub = ((cs_string*)argv[1].as.p)->data;
    *out = cs_bool(strstr(s, sub) != NULL);
    return 0;
}

static int nf_str_count(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { *out = cs_int(0); return 0; }
    const char* s = ((cs_string*)argv[0].as.p)->data;
    const char* sub = ((cs_string*)argv[1].as.p)->data;
    if (!*sub) { *out = cs_int(0); return 0; }
    size_t count = 0;
    size_t subl = strlen(sub);
    const char* p = s;
    while ((p = strstr(p, sub)) != NULL) {
        count++;
        p += subl;
    }
    *out = cs_int((int64_t)count);
    return 0;
}

static int nf_str_pad_start(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 2 || argc > 3 || argv[0].type != CS_T_STR || argv[1].type != CS_T_INT) { *out = cs_nil(); return 0; }
    const char* s = ((cs_string*)argv[0].as.p)->data;
    size_t sl = ((cs_string*)argv[0].as.p)->len;
    int64_t width = argv[1].as.i;
    const char* pad = " ";
    size_t padl = 1;
    if (argc == 3 && argv[2].type == CS_T_STR) {
        pad = ((cs_string*)argv[2].as.p)->data;
        padl = ((cs_string*)argv[2].as.p)->len;
    }
    if (width <= (int64_t)sl || padl == 0) { *out = cs_str(vm, s); return 0; }
    size_t need = (size_t)width - sl;
    size_t total = need + sl;
    char* buf = (char*)malloc(total + 1);
    if (!buf) { cs_error(vm, "out of memory"); return 1; }
    size_t w = 0;
    while (need > 0) {
        size_t n = padl < need ? padl : need;
        memcpy(buf + w, pad, n);
        w += n;
        need -= n;
    }
    memcpy(buf + w, s, sl);
    w += sl;
    buf[w] = 0;
    *out = cs_str_take(vm, buf, (uint64_t)w);
    return 0;
}

static int nf_str_pad_end(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 2 || argc > 3 || argv[0].type != CS_T_STR || argv[1].type != CS_T_INT) { *out = cs_nil(); return 0; }
    const char* s = ((cs_string*)argv[0].as.p)->data;
    size_t sl = ((cs_string*)argv[0].as.p)->len;
    int64_t width = argv[1].as.i;
    const char* pad = " ";
    size_t padl = 1;
    if (argc == 3 && argv[2].type == CS_T_STR) {
        pad = ((cs_string*)argv[2].as.p)->data;
        padl = ((cs_string*)argv[2].as.p)->len;
    }
    if (width <= (int64_t)sl || padl == 0) { *out = cs_str(vm, s); return 0; }
    size_t need = (size_t)width - sl;
    size_t total = need + sl;
    char* buf = (char*)malloc(total + 1);
    if (!buf) { cs_error(vm, "out of memory"); return 1; }
    size_t w = 0;
    memcpy(buf + w, s, sl);
    w += sl;
    while (need > 0) {
        size_t n = padl < need ? padl : need;
        memcpy(buf + w, pad, n);
        w += n;
        need -= n;
    }
    buf[w] = 0;
    *out = cs_str_take(vm, buf, (uint64_t)w);
    return 0;
}

static int nf_str_reverse(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }
    cs_string* st = (cs_string*)argv[0].as.p;
    size_t sl = st->len;
    char* buf = (char*)malloc(sl + 1);
    if (!buf) { cs_error(vm, "out of memory"); return 1; }
    for (size_t i = 0; i < sl; i++) {
        buf[i] = st->data[sl - 1 - i];
    }
    buf[sl] = 0;
    *out = cs_str_take(vm, buf, (uint64_t)sl);
    return 0;
}

#if !defined(_WIN32)
static int regex_compile_or_error(cs_vm* vm, regex_t* re, const char* pattern) {
    if (!re) return 0;
    int rc = regcomp(re, pattern ? pattern : "", REG_EXTENDED);
    if (rc == 0) return 1;
    size_t need = regerror(rc, re, NULL, 0);
    char* buf = (char*)malloc(need + 16);
    if (!buf) { cs_error(vm, "regex error"); return 0; }
    regerror(rc, re, buf, need);
    cs_error(vm, buf);
    free(buf);
    return 0;
}

static cs_value cs_str_slice_range(cs_vm* vm, const char* s, size_t start, size_t end) {
    if (!s || end < start) return cs_str(vm, "");
    size_t len = end - start;
    char* buf = (char*)malloc(len + 1);
    if (!buf) return cs_nil();
    memcpy(buf, s + start, len);
    buf[len] = 0;
    return cs_str_take(vm, buf, (uint64_t)len);
}

static int regex_buf_append(char** buf, size_t* len, size_t* cap, const char* s, size_t n) {
    if (!s || n == 0) return 1;
    size_t need = *len + n + 1;
    if (need > *cap) {
        size_t nc = *cap ? *cap : 64;
        while (nc < need) nc *= 2;
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

static cs_value regex_match_map(cs_vm* vm, const char* text, size_t offset, regmatch_t* matches, size_t nmatch) {
    if (!matches || nmatch == 0 || matches[0].rm_so < 0) return cs_nil();
    size_t start = offset + (size_t)matches[0].rm_so;
    size_t end = offset + (size_t)matches[0].rm_eo;
    cs_value mapv = cs_map(vm);
    if (!mapv.as.p) { cs_error(vm, "out of memory"); return cs_nil(); }

    cs_value matchv = cs_str_slice_range(vm, text, start, end);
    cs_value groups = cs_list(vm);
    if (!groups.as.p || matchv.type == CS_T_NIL) {
        cs_value_release(matchv);
        cs_value_release(groups);
        cs_value_release(mapv);
        cs_error(vm, "out of memory");
        return cs_nil();
    }

    for (size_t i = 1; i < nmatch; i++) {
        if (matches[i].rm_so < 0 || matches[i].rm_eo < 0) {
            if (cs_list_push(groups, cs_nil()) != 0) {
                cs_value_release(matchv);
                cs_value_release(groups);
                cs_value_release(mapv);
                cs_error(vm, "out of memory");
                return cs_nil();
            }
            continue;
        }
        size_t gs = offset + (size_t)matches[i].rm_so;
        size_t ge = offset + (size_t)matches[i].rm_eo;
        cs_value gv = cs_str_slice_range(vm, text, gs, ge);
        if (gv.type == CS_T_NIL || cs_list_push(groups, gv) != 0) {
            cs_value_release(gv);
            cs_value_release(matchv);
            cs_value_release(groups);
            cs_value_release(mapv);
            cs_error(vm, "out of memory");
            return cs_nil();
        }
        cs_value_release(gv);
    }

    cs_map_set(mapv, "start", cs_int((int64_t)start));
    cs_map_set(mapv, "end", cs_int((int64_t)end));
    cs_map_set(mapv, "match", matchv);
    cs_map_set(mapv, "groups", groups);
    cs_value_release(matchv);
    cs_value_release(groups);
    return mapv;
}

static int nf_regex_is_match(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { *out = cs_bool(0); return 0; }
    const char* pattern = cs_to_cstr(argv[0]);
    const char* text = cs_to_cstr(argv[1]);
    regex_t re;
    if (!regex_compile_or_error(vm, &re, pattern)) return 1;
    regmatch_t m;
    int rc = regexec(&re, text, 1, &m, 0);
    regfree(&re);
    *out = cs_bool(rc == 0);
    return 0;
}

static int nf_regex_match(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { *out = cs_bool(0); return 0; }
    const char* pattern = cs_to_cstr(argv[0]);
    const char* text = cs_to_cstr(argv[1]);
    regex_t re;
    if (!regex_compile_or_error(vm, &re, pattern)) return 1;
    regmatch_t m;
    int rc = regexec(&re, text, 1, &m, 0);
    regfree(&re);
    if (rc != 0) { *out = cs_bool(0); return 0; }
    size_t len = strlen(text);
    *out = cs_bool(m.rm_so == 0 && (size_t)m.rm_eo == len);
    return 0;
}

static int nf_regex_find(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { *out = cs_nil(); return 0; }
    const char* pattern = cs_to_cstr(argv[0]);
    const char* text = cs_to_cstr(argv[1]);
    regex_t re;
    if (!regex_compile_or_error(vm, &re, pattern)) return 1;
    size_t nmatch = (size_t)re.re_nsub + 1;
    regmatch_t* matches = (regmatch_t*)calloc(nmatch, sizeof(regmatch_t));
    if (!matches) { regfree(&re); cs_error(vm, "out of memory"); return 1; }
    int rc = regexec(&re, text, nmatch, matches, 0);
    if (rc != 0) {
        free(matches);
        regfree(&re);
        *out = cs_nil();
        return 0;
    }
    cs_value mv = regex_match_map(vm, text, 0, matches, nmatch);
    free(matches);
    regfree(&re);
    if (mv.type == CS_T_NIL) return 1;
    *out = mv;
    return 0;
}

static int nf_regex_find_all(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { *out = cs_list(vm); return 0; }
    const char* pattern = cs_to_cstr(argv[0]);
    const char* text = cs_to_cstr(argv[1]);
    regex_t re;
    if (!regex_compile_or_error(vm, &re, pattern)) return 1;
    size_t nmatch = (size_t)re.re_nsub + 1;
    regmatch_t* matches = (regmatch_t*)calloc(nmatch, sizeof(regmatch_t));
    if (!matches) { regfree(&re); cs_error(vm, "out of memory"); return 1; }

    cs_value listv = cs_list(vm);
    if (!listv.as.p) { free(matches); regfree(&re); cs_error(vm, "out of memory"); return 1; }

    size_t text_len = strlen(text);
    size_t offset = 0;
    while (offset <= text_len) {
        const char* sub = text + offset;
        int rc = regexec(&re, sub, nmatch, matches, 0);
        if (rc != 0 || matches[0].rm_so < 0) break;

        cs_value mv = regex_match_map(vm, text, offset, matches, nmatch);
        if (mv.type == CS_T_NIL || cs_list_push(listv, mv) != 0) {
            cs_value_release(mv);
            cs_value_release(listv);
            free(matches);
            regfree(&re);
            cs_error(vm, "out of memory");
            return 1;
        }
        cs_value_release(mv);

        size_t mstart = offset + (size_t)matches[0].rm_so;
        size_t mend = offset + (size_t)matches[0].rm_eo;
        if (mend == mstart) {
            if (mend >= text_len) break;
            offset = mend + 1;
        } else {
            offset = mend;
        }
    }

    free(matches);
    regfree(&re);
    *out = listv;
    return 0;
}

static int nf_regex_replace(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 3 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR || argv[2].type != CS_T_STR) {
        *out = cs_nil();
        return 0;
    }
    const char* pattern = cs_to_cstr(argv[0]);
    const char* text = cs_to_cstr(argv[1]);
    const char* repl = cs_to_cstr(argv[2]);
    regex_t re;
    if (!regex_compile_or_error(vm, &re, pattern)) return 1;
    size_t nmatch = (size_t)re.re_nsub + 1;
    regmatch_t* matches = (regmatch_t*)calloc(nmatch, sizeof(regmatch_t));
    if (!matches) { regfree(&re); cs_error(vm, "out of memory"); return 1; }

    char* buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    size_t text_len = strlen(text);
    size_t offset = 0;
    size_t last = 0;

    while (offset <= text_len) {
        const char* sub = text + offset;
        int rc = regexec(&re, sub, nmatch, matches, 0);
        if (rc != 0 || matches[0].rm_so < 0) break;
        size_t mstart = offset + (size_t)matches[0].rm_so;
        size_t mend = offset + (size_t)matches[0].rm_eo;

        if (mstart > last) {
            if (!regex_buf_append(&buf, &len, &cap, text + last, mstart - last)) {
                free(buf);
                free(matches);
                regfree(&re);
                cs_error(vm, "out of memory");
                return 1;
            }
        }
        if (!regex_buf_append(&buf, &len, &cap, repl, strlen(repl))) {
            free(buf);
            free(matches);
            regfree(&re);
            cs_error(vm, "out of memory");
            return 1;
        }

        last = mend;
        if (mend == mstart) {
            if (mend >= text_len) break;
            offset = mend + 1;
        } else {
            offset = mend;
        }
    }

    if (last < text_len) {
        if (!regex_buf_append(&buf, &len, &cap, text + last, text_len - last)) {
            free(buf);
            free(matches);
            regfree(&re);
            cs_error(vm, "out of memory");
            return 1;
        }
    }

    free(matches);
    regfree(&re);

    if (!buf) {
        *out = cs_str(vm, "");
        return 0;
    }
    *out = cs_str_take(vm, buf, (uint64_t)len);
    return 0;
}
#else
static int nf_regex_is_match(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (out) *out = cs_bool(0);
    cs_error(vm, "regex not supported on this platform");
    return 1;
}

static int nf_regex_match(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    return nf_regex_is_match(vm, ud, argc, argv, out);
}

static int nf_regex_find(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    return nf_regex_is_match(vm, ud, argc, argv, out);
}

static int nf_regex_find_all(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    return nf_regex_is_match(vm, ud, argc, argv, out);
}

static int nf_regex_replace(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    return nf_regex_is_match(vm, ud, argc, argv, out);
}
#endif

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

static int nf_read_file(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }

    const char* path = ((cs_string*)argv[0].as.p)->data;
    char* resolved = resolve_path_alloc(vm, path);
    if (!resolved) { cs_error(vm, "out of memory"); return 1; }

    FILE* f = fopen(resolved, "rb");
    free(resolved);
    if (!f) { *out = cs_nil(); return 0; }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); *out = cs_nil(); return 0; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); *out = cs_nil(); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); *out = cs_nil(); return 0; }

    size_t n = (size_t)sz;
    char* buf = (char*)malloc(n + 1);
    if (!buf) { fclose(f); cs_error(vm, "out of memory"); return 1; }
    size_t r = fread(buf, 1, n, f);
    fclose(f);
    if (r != n) { free(buf); *out = cs_nil(); return 0; }
    buf[n] = 0;
    *out = cs_str_take(vm, buf, (uint64_t)n);
    return 0;
}

static int nf_write_file(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { *out = cs_bool(0); return 0; }

    const char* path = ((cs_string*)argv[0].as.p)->data;
    const char* data = ((cs_string*)argv[1].as.p)->data;
    size_t len = ((cs_string*)argv[1].as.p)->len;

    char* resolved = resolve_path_alloc(vm, path);
    if (!resolved) { cs_error(vm, "out of memory"); return 1; }

    FILE* f = fopen(resolved, "wb");
    free(resolved);
    if (!f) { *out = cs_bool(0); return 0; }

    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    *out = cs_bool(w == len);
    return 0;
}

static int nf_exists(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_bool(0); return 0; }

    const char* path = ((cs_string*)argv[0].as.p)->data;
    char* resolved = resolve_path_alloc(vm, path);
    if (!resolved) { cs_error(vm, "out of memory"); return 1; }

    CS_STAT_STRUCT st;
    int ok = fs_stat(resolved, &st);
    free(resolved);
    *out = cs_bool(ok);
    return 0;
}

static int nf_is_dir(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_bool(0); return 0; }

    const char* path = ((cs_string*)argv[0].as.p)->data;
    char* resolved = resolve_path_alloc(vm, path);
    if (!resolved) { cs_error(vm, "out of memory"); return 1; }
    int ok = fs_is_dir(resolved);
    free(resolved);
    *out = cs_bool(ok);
    return 0;
}

static int nf_is_file(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_bool(0); return 0; }

    const char* path = ((cs_string*)argv[0].as.p)->data;
    char* resolved = resolve_path_alloc(vm, path);
    if (!resolved) { cs_error(vm, "out of memory"); return 1; }
    int ok = fs_is_file(resolved);
    free(resolved);
    *out = cs_bool(ok);
    return 0;
}

static int nf_list_dir(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }

    const char* path = ((cs_string*)argv[0].as.p)->data;
    char* resolved = resolve_path_alloc(vm, path);
    if (!resolved) { cs_error(vm, "out of memory"); return 1; }

    cs_value listv = cs_list(vm);
    if (listv.type != CS_T_LIST) { free(resolved); *out = cs_nil(); return 0; }

#if defined(_WIN32)
    size_t n = strlen(resolved);
    const char* suffix = (n > 0 && (resolved[n - 1] == '/' || resolved[n - 1] == '\\')) ? "*" : "\\*";
    size_t sn = strlen(suffix);
    char* pattern = (char*)malloc(n + sn + 1);
    if (!pattern) { free(resolved); cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
    memcpy(pattern, resolved, n);
    memcpy(pattern + n, suffix, sn + 1);

    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) { free(resolved); cs_value_release(listv); *out = cs_nil(); return 0; }
    do {
        const char* name = data.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        cs_value sv = cs_str(vm, name);
        if (cs_list_push(listv, sv) != 0) { cs_value_release(sv); FindClose(h); free(resolved); cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
        cs_value_release(sv);
    } while (FindNextFileA(h, &data));
    FindClose(h);
#else
    DIR* dir = opendir(resolved);
    if (!dir) { free(resolved); cs_value_release(listv); *out = cs_nil(); return 0; }
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        const char* name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        cs_value sv = cs_str(vm, name);
        if (cs_list_push(listv, sv) != 0) { cs_value_release(sv); closedir(dir); free(resolved); cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
        cs_value_release(sv);
    }
    closedir(dir);
#endif

    free(resolved);
    *out = listv;
    return 0;
}

static int nf_mkdir(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_bool(0); return 0; }

    const char* path = ((cs_string*)argv[0].as.p)->data;
    char* resolved = resolve_path_alloc(vm, path);
    if (!resolved) { cs_error(vm, "out of memory"); return 1; }

    if (fs_is_dir(resolved)) { free(resolved); *out = cs_bool(1); return 0; }

#if defined(_WIN32)
    int ok = _mkdir(resolved) == 0;
#else
    int ok = mkdir(resolved, 0777) == 0;
#endif
    free(resolved);
    *out = cs_bool(ok);
    return 0;
}

static int nf_rm(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_bool(0); return 0; }

    const char* path = ((cs_string*)argv[0].as.p)->data;
    char* resolved = resolve_path_alloc(vm, path);
    if (!resolved) { cs_error(vm, "out of memory"); return 1; }

    int ok = 0;
    if (fs_is_dir(resolved)) {
#if defined(_WIN32)
        ok = _rmdir(resolved) == 0;
#else
        ok = rmdir(resolved) == 0;
#endif
    } else {
        ok = remove(resolved) == 0;
    }

    free(resolved);
    *out = cs_bool(ok);
    return 0;
}

static int nf_rename(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { *out = cs_bool(0); return 0; }

    const char* a = ((cs_string*)argv[0].as.p)->data;
    const char* b = ((cs_string*)argv[1].as.p)->data;

    char* ra = resolve_path_alloc(vm, a);
    char* rb = resolve_path_alloc(vm, b);
    if (!ra || !rb) {
        free(ra);
        free(rb);
        cs_error(vm, "out of memory");
        return 1;
    }

    int ok = rename(ra, rb) == 0;
    free(ra);
    free(rb);
    *out = cs_bool(ok);
    return 0;
}

static int nf_cwd(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    (void)argc;
    (void)argv;
    if (!out) return 0;
    *out = cs_str(vm, vm_current_dir(vm));
    return 0;
}

static int nf_chdir(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_bool(0); return 0; }

    const char* path = ((cs_string*)argv[0].as.p)->data;
    char* resolved = resolve_path_alloc(vm, path);
    if (!resolved) { cs_error(vm, "out of memory"); return 1; }

    if (!fs_is_dir(resolved)) { free(resolved); *out = cs_bool(0); return 0; }
    int ok = vm_set_current_dir(vm, resolved);
    free(resolved);
    *out = cs_bool(ok);
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

// ---------- JSON helpers ----------
typedef struct {
    const char* s;
    size_t len;
    size_t pos;
    cs_vm* vm;
} json_parser;

static void json_skip_ws(json_parser* p) {
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { p->pos++; continue; }
        break;
    }
}

static int json_match(json_parser* p, const char* lit) {
    size_t n = strlen(lit);
    if (p->pos + n > p->len) return 0;
    if (memcmp(p->s + p->pos, lit, n) != 0) return 0;
    p->pos += n;
    return 1;
}

static int json_hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static cs_value json_parse_value(json_parser* p, int* ok);

static cs_value json_parse_string(json_parser* p, int* ok) {
    if (!ok) return cs_nil();
    *ok = 1;
    if (p->pos >= p->len || p->s[p->pos] != '"') { *ok = 0; return cs_nil(); }
    p->pos++;

    char* buf = NULL;
    size_t len = 0, cap = 0;
    while (p->pos < p->len) {
        char c = p->s[p->pos++];
        if (c == '"') break;
        if ((unsigned char)c < 0x20) { *ok = 0; break; }
        if (c == '\\') {
            if (p->pos >= p->len) { *ok = 0; break; }
            char e = p->s[p->pos++];
            switch (e) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    if (p->pos + 4 > p->len) { *ok = 0; break; }
                    int h1 = json_hex_val(p->s[p->pos++]);
                    int h2 = json_hex_val(p->s[p->pos++]);
                    int h3 = json_hex_val(p->s[p->pos++]);
                    int h4 = json_hex_val(p->s[p->pos++]);
                    if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) { *ok = 0; break; }
                    int code = (h1 << 12) | (h2 << 8) | (h3 << 4) | h4;
                    if (code < 0x80) {
                        c = (char)code;
                    } else {
                        c = '?';
                    }
                    break;
                }
                default:
                    c = e;
                    break;
            }
        }
        if (!sb_append(&buf, &len, &cap, &c, 1)) { *ok = 0; break; }
    }

    if (!*ok) { free(buf); return cs_nil(); }
    if (!buf) {
        char* empty = (char*)malloc(1);
        if (!empty) { *ok = 0; return cs_nil(); }
        empty[0] = 0;
        return cs_str_take(p->vm, empty, 0);
    }
    return cs_str_take(p->vm, buf, (uint64_t)len);
}

static cs_value json_parse_number(json_parser* p, int* ok) {
    if (!ok) return cs_nil();
    *ok = 1;
    size_t start = p->pos;
    if (p->pos < p->len && (p->s[p->pos] == '-' || p->s[p->pos] == '+')) p->pos++;
    while (p->pos < p->len && isdigit((unsigned char)p->s[p->pos])) p->pos++;
    if (p->pos < p->len && p->s[p->pos] == '.') {
        p->pos++;
        while (p->pos < p->len && isdigit((unsigned char)p->s[p->pos])) p->pos++;
    }
    if (p->pos < p->len && (p->s[p->pos] == 'e' || p->s[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->s[p->pos] == '+' || p->s[p->pos] == '-')) p->pos++;
        while (p->pos < p->len && isdigit((unsigned char)p->s[p->pos])) p->pos++;
    }

    size_t n = p->pos - start;
    if (n == 0) { *ok = 0; return cs_nil(); }
    int is_float = 0;
    for (size_t i = start; i < p->pos; i++) {
        char c = p->s[i];
        if (c == '.' || c == 'e' || c == 'E') { is_float = 1; break; }
    }

    if (!is_float) {
        errno = 0;
        long long v = strtoll(p->s + start, NULL, 10);
        if (errno == 0) return cs_int((int64_t)v);
    }

    errno = 0;
    double d = strtod(p->s + start, NULL);
    if (errno != 0) { *ok = 0; return cs_nil(); }
    return cs_float(d);
}

static cs_value json_parse_array(json_parser* p, int* ok) {
    if (!ok) return cs_nil();
    *ok = 1;
    if (p->pos >= p->len || p->s[p->pos] != '[') { *ok = 0; return cs_nil(); }
    p->pos++;
    json_skip_ws(p);

    cs_value listv = cs_list(p->vm);
    if (listv.type != CS_T_LIST) { *ok = 0; return cs_nil(); }

    if (p->pos < p->len && p->s[p->pos] == ']') { p->pos++; return listv; }

    while (p->pos < p->len) {
        cs_value v = json_parse_value(p, ok);
        if (!*ok) { cs_value_release(listv); return cs_nil(); }
        if (cs_list_push(listv, v) != 0) { cs_value_release(v); cs_value_release(listv); cs_error(p->vm, "out of memory"); *ok = 0; return cs_nil(); }
        cs_value_release(v);

        json_skip_ws(p);
        if (p->pos < p->len && p->s[p->pos] == ',') { p->pos++; json_skip_ws(p); continue; }
        if (p->pos < p->len && p->s[p->pos] == ']') { p->pos++; return listv; }
        break;
    }

    cs_value_release(listv);
    *ok = 0;
    return cs_nil();
}

static cs_value json_parse_object(json_parser* p, int* ok) {
    if (!ok) return cs_nil();
    *ok = 1;
    if (p->pos >= p->len || p->s[p->pos] != '{') { *ok = 0; return cs_nil(); }
    p->pos++;
    json_skip_ws(p);

    cs_value mapv = cs_map(p->vm);
    if (mapv.type != CS_T_MAP) { *ok = 0; return cs_nil(); }

    if (p->pos < p->len && p->s[p->pos] == '}') { p->pos++; return mapv; }

    while (p->pos < p->len) {
        cs_value keyv = json_parse_string(p, ok);
        if (!*ok || keyv.type != CS_T_STR) { cs_value_release(mapv); return cs_nil(); }

        json_skip_ws(p);
        if (p->pos >= p->len || p->s[p->pos] != ':') { cs_value_release(keyv); cs_value_release(mapv); *ok = 0; return cs_nil(); }
        p->pos++;
        json_skip_ws(p);

        cs_value val = json_parse_value(p, ok);
        if (!*ok) { cs_value_release(keyv); cs_value_release(mapv); return cs_nil(); }

        if (cs_map_set(mapv, cs_to_cstr(keyv), val) != 0) {
            cs_value_release(keyv);
            cs_value_release(val);
            cs_value_release(mapv);
            cs_error(p->vm, "out of memory");
            *ok = 0;
            return cs_nil();
        }
        cs_value_release(keyv);
        cs_value_release(val);

        json_skip_ws(p);
        if (p->pos < p->len && p->s[p->pos] == ',') { p->pos++; json_skip_ws(p); continue; }
        if (p->pos < p->len && p->s[p->pos] == '}') { p->pos++; return mapv; }
        break;
    }

    cs_value_release(mapv);
    *ok = 0;
    return cs_nil();
}

static cs_value json_parse_value(json_parser* p, int* ok) {
    if (!ok) return cs_nil();
    *ok = 1;
    json_skip_ws(p);
    if (p->pos >= p->len) { *ok = 0; return cs_nil(); }
    char c = p->s[p->pos];

    if (c == '"') return json_parse_string(p, ok);
    if (c == '[') return json_parse_array(p, ok);
    if (c == '{') return json_parse_object(p, ok);
    if (c == 't') { if (json_match(p, "true")) return cs_bool(1); }
    if (c == 'f') { if (json_match(p, "false")) return cs_bool(0); }
    if (c == 'n') { if (json_match(p, "null")) return cs_nil(); }
    if (c == '-' || c == '+' || isdigit((unsigned char)c)) return json_parse_number(p, ok);

    *ok = 0;
    return cs_nil();
}

static int json_append_escaped(char** buf, size_t* len, size_t* cap, const char* s) {
    if (!buf || !len || !cap) return 0;
    if (!sb_append(buf, len, cap, "\"", 1)) return 0;
    for (size_t i = 0; s && s[i]; i++) {
        char c = s[i];
        switch (c) {
            case '"': if (!sb_append(buf, len, cap, "\\\"", 2)) return 0; break;
            case '\\': if (!sb_append(buf, len, cap, "\\\\", 2)) return 0; break;
            case '\b': if (!sb_append(buf, len, cap, "\\b", 2)) return 0; break;
            case '\f': if (!sb_append(buf, len, cap, "\\f", 2)) return 0; break;
            case '\n': if (!sb_append(buf, len, cap, "\\n", 2)) return 0; break;
            case '\r': if (!sb_append(buf, len, cap, "\\r", 2)) return 0; break;
            case '\t': if (!sb_append(buf, len, cap, "\\t", 2)) return 0; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char tmp[7];
                    snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned char)c);
                    if (!sb_append(buf, len, cap, tmp, 6)) return 0;
                } else {
                    if (!sb_append(buf, len, cap, &c, 1)) return 0;
                }
                break;
        }
    }
    if (!sb_append(buf, len, cap, "\"", 1)) return 0;
    return 1;
}

static int json_stringify_value(cs_vm* vm, cs_value v, char** buf, size_t* len, size_t* cap, void** stack, size_t depth) {
    switch (v.type) {
        case CS_T_NIL:
            return sb_append(buf, len, cap, "null", 4);
        case CS_T_BOOL:
            return sb_append(buf, len, cap, v.as.b ? "true" : "false", v.as.b ? 4 : 5);
        case CS_T_INT: {
            char tmp[32];
            int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)v.as.i);
            return sb_append(buf, len, cap, tmp, (size_t)n);
        }
        case CS_T_FLOAT: {
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%.17g", v.as.f);
            return sb_append(buf, len, cap, tmp, (size_t)n);
        }
        case CS_T_STR:
            return json_append_escaped(buf, len, cap, cs_to_cstr(v));
        case CS_T_LIST: {
            cs_list_obj* list = (cs_list_obj*)v.as.p;
            for (size_t i = 0; i < depth; i++) if (stack[i] == list) { cs_error(vm, "json_stringify(): cycle detected"); return 0; }
            stack[depth] = list;
            if (!sb_append(buf, len, cap, "[", 1)) return 0;
            for (size_t i = 0; i < list->len; i++) {
                if (i > 0 && !sb_append(buf, len, cap, ",", 1)) return 0;
                if (!json_stringify_value(vm, list->items[i], buf, len, cap, stack, depth + 1)) return 0;
            }
            return sb_append(buf, len, cap, "]", 1);
        }
        case CS_T_MAP: {
            cs_map_obj* m = (cs_map_obj*)v.as.p;
            for (size_t i = 0; i < depth; i++) if (stack[i] == m) { cs_error(vm, "json_stringify(): cycle detected"); return 0; }
            stack[depth] = m;
            if (!sb_append(buf, len, cap, "{", 1)) return 0;
            int first = 1;
            for (size_t i = 0; i < m->cap; i++) {
                if (!m->entries[i].in_use) continue;
                if (!first && !sb_append(buf, len, cap, ",", 1)) return 0;
                first = 0;
                char kbuf[128];
                const char* kstr = value_repr(m->entries[i].key, kbuf, sizeof(kbuf));
                if (!json_append_escaped(buf, len, cap, kstr)) return 0;
                if (!sb_append(buf, len, cap, ":", 1)) return 0;
                if (!json_stringify_value(vm, m->entries[i].val, buf, len, cap, stack, depth + 1)) return 0;
            }
            return sb_append(buf, len, cap, "}", 1);
        }
        default:
            cs_error(vm, "json_stringify(): unsupported value type");
            return 0;
    }
}

static int nf_json_parse(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }

    const cs_string* s = (const cs_string*)argv[0].as.p;
    json_parser p;
    p.s = s ? s->data : "";
    p.len = s ? s->len : 0;
    p.pos = 0;
    p.vm = vm;

    int ok = 1;
    cs_value v = json_parse_value(&p, &ok);
    if (!ok) { cs_error(vm, "json_parse(): invalid JSON"); return 1; }
    json_skip_ws(&p);
    if (p.pos != p.len) { cs_value_release(v); cs_error(vm, "json_parse(): trailing characters"); return 1; }
    *out = v;
    return 0;
}

static int nf_json_stringify(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_nil(); return 0; }

    char* buf = NULL;
    size_t len = 0, cap = 0;
    void* stack[128];
    if (!json_stringify_value(vm, argv[0], &buf, &len, &cap, stack, 0)) {
        free(buf);
        return 1;
    }
    if (!buf) {
        char* empty = (char*)malloc(1);
        if (!empty) { cs_error(vm, "out of memory"); return 1; }
        empty[0] = 0;
        *out = cs_str_take(vm, empty, 0);
        return 0;
    }
    *out = cs_str_take(vm, buf, (uint64_t)len);
    return 0;
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

static uint64_t stdlib_now_ms(void) {
#if !defined(_WIN32)
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)(tv.tv_usec / 1000);
#else
    return (uint64_t)(clock() * 1000 / (uint64_t)CLOCKS_PER_SEC);
#endif
}

static int nf_sleep(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_INT) { *out = cs_nil(); return 0; }
    int64_t ms = argv[0].as.i;
    cs_value p = cs_promise_new(vm);
    if (p.type != CS_T_PROMISE) { cs_error(vm, "out of memory"); return 1; }
    if (ms <= 0) {
        cs_promise_resolve(vm, p, cs_nil());
        *out = p;
        return 0;
    }
    uint64_t due = stdlib_now_ms() + (uint64_t)ms;
    cs_schedule_timer(vm, p, due);
    *out = p;
    return 0;
}

static int nf_promise(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    cs_value p = cs_promise_new(vm);
    if (p.type != CS_T_PROMISE) { cs_error(vm, "out of memory"); return 1; }
    *out = p;
    return 0;
}

static int nf_resolve(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_bool(0);
    if (argc < 1) return 0;
    cs_value val = (argc >= 2) ? argv[1] : cs_nil();
    int ok = cs_promise_resolve(vm, argv[0], val);
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_reject(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_bool(0);
    if (argc < 1) return 0;
    cs_value val = (argc >= 2) ? argv[1] : cs_nil();
    int ok = cs_promise_reject(vm, argv[0], val);
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_is_promise(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_bool(0); return 0; }
    *out = cs_bool(argv[0].type == CS_T_PROMISE);
    return 0;
}

// Native function: await_all(promises) -> promise<list>
static int nf_await_all(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    cs_value result_list = cs_list(vm);
    if (argc < 1 || argv[0].type != CS_T_LIST) {
        cs_value p = cs_promise_new(vm);
        cs_promise_resolve(vm, p, result_list);
        *out = p;
        cs_value_release(result_list);
        return 0;
    }

    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    int ok = 1;
    for (size_t i = 0; i < l->len; i++) {
        cs_value v = l->items[i];
        if (v.type == CS_T_PROMISE) {
            cs_value awaited = cs_wait_promise(vm, v, &ok);
            if (!ok) {
                cs_value_release(awaited);
                return 1;
            }
            cs_list_push(result_list, awaited);
            cs_value_release(awaited);
        } else {
            cs_list_push(result_list, v);
        }
    }

    cs_value p = cs_promise_new(vm);
    cs_promise_resolve(vm, p, result_list);
    *out = p;
    cs_value_release(result_list);
    return 0;
}

// Native function: await_any(promises) -> promise<T>
static int nf_await_any(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_LIST) {
        cs_value p = cs_promise_new(vm);
        cs_promise_resolve(vm, p, cs_nil());
        *out = p;
        return 0;
    }

    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    int ok = 1;
    for (size_t i = 0; i < l->len; i++) {
        cs_value v = l->items[i];
        if (v.type == CS_T_PROMISE) {
            cs_value awaited = cs_wait_promise(vm, v, &ok);
            if (!ok) {
                cs_value_release(awaited);
                return 1;
            }
            cs_value p = cs_promise_new(vm);
            cs_promise_resolve(vm, p, awaited);
            *out = p;
            cs_value_release(awaited);
            return 0;
        } else {
            cs_value p = cs_promise_new(vm);
            cs_promise_resolve(vm, p, v);
            *out = p;
            return 0;
        }
    }

    cs_value p = cs_promise_new(vm);
    cs_promise_resolve(vm, p, cs_nil());
    *out = p;
    return 0;
}

// Native function: await_all_settled(promises) -> promise<list>
static int nf_await_all_settled(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    cs_value result_list = cs_list(vm);
    if (argc < 1 || argv[0].type != CS_T_LIST) {
        cs_value p = cs_promise_new(vm);
        cs_promise_resolve(vm, p, result_list);
        *out = p;
        cs_value_release(result_list);
        return 0;
    }

    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    int ok = 1;
    for (size_t i = 0; i < l->len; i++) {
        cs_value entry = cs_map(vm);
        cs_value v = l->items[i];
        if (v.type == CS_T_PROMISE) {
            cs_value awaited = cs_wait_promise(vm, v, &ok);
            if (!ok) {
                cs_value_release(awaited);
                return 1;
            }
            cs_map_set(entry, "status", cs_str(vm, "fulfilled"));
            cs_map_set(entry, "value", awaited);
            cs_value_release(awaited);
        } else {
            cs_map_set(entry, "status", cs_str(vm, "fulfilled"));
            cs_map_set(entry, "value", v);
        }
        cs_list_push(result_list, entry);
        cs_value_release(entry);
    }

    cs_value p = cs_promise_new(vm);
    cs_promise_resolve(vm, p, result_list);
    *out = p;
    cs_value_release(result_list);
    return 0;
}

// Native function: timeout(promise, ms) -> promise
static int nf_timeout(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 2 || argv[0].type != CS_T_PROMISE || argv[1].type != CS_T_INT) {
        *out = cs_promise_new(vm);
        cs_promise_resolve(vm, *out, cs_nil());
        return 0;
    }
    int64_t ms = argv[1].as.i;
    if (ms <= 0) {
        *out = argv[0];
        return 0;
    }

    int ok = 1;
    cs_value awaited = cs_wait_promise(vm, argv[0], &ok);
    if (!ok) {
        cs_value_release(awaited);
        return 1;
    }
    cs_value p = cs_promise_new(vm);
    cs_promise_resolve(vm, p, awaited);
    *out = p;
    cs_value_release(awaited);
    return 0;
}

static int nf_values(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_MAP) { *out = cs_list(vm); return 0; }
    cs_map_obj* m = (cs_map_obj*)argv[0].as.p;
    cs_value listv = cs_list(vm);
    if (!listv.as.p) { cs_error(vm, "out of memory"); return 1; }
    cs_list_obj* l = (cs_list_obj*)listv.as.p;
    if (!list_ensure(l, m->len)) { cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
    for (size_t i = 0; i < m->cap; i++) {
        if (!m->entries[i].in_use) continue;
        l->items[l->len++] = cs_value_copy(m->entries[i].val);
    }
    *out = listv;
    return 0;
}

static int nf_items(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_MAP) { *out = cs_list(vm); return 0; }
    cs_map_obj* m = (cs_map_obj*)argv[0].as.p;
    cs_value outer = cs_list(vm);
    if (!outer.as.p) { cs_error(vm, "out of memory"); return 1; }
    cs_list_obj* ol = (cs_list_obj*)outer.as.p;
    if (!list_ensure(ol, m->len)) { cs_value_release(outer); cs_error(vm, "out of memory"); return 1; }

    for (size_t i = 0; i < m->cap; i++) {
        if (!m->entries[i].in_use) continue;
        cs_value pair = cs_list(vm);
        if (!pair.as.p) { cs_value_release(outer); cs_error(vm, "out of memory"); return 1; }
        cs_list_obj* pl = (cs_list_obj*)pair.as.p;
        if (!list_ensure(pl, 2)) { cs_value_release(pair); cs_value_release(outer); cs_error(vm, "out of memory"); return 1; }

        pl->items[pl->len++] = cs_value_copy(m->entries[i].key);
        pl->items[pl->len++] = cs_value_copy(m->entries[i].val);

        ol->items[ol->len++] = pair;
    }

    *out = outer;
    return 0;
}

static int nf_enumerate(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_LIST) { *out = cs_list(vm); return 0; }
    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    cs_value outer = cs_list(vm);
    if (!outer.as.p) { cs_error(vm, "out of memory"); return 1; }
    for (size_t i = 0; l && i < l->len; i++) {
        cs_value pair = cs_list(vm);
        if (!pair.as.p) { cs_value_release(outer); cs_error(vm, "out of memory"); return 1; }
        if (cs_list_push(pair, cs_int((int64_t)i)) != 0 || cs_list_push(pair, l->items[i]) != 0) {
            cs_value_release(pair);
            cs_value_release(outer);
            cs_error(vm, "out of memory");
            return 1;
        }
        if (cs_list_push(outer, pair) != 0) {
            cs_value_release(pair);
            cs_value_release(outer);
            cs_error(vm, "out of memory");
            return 1;
        }
        cs_value_release(pair);
    }
    *out = outer;
    return 0;
}

static int nf_zip(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_LIST || argv[1].type != CS_T_LIST) { *out = cs_list(vm); return 0; }
    cs_list_obj* a = (cs_list_obj*)argv[0].as.p;
    cs_list_obj* b = (cs_list_obj*)argv[1].as.p;
    size_t n = (a && b) ? (a->len < b->len ? a->len : b->len) : 0;
    cs_value outer = cs_list(vm);
    if (!outer.as.p) { cs_error(vm, "out of memory"); return 1; }
    for (size_t i = 0; i < n; i++) {
        cs_value pair = cs_list(vm);
        if (!pair.as.p) { cs_value_release(outer); cs_error(vm, "out of memory"); return 1; }
        if (cs_list_push(pair, a->items[i]) != 0 || cs_list_push(pair, b->items[i]) != 0) {
            cs_value_release(pair);
            cs_value_release(outer);
            cs_error(vm, "out of memory");
            return 1;
        }
        if (cs_list_push(outer, pair) != 0) {
            cs_value_release(pair);
            cs_value_release(outer);
            cs_error(vm, "out of memory");
            return 1;
        }
        cs_value_release(pair);
    }
    *out = outer;
    return 0;
}

static int nf_any(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_LIST) { *out = cs_bool(0); return 0; }
    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    cs_value pred = (argc >= 2) ? argv[1] : cs_nil();
    if (argc >= 2 && pred.type != CS_T_FUNC && pred.type != CS_T_NATIVE) {
        cs_error(vm, "any(): predicate must be a function");
        return 1;
    }
    for (size_t i = 0; l && i < l->len; i++) {
        int truth = 0;
        if (pred.type == CS_T_NIL) {
            truth = truthy_local(l->items[i]);
        } else {
            cs_value args[1] = { l->items[i] };
            cs_value ret = cs_nil();
            if (cs_call_value(vm, pred, 1, args, &ret) != 0) { return 1; }
            truth = truthy_local(ret);
            cs_value_release(ret);
        }
        if (truth) { *out = cs_bool(1); return 0; }
    }
    *out = cs_bool(0);
    return 0;
}

static int nf_all(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_LIST) { *out = cs_bool(0); return 0; }
    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    cs_value pred = (argc >= 2) ? argv[1] : cs_nil();
    if (argc >= 2 && pred.type != CS_T_FUNC && pred.type != CS_T_NATIVE) {
        cs_error(vm, "all(): predicate must be a function");
        return 1;
    }
    for (size_t i = 0; l && i < l->len; i++) {
        int truth = 0;
        if (pred.type == CS_T_NIL) {
            truth = truthy_local(l->items[i]);
        } else {
            cs_value args[1] = { l->items[i] };
            cs_value ret = cs_nil();
            if (cs_call_value(vm, pred, 1, args, &ret) != 0) { return 1; }
            truth = truthy_local(ret);
            cs_value_release(ret);
        }
        if (!truth) { *out = cs_bool(0); return 0; }
    }
    *out = cs_bool(1);
    return 0;
}

static int nf_filter(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_LIST) { *out = cs_list(vm); return 0; }
    if (argv[1].type != CS_T_FUNC && argv[1].type != CS_T_NATIVE) { cs_error(vm, "filter(): predicate must be a function"); return 1; }
    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    cs_value pred = argv[1];
    cs_value out_list = cs_list(vm);
    if (!out_list.as.p) { cs_error(vm, "out of memory"); return 1; }
    for (size_t i = 0; l && i < l->len; i++) {
        cs_value args[1] = { l->items[i] };
        cs_value ret = cs_nil();
        if (cs_call_value(vm, pred, 1, args, &ret) != 0) { cs_value_release(out_list); return 1; }
        int truth = truthy_local(ret);
        cs_value_release(ret);
        if (truth) {
            if (cs_list_push(out_list, l->items[i]) != 0) { cs_value_release(out_list); cs_error(vm, "out of memory"); return 1; }
        }
    }
    *out = out_list;
    return 0;
}

static int nf_reduce(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 2 || argv[0].type != CS_T_LIST) { *out = cs_nil(); return 0; }
    if (argv[1].type != CS_T_FUNC && argv[1].type != CS_T_NATIVE) { cs_error(vm, "reduce(): reducer must be a function"); return 1; }
    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    cs_value reducer = argv[1];
    size_t idx = 0;
    cs_value acc = cs_nil();
    if (argc >= 3) {
        acc = cs_value_copy(argv[2]);
    } else {
        if (!l || l->len == 0) { *out = cs_nil(); return 0; }
        acc = cs_value_copy(l->items[0]);
        idx = 1;
    }
    for (size_t i = idx; l && i < l->len; i++) {
        cs_value args[2] = { acc, l->items[i] };
        cs_value ret = cs_nil();
        if (cs_call_value(vm, reducer, 2, args, &ret) != 0) { cs_value_release(acc); return 1; }
        cs_value_release(acc);
        acc = ret;
    }
    *out = acc;
    return 0;
}

static int nf_insert(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (out) *out = cs_nil();
    if (argc != 3 || argv[0].type != CS_T_LIST || argv[1].type != CS_T_INT) return 0;
    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    int64_t idx = argv[1].as.i;
    if (idx < 0) idx = 0;
    if ((size_t)idx > l->len) idx = (int64_t)l->len;
    if (!list_ensure(l, l->len + 1)) { cs_error(vm, "out of memory"); return 1; }
    for (size_t i = l->len; i > (size_t)idx; i--) l->items[i] = l->items[i - 1];
    l->items[(size_t)idx] = cs_value_copy(argv[2]);
    l->len++;
    return 0;
}

static int nf_remove(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_LIST || argv[1].type != CS_T_INT) { *out = cs_nil(); return 0; }
    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    int64_t idx = argv[1].as.i;
    if (idx < 0 || (size_t)idx >= l->len) { *out = cs_nil(); return 0; }
    size_t u = (size_t)idx;
    cs_value removed = l->items[u];
    for (size_t i = u + 1; i < l->len; i++) l->items[i - 1] = l->items[i];
    l->len--;
    l->items[l->len] = cs_nil();
    *out = removed;
    return 0;
}

static int nf_slice(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 3 || argv[0].type != CS_T_LIST || argv[1].type != CS_T_INT || argv[2].type != CS_T_INT) { *out = cs_list(vm); return 0; }
    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    int64_t start = argv[1].as.i;
    int64_t end = argv[2].as.i;
    if (start < 0) start = 0;
    if (end < start) end = start;
    if ((size_t)end > l->len) end = (int64_t)l->len;
    cs_value outl = cs_list(vm);
    if (!outl.as.p) { cs_error(vm, "out of memory"); return 1; }
    cs_list_obj* ol = (cs_list_obj*)outl.as.p;
    if (!list_ensure(ol, (size_t)(end - start))) { cs_value_release(outl); cs_error(vm, "out of memory"); return 1; }
    for (int64_t i = start; i < end; i++) ol->items[ol->len++] = cs_value_copy(l->items[(size_t)i]);
    *out = outl;
    return 0;
}

static int nf_substr(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 3 || argv[0].type != CS_T_STR || argv[1].type != CS_T_INT || argv[2].type != CS_T_INT) { *out = cs_nil(); return 0; }
    cs_string* s = (cs_string*)argv[0].as.p;
    int64_t start = argv[1].as.i;
    int64_t n = argv[2].as.i;
    if (start < 0) start = 0;
    if (n < 0) n = 0;
    if ((size_t)start > s->len) start = (int64_t)s->len;
    size_t maxn = s->len - (size_t)start;
    size_t take = (size_t)n;
    if (take > maxn) take = maxn;
    char* buf = (char*)malloc(take + 1);
    if (!buf) { cs_error(vm, "out of memory"); return 1; }
    memcpy(buf, s->data + start, take);
    buf[take] = 0;
    *out = cs_str_take(vm, buf, (uint64_t)take);
    return 0;
}

static int nf_to_int(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_nil(); return 0; }
    if (argv[0].type == CS_T_INT) { *out = cs_int(argv[0].as.i); return 0; }
    if (argv[0].type == CS_T_BOOL) { *out = cs_int(argv[0].as.b ? 1 : 0); return 0; }
    if (argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }
    const char* p = ((cs_string*)argv[0].as.p)->data;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') { p++; }
    int64_t v = 0;
    int any = 0;
    while (*p >= '0' && *p <= '9') { any = 1; v = v * 10 + (*p - '0'); p++; }
    if (!any) { *out = cs_nil(); return 0; }
    *out = cs_int(v * sign);
    return 0;
}

static int nf_to_str(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_str(vm, ""); return 0; }
    char tmp[128];
    const char* s = value_repr(argv[0], tmp, sizeof(tmp));
    *out = cs_str(vm, s);
    return 0;
}

static int nf_join(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_LIST || argv[1].type != CS_T_STR) { *out = cs_nil(); return 0; }
    cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
    const char* sep = ((cs_string*)argv[1].as.p)->data;
    size_t sepn = strlen(sep);

    char* buf = NULL;
    size_t len = 0, cap = 0;
    for (size_t i = 0; i < l->len; i++) {
        if (i > 0) {
            if (!sb_append(&buf, &len, &cap, sep, sepn)) { free(buf); cs_error(vm, "out of memory"); return 1; }
        }
        char tmp[128];
        const char* part = value_repr(l->items[i], tmp, sizeof(tmp));
        size_t pn = strlen(part);
        if (!sb_append(&buf, &len, &cap, part, pn)) { free(buf); cs_error(vm, "out of memory"); return 1; }
    }
    if (!buf) buf = cs_strdup2_local("");
    if (!buf) { cs_error(vm, "out of memory"); return 1; }
    *out = cs_str_take(vm, buf, (uint64_t)len);
    return 0;
}

static int nf_gc(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    *out = cs_int((int64_t)cs_vm_collect_cycles(vm));
    return 0;
}

static int nf_gc_stats(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    
    cs_value stats_map = cs_map(vm);
    if (stats_map.type != CS_T_MAP) {
        *out = cs_nil();
        return 0;
    }
    
    // Get internal VM GC stats
    extern size_t vm_get_tracked_count(cs_vm* vm);
    extern size_t vm_get_gc_collections(cs_vm* vm);
    extern size_t vm_get_gc_objects_collected(cs_vm* vm);
    extern size_t vm_get_gc_allocations(cs_vm* vm);
    
    cs_map_set(stats_map, "tracked", cs_int((int64_t)vm_get_tracked_count(vm)));
    cs_map_set(stats_map, "collections", cs_int((int64_t)vm_get_gc_collections(vm)));
    cs_map_set(stats_map, "collected", cs_int((int64_t)vm_get_gc_objects_collected(vm)));
    cs_map_set(stats_map, "allocations", cs_int((int64_t)vm_get_gc_allocations(vm)));
    
    *out = stats_map;
    return 0;
}

static int nf_gc_config(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    
    // gc_config() - get current config
    if (argc == 0) {
        cs_value config_map = cs_map(vm);
        if (config_map.type != CS_T_MAP) {
            *out = cs_nil();
            return 0;
        }
        
        extern size_t vm_get_gc_threshold(cs_vm* vm);
        extern size_t vm_get_gc_alloc_trigger(cs_vm* vm);
        
        cs_map_set(config_map, "threshold", cs_int((int64_t)vm_get_gc_threshold(vm)));
        cs_map_set(config_map, "alloc_trigger", cs_int((int64_t)vm_get_gc_alloc_trigger(vm)));
        
        *out = config_map;
        return 0;
    }
    
    // gc_config(map) - set config from map
    if (argc == 1 && argv[0].type == CS_T_MAP) {
        cs_value threshold_val = cs_map_get(argv[0], "threshold");
        if (threshold_val.type == CS_T_INT) {
            cs_vm_set_gc_threshold(vm, (size_t)threshold_val.as.i);
        }
        cs_value_release(threshold_val);
        
        cs_value trigger_val = cs_map_get(argv[0], "alloc_trigger");
        if (trigger_val.type == CS_T_INT) {
            cs_vm_set_gc_alloc_trigger(vm, (size_t)trigger_val.as.i);
        }
        cs_value_release(trigger_val);
        
        *out = cs_bool(1);
        return 0;
    }
    
    // gc_config(threshold, alloc_trigger) - set both
    if (argc == 2) {
        if (argv[0].type == CS_T_INT) {
            cs_vm_set_gc_threshold(vm, (size_t)argv[0].as.i);
        }
        if (argv[1].type == CS_T_INT) {
            cs_vm_set_gc_alloc_trigger(vm, (size_t)argv[1].as.i);
        }
        *out = cs_bool(1);
        return 0;
    }
    
    *out = cs_nil();
    return 0;
}

static int nf_set_timeout(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    
    if (argc < 1 || argv[0].type != CS_T_INT) {
        cs_error(vm, "set_timeout() requires an integer (milliseconds)");
        return 1;
    }
    
    cs_vm_set_timeout(vm, (uint64_t)argv[0].as.i);
    *out = cs_bool(1);
    return 0;
}

static int nf_set_instruction_limit(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    
    if (argc < 1 || argv[0].type != CS_T_INT) {
        cs_error(vm, "set_instruction_limit() requires an integer");
        return 1;
    }
    
    cs_vm_set_instruction_limit(vm, (uint64_t)argv[0].as.i);
    *out = cs_bool(1);
    return 0;
}

static int nf_get_timeout(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    
    extern uint64_t vm_get_timeout(cs_vm* vm);
    *out = cs_int((int64_t)vm_get_timeout(vm));
    return 0;
}

static int nf_get_instruction_limit(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    
    extern uint64_t vm_get_instruction_limit(cs_vm* vm);
    *out = cs_int((int64_t)vm_get_instruction_limit(vm));
    return 0;
}

static int nf_get_instruction_count(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    
    *out = cs_int((int64_t)cs_vm_get_instruction_count(vm));
    return 0;
}

static int nf_range(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    
    int64_t start = 0, end = 0, step = 1;
    
    // range(end) or range(start, end) or range(start, end, step)
    if (argc == 1) {
        if (argv[0].type != CS_T_INT) { *out = cs_nil(); return 0; }
        end = argv[0].as.i;
    } else if (argc == 2) {
        if (argv[0].type != CS_T_INT || argv[1].type != CS_T_INT) { *out = cs_nil(); return 0; }
        start = argv[0].as.i;
        end = argv[1].as.i;
    } else if (argc >= 3) {
        if (argv[0].type != CS_T_INT || argv[1].type != CS_T_INT || argv[2].type != CS_T_INT) { *out = cs_nil(); return 0; }
        start = argv[0].as.i;
        end = argv[1].as.i;
        step = argv[2].as.i;
    } else {
        *out = cs_nil();
        return 0;
    }
    
    if (step == 0) {
        cs_error(vm, "range() step cannot be zero");
        return 1;
    }
    
    cs_range_obj* r = (cs_range_obj*)calloc(1, sizeof(cs_range_obj));
    if (!r) { cs_error(vm, "out of memory"); return 1; }
    r->ref = 1;
    r->start = start;
    r->end = end;
    r->step = step;
    r->inclusive = 0;
    out->type = CS_T_RANGE;
    out->as.p = r;
    return 0;
}

// Type predicates
static int nf_is_nil(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    *out = cs_bool(argc > 0 && argv[0].type == CS_T_NIL);
    return 0;
}

static int nf_is_bool(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    *out = cs_bool(argc > 0 && argv[0].type == CS_T_BOOL);
    return 0;
}

static int nf_is_int(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    *out = cs_bool(argc > 0 && argv[0].type == CS_T_INT);
    return 0;
}

static int nf_is_float(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    *out = cs_bool(argc > 0 && argv[0].type == CS_T_FLOAT);
    return 0;
}

static int nf_is_string(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    *out = cs_bool(argc > 0 && argv[0].type == CS_T_STR);
    return 0;
}

static int nf_is_list(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    *out = cs_bool(argc > 0 && argv[0].type == CS_T_LIST);
    return 0;
}

static int nf_is_map(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    *out = cs_bool(argc > 0 && argv[0].type == CS_T_MAP);
    return 0;
}

static int nf_is_function(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    *out = cs_bool(argc > 0 && (argv[0].type == CS_T_FUNC || argv[0].type == CS_T_NATIVE));
    return 0;
}

// Math functions
static double to_number(cs_value v) {
    if (v.type == CS_T_INT) return (double)v.as.i;
    if (v.type == CS_T_FLOAT) return v.as.f;
    return 0.0;
}

static int nf_abs(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_nil(); return 0; }
    
    if (argv[0].type == CS_T_INT) {
        int64_t v = argv[0].as.i;
        *out = cs_int(v < 0 ? -v : v);
    } else if (argv[0].type == CS_T_FLOAT) {
        *out = cs_float(fabs(argv[0].as.f));
    } else {
        *out = cs_nil();
    }
    return 0;
}

static int nf_min(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc < 2) { *out = argc == 1 ? argv[0] : cs_nil(); return 0; }
    
    cs_value min_val = argv[0];
    double min_num = to_number(argv[0]);
    for (int i = 1; i < argc; i++) {
        double v = to_number(argv[i]);
        if (v < min_num) {
            min_num = v;
            min_val = argv[i];
        }
    }
    *out = cs_value_copy(min_val);
    return 0;
}

static int nf_max(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc < 2) { *out = argc == 1 ? argv[0] : cs_nil(); return 0; }
    
    cs_value max_val = argv[0];
    double max_num = to_number(argv[0]);
    for (int i = 1; i < argc; i++) {
        double v = to_number(argv[i]);
        if (v > max_num) {
            max_num = v;
            max_val = argv[i];
        }
    }
    *out = cs_value_copy(max_val);
    return 0;
}

static int nf_floor(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_nil(); return 0; }
    
    double v = to_number(argv[0]);
    *out = cs_int((int64_t)floor(v));
    return 0;
}

static int nf_ceil(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_nil(); return 0; }
    
    double v = to_number(argv[0]);
    *out = cs_int((int64_t)ceil(v));
    return 0;
}

static int nf_round(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_nil(); return 0; }
    
    double v = to_number(argv[0]);
    *out = cs_int((int64_t)round(v));
    return 0;
}

static int nf_sqrt(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_nil(); return 0; }
    
    double v = to_number(argv[0]);
    *out = cs_float(sqrt(v));
    return 0;
}

static int nf_pow(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2) { *out = cs_nil(); return 0; }
    
    double base = to_number(argv[0]);
    double exp = to_number(argv[1]);
    *out = cs_float(pow(base, exp));
    return 0;
}

// String ergonomics functions
static int cs_value_equals(const cs_value* a, const cs_value* b);  // forward decl

static int cs_value_equals(const cs_value* a, const cs_value* b) {
    if (a->type != b->type) return 0;
    
    switch (a->type) {
        case CS_T_NIL:
            return 1;
        case CS_T_BOOL:
            return a->as.b == b->as.b;
        case CS_T_INT:
            return a->as.i == b->as.i;
        case CS_T_FLOAT:
            return a->as.f == b->as.f;
        case CS_T_STR:
            return strcmp(cs_to_cstr(*a), cs_to_cstr(*b)) == 0;
        case CS_T_LIST: {
            cs_list_obj* la = (cs_list_obj*)a->as.p;
            cs_list_obj* lb = (cs_list_obj*)b->as.p;
            if (la->len != lb->len) return 0;
            for (size_t i = 0; i < la->len; i++) {
                if (!cs_value_equals(&la->items[i], &lb->items[i])) {
                    return 0;
                }
            }
            return 1;
        }
        case CS_T_MAP: {
            cs_map_obj* ma = (cs_map_obj*)a->as.p;
            cs_map_obj* mb = (cs_map_obj*)b->as.p;
            if (ma->len != mb->len) return 0;
            cs_value mbv = *b;
            for (size_t i = 0; i < ma->cap; i++) {
                if (!ma->entries[i].in_use) continue;
                cs_value k = ma->entries[i].key;
                if (cs_map_has_value(mbv, k) == 0) return 0;
                cs_value bv = cs_map_get_value(mbv, k);
                int ok = cs_value_equals(&ma->entries[i].val, &bv);
                cs_value_release(bv);
                if (!ok) return 0;
            }
            return 1;
        }
        default:
            return a->as.p == b->as.p;
    }
}

static int nf_str_trim(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_STR) { cs_error(vm, "str_trim() requires a string argument"); return 1; }
    
    const char* s = cs_to_cstr(argv[0]);
    while (*s && isspace((unsigned char)*s)) s++;
    
    const char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    
    size_t len = (size_t)(end - s);
    char* trimmed = (char*)malloc(len + 1);
    if (!trimmed) { cs_error(vm, "out of memory"); return 1; }
    memcpy(trimmed, s, len);
    trimmed[len] = '\0';
    
    *out = cs_str(vm, trimmed);
    free(trimmed);
    return 0;
}

static int nf_str_ltrim(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_STR) { cs_error(vm, "str_ltrim() requires a string argument"); return 1; }
    
    const char* s = cs_to_cstr(argv[0]);
    while (*s && isspace((unsigned char)*s)) s++;
    
    *out = cs_str(vm, s);
    return 0;
}

static int nf_str_rtrim(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_STR) { cs_error(vm, "str_rtrim() requires a string argument"); return 1; }
    
    const char* s = cs_to_cstr(argv[0]);
    const char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    
    size_t len = (size_t)(end - s);
    char* trimmed = (char*)malloc(len + 1);
    if (!trimmed) { cs_error(vm, "out of memory"); return 1; }
    memcpy(trimmed, s, len);
    trimmed[len] = '\0';
    
    *out = cs_str(vm, trimmed);
    free(trimmed);
    return 0;
}

static int nf_str_lower(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_STR) { cs_error(vm, "str_lower() requires a string argument"); return 1; }
    
    const char* s = cs_to_cstr(argv[0]);
    size_t len = strlen(s);
    char* lower = (char*)malloc(len + 1);
    if (!lower) { cs_error(vm, "out of memory"); return 1; }
    
    for (size_t i = 0; i < len; i++) {
        lower[i] = (char)tolower((unsigned char)s[i]);
    }
    lower[len] = '\0';
    
    *out = cs_str(vm, lower);
    free(lower);
    return 0;
}

static int nf_str_upper(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_STR) { cs_error(vm, "str_upper() requires a string argument"); return 1; }
    
    const char* s = cs_to_cstr(argv[0]);
    size_t len = strlen(s);
    char* upper = (char*)malloc(len + 1);
    if (!upper) { cs_error(vm, "out of memory"); return 1; }
    
    for (size_t i = 0; i < len; i++) {
        upper[i] = (char)toupper((unsigned char)s[i]);
    }
    upper[len] = '\0';
    
    *out = cs_str(vm, upper);
    free(upper);
    return 0;
}

static int nf_str_startswith(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { 
        cs_error(vm, "str_startswith() requires 2 string arguments"); 
        return 1; 
    }
    
    const char* s = cs_to_cstr(argv[0]);
    const char* prefix = cs_to_cstr(argv[1]);
    size_t prefix_len = strlen(prefix);
    
    *out = cs_bool(strncmp(s, prefix, prefix_len) == 0);
    return 0;
}

static int nf_str_endswith(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) { 
        cs_error(vm, "str_endswith() requires 2 string arguments"); 
        return 1; 
    }
    
    const char* s = cs_to_cstr(argv[0]);
    const char* suffix = cs_to_cstr(argv[1]);
    
    size_t s_len = strlen(s);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > s_len) {
        *out = cs_bool(0);
        return 0;
    }
    
    *out = cs_bool(strcmp(s + s_len - suffix_len, suffix) == 0);
    return 0;
}

static int nf_str_repeat(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_INT) { 
        cs_error(vm, "str_repeat() requires a string and an integer"); 
        return 1; 
    }
    
    const char* s = cs_to_cstr(argv[0]);
    int64_t count = argv[1].as.i;
    
    if (count <= 0) {
        *out = cs_str(vm, "");
        return 0;
    }
    
    size_t s_len = strlen(s);
    size_t total_len = s_len * (size_t)count;
    char* result = (char*)malloc(total_len + 1);
    if (!result) { cs_error(vm, "out of memory"); return 1; }
    
    char* p = result;
    for (int64_t i = 0; i < count; i++) {
        memcpy(p, s, s_len);
        p += s_len;
    }
    *p = '\0';
    
    *out = cs_str(vm, result);
    free(result);
    return 0;
}

static int nf_split_lines(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }

    const cs_string* s = (const cs_string*)argv[0].as.p;
    const char* data = s ? s->data : "";
    size_t n = s ? s->len : 0;

    cs_value listv = cs_list(vm);
    if (listv.type != CS_T_LIST) { *out = cs_nil(); return 0; }

    size_t start = 0;
    for (size_t i = 0; i < n; i++) {
        char c = data[i];
        if (c == '\n' || c == '\r') {
            size_t line_len = i - start;
            char* buf = (char*)malloc(line_len + 1);
            if (!buf) { cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
            if (line_len) memcpy(buf, data + start, line_len);
            buf[line_len] = 0;
            cs_value sv = cs_str_take(vm, buf, (uint64_t)line_len);
            if (cs_list_push(listv, sv) != 0) { cs_value_release(sv); cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
            cs_value_release(sv);

            if (c == '\r' && i + 1 < n && data[i + 1] == '\n') i++;
            start = i + 1;
        }
    }

    size_t tail_len = (start <= n) ? (n - start) : 0;
    char* tbuf = (char*)malloc(tail_len + 1);
    if (!tbuf) { cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
    if (tail_len) memcpy(tbuf, data + start, tail_len);
    tbuf[tail_len] = 0;
    cs_value tv = cs_str_take(vm, tbuf, (uint64_t)tail_len);
    if (cs_list_push(listv, tv) != 0) { cs_value_release(tv); cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
    cs_value_release(tv);

    *out = listv;
    return 0;
}

static int list_contains_value_local(cs_list_obj* l, cs_value v) {
    if (!l) return 0;
    for (size_t i = 0; i < l->len; i++) {
        if (cs_value_equals(&l->items[i], &v)) return 1;
    }
    return 0;
}

static int nf_list_unique(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_LIST) { *out = cs_list(vm); return 0; }
    cs_list_obj* src = (cs_list_obj*)argv[0].as.p;
    cs_value listv = cs_list(vm);
    if (!listv.as.p) { cs_error(vm, "out of memory"); return 1; }
    cs_list_obj* outl = (cs_list_obj*)listv.as.p;
    for (size_t i = 0; i < src->len; i++) {
        cs_value item = src->items[i];
        if (!list_contains_value_local(outl, item)) {
            if (!list_ensure(outl, outl->len + 1)) { cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
            outl->items[outl->len++] = cs_value_copy(item);
        }
    }
    *out = listv;
    return 0;
}

static int nf_list_flatten(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_LIST) { *out = cs_list(vm); return 0; }
    cs_list_obj* src = (cs_list_obj*)argv[0].as.p;
    cs_value listv = cs_list(vm);
    if (!listv.as.p) { cs_error(vm, "out of memory"); return 1; }
    cs_list_obj* outl = (cs_list_obj*)listv.as.p;
    for (size_t i = 0; i < src->len; i++) {
        cs_value item = src->items[i];
        if (item.type == CS_T_LIST) {
            cs_list_obj* sub = (cs_list_obj*)item.as.p;
            for (size_t j = 0; j < sub->len; j++) {
                if (!list_ensure(outl, outl->len + 1)) { cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
                outl->items[outl->len++] = cs_value_copy(sub->items[j]);
            }
        } else {
            if (!list_ensure(outl, outl->len + 1)) { cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
            outl->items[outl->len++] = cs_value_copy(item);
        }
    }
    *out = listv;
    return 0;
}

static int nf_list_chunk(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_LIST || argv[1].type != CS_T_INT) { *out = cs_list(vm); return 0; }
    int64_t size = argv[1].as.i;
    cs_value listv = cs_list(vm);
    if (!listv.as.p) { cs_error(vm, "out of memory"); return 1; }
    if (size <= 0) { *out = listv; return 0; }
    cs_list_obj* src = (cs_list_obj*)argv[0].as.p;
    cs_list_obj* outl = (cs_list_obj*)listv.as.p;
    for (size_t i = 0; i < src->len; i += (size_t)size) {
        cs_value chunkv = cs_list(vm);
        if (!chunkv.as.p) { cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
        cs_list_obj* chunk = (cs_list_obj*)chunkv.as.p;
        size_t end = i + (size_t)size;
        if (end > src->len) end = src->len;
        for (size_t j = i; j < end; j++) {
            if (!list_ensure(chunk, chunk->len + 1)) { cs_value_release(chunkv); cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
            chunk->items[chunk->len++] = cs_value_copy(src->items[j]);
        }
        if (!list_ensure(outl, outl->len + 1)) { cs_value_release(chunkv); cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
        outl->items[outl->len++] = chunkv;
    }
    *out = listv;
    return 0;
}

static int nf_list_compact(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_LIST) { *out = cs_list(vm); return 0; }
    cs_list_obj* src = (cs_list_obj*)argv[0].as.p;
    cs_value listv = cs_list(vm);
    if (!listv.as.p) { cs_error(vm, "out of memory"); return 1; }
    cs_list_obj* outl = (cs_list_obj*)listv.as.p;
    for (size_t i = 0; i < src->len; i++) {
        if (src->items[i].type == CS_T_NIL) continue;
        if (!list_ensure(outl, outl->len + 1)) { cs_value_release(listv); cs_error(vm, "out of memory"); return 1; }
        outl->items[outl->len++] = cs_value_copy(src->items[i]);
    }
    *out = listv;
    return 0;
}

static int nf_list_sum(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_LIST) { *out = cs_nil(); return 0; }
    cs_list_obj* src = (cs_list_obj*)argv[0].as.p;
    double sum = 0.0;
    int has_float = 0;
    for (size_t i = 0; i < src->len; i++) {
        cs_value v = src->items[i];
        if (v.type == CS_T_NIL) continue;
        if (v.type == CS_T_INT) sum += (double)v.as.i;
        else if (v.type == CS_T_FLOAT) { sum += v.as.f; has_float = 1; }
        else { *out = cs_nil(); return 0; }
    }
    if (has_float) *out = cs_float(sum);
    else *out = cs_int((int64_t)sum);
    return 0;
}

// Data quality-of-life functions
static int nf_map_values(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_MAP) { cs_error(vm, "map_values() requires a map argument"); return 1; }
    
    cs_map_obj* m = (cs_map_obj*)argv[0].as.p;
    cs_value values_list = cs_list(vm);
    if (!values_list.as.p) { cs_error(vm, "out of memory"); return 1; }
    cs_list_obj* l = (cs_list_obj*)values_list.as.p;
    
    if (!list_ensure(l, m->len)) { cs_value_release(values_list); cs_error(vm, "out of memory"); return 1; }
    for (size_t i = 0; i < m->cap; i++) {
        if (!m->entries[i].in_use) continue;
        l->items[l->len++] = cs_value_copy(m->entries[i].val);
    }
    
    *out = values_list;
    return 0;
}

static int nf_copy(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1) { cs_error(vm, "copy() requires 1 argument"); return 1; }
    
    cs_value src = argv[0];
    
    switch (src.type) {
        case CS_T_LIST: {
            cs_value new_list = cs_list(vm);
            if (!new_list.as.p) { cs_error(vm, "out of memory"); return 1; }
            cs_list_obj* src_list = (cs_list_obj*)src.as.p;
            cs_list_obj* dst_list = (cs_list_obj*)new_list.as.p;
            
            if (!list_ensure(dst_list, src_list->len)) {
                cs_value_release(new_list);
                cs_error(vm, "out of memory");
                return 1;
            }
            
            for (size_t i = 0; i < src_list->len; i++) {
                dst_list->items[dst_list->len++] = cs_value_copy(src_list->items[i]);
            }
            
            *out = new_list;
            return 0;
        }
        
        case CS_T_MAP: {
            cs_value new_map = cs_map(vm);
            if (!new_map.as.p) { cs_error(vm, "out of memory"); return 1; }
            cs_map_obj* src_map = (cs_map_obj*)src.as.p;
            for (size_t i = 0; i < src_map->cap; i++) {
                if (!src_map->entries[i].in_use) continue;
                if (cs_map_set_value(new_map, src_map->entries[i].key, src_map->entries[i].val) != 0) {
                    cs_value_release(new_map);
                    cs_error(vm, "out of memory");
                    return 1;
                }
            }
            
            *out = new_map;
            return 0;
        }
        
        default:
            *out = cs_value_copy(src);
            return 0;
    }
}

static cs_value deepcopy_impl(cs_vm* vm, cs_value src, cs_value visited_map);

static cs_value deepcopy_impl(cs_vm* vm, cs_value src, cs_value visited_map) {
    // Check for cycles
    if (src.type == CS_T_LIST || src.type == CS_T_MAP) {
        char ptr_str[32];
        snprintf(ptr_str, sizeof(ptr_str), "%p", src.as.p);

        cs_value hit = cs_map_get(visited_map, ptr_str);
        if (hit.type != CS_T_NIL) return hit;
        cs_value_release(hit);
    }
    
    switch (src.type) {
	        case CS_T_LIST: {
	            cs_value new_list = cs_list(vm);
	            cs_list_obj* src_list = (cs_list_obj*)src.as.p;
	            cs_list_obj* dst_list = (cs_list_obj*)new_list.as.p;
            
            // Register in visited map
            char ptr_str[32];
            snprintf(ptr_str, sizeof(ptr_str), "%p", src.as.p);
            if (cs_map_set(visited_map, ptr_str, new_list) != 0) { cs_value_release(new_list); return cs_nil(); }
            
	            if (!list_ensure(dst_list, src_list->len)) { cs_value_release(new_list); return cs_nil(); }
	            for (size_t i = 0; i < src_list->len; i++) {
	                cs_value item = deepcopy_impl(vm, src_list->items[i], visited_map);
	                if (item.type == CS_T_NIL && src_list->items[i].type != CS_T_NIL) { cs_value_release(new_list); return cs_nil(); }
	                dst_list->items[dst_list->len++] = item;
	            }
	            
	            return new_list;
	        }
        
        case CS_T_MAP: {
            cs_value new_map = cs_map(vm);
            cs_map_obj* src_map = (cs_map_obj*)src.as.p;
            
            // Register in visited map
            char ptr_str[32];
            snprintf(ptr_str, sizeof(ptr_str), "%p", src.as.p);
            if (cs_map_set(visited_map, ptr_str, new_map) != 0) { cs_value_release(new_map); return cs_nil(); }
            
            for (size_t i = 0; i < src_map->cap; i++) {
                if (!src_map->entries[i].in_use) continue;
                cs_value val = deepcopy_impl(vm, src_map->entries[i].val, visited_map);
                if (val.type == CS_T_NIL && src_map->entries[i].val.type != CS_T_NIL) { cs_value_release(new_map); return cs_nil(); }
                if (cs_map_set_value(new_map, src_map->entries[i].key, val) != 0) { cs_value_release(val); cs_value_release(new_map); return cs_nil(); }
                cs_value_release(val);
            }
            
            return new_map;
        }
        
        default:
            return cs_value_copy(src);
    }
}

static int nf_deepcopy(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1) { cs_error(vm, "deepcopy() requires 1 argument"); return 1; }
    
    cs_value visited = cs_map(vm);
    if (!visited.as.p) { cs_error(vm, "out of memory"); return 1; }
    *out = deepcopy_impl(vm, argv[0], visited);
    cs_value_release(visited);
    
    return 0;
}

static int nf_reverse(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_LIST) { cs_error(vm, "reverse() requires a list argument"); return 1; }
    
    cs_list_obj* list = (cs_list_obj*)argv[0].as.p;
    
    for (size_t i = 0; i < list->len / 2; i++) {
        size_t j = list->len - 1 - i;
        cs_value temp = list->items[i];
        list->items[i] = list->items[j];
        list->items[j] = temp;
    }
    
    *out = cs_nil();
    return 0;
}

static int nf_reversed(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_LIST) { cs_error(vm, "reversed() requires a list argument"); return 1; }
    
    cs_list_obj* src_list = (cs_list_obj*)argv[0].as.p;
    cs_value rev_list = cs_list(vm);
    if (!rev_list.as.p) { cs_error(vm, "out of memory"); return 1; }
    cs_list_obj* dst_list = (cs_list_obj*)rev_list.as.p;
    
    if (!list_ensure(dst_list, src_list->len)) {
        cs_value_release(rev_list);
        cs_error(vm, "out of memory");
        return 1;
    }
    
    for (size_t i = src_list->len; i > 0; i--) {
        dst_list->items[dst_list->len++] = cs_value_copy(src_list->items[i - 1]);
    }
    
    *out = rev_list;
    return 0;
}

static int nf_contains(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 2) { cs_error(vm, "contains() requires 2 arguments"); return 1; }
    
    cs_value container = argv[0];
    cs_value item = argv[1];
    
    switch (container.type) {
        case CS_T_LIST: {
            cs_list_obj* list = (cs_list_obj*)container.as.p;
            for (size_t i = 0; i < list->len; i++) {
                if (cs_value_equals(&list->items[i], &item)) {
                    *out = cs_bool(1);
                    return 0;
                }
            }
            *out = cs_bool(0);
            return 0;
        }
        
        case CS_T_MAP: {
            *out = cs_bool(cs_map_has_value(container, item) != 0);
            return 0;
        }
        
        case CS_T_STR: {
            if (item.type != CS_T_STR) {
                *out = cs_bool(0);
                return 0;
            }
            const char* haystack = cs_to_cstr(container);
            const char* needle = cs_to_cstr(item);
            *out = cs_bool(strstr(haystack, needle) != NULL);
            return 0;
        }
        
        default:
            cs_error(vm, "contains() requires a list, map, or string");
            return 1;
    }
}

// Error object functions
static int nf_error(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1) { cs_error(vm, "error() requires at least 1 argument"); return 1; }
    
    cs_value err_map = cs_map(vm);
    if (!err_map.as.p) { cs_error(vm, "out of memory"); return 1; }
    
    // Set message (required)
    const char* msg = (argv[0].type == CS_T_STR) ? cs_to_cstr(argv[0]) : "Error";
    cs_value msg_val = cs_str(vm, msg);
    if (!msg_val.as.p) { cs_value_release(err_map); cs_error(vm, "out of memory"); return 1; }
    
    cs_value args[3];
    args[0] = err_map;
    args[1] = cs_str(vm, "msg");
    args[2] = msg_val;
    nf_mset(vm, NULL, 3, args, NULL);
    cs_value_release(msg_val);
    cs_value_release(args[1]);
    
    // Set code (optional, default to "ERROR")
    if (argc >= 2 && argv[1].type == CS_T_STR) {
        args[1] = cs_str(vm, "code");
        args[2] = cs_value_copy(argv[1]);
        nf_mset(vm, NULL, 3, args, NULL);
        cs_value_release(args[1]);
        cs_value_release(args[2]);
    } else {
        cs_value code_val = cs_str(vm, "ERROR");
        args[1] = cs_str(vm, "code");
        args[2] = code_val;
        nf_mset(vm, NULL, 3, args, NULL);
        cs_value_release(args[1]);
        cs_value_release(code_val);
    }
    
    // Capture stack trace
    cs_value stack = cs_capture_stack_trace(vm);
    args[1] = cs_str(vm, "stack");
    args[2] = stack;
    nf_mset(vm, NULL, 3, args, NULL);
    cs_value_release(args[1]);
    cs_value_release(stack);
    
    *out = err_map;
    return 0;
}

static int nf_is_error(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc < 1) {
        *out = cs_bool(0);
        return 0;
    }
    
    // Error objects are maps with "msg" and "code" keys
    if (argv[0].type != CS_T_MAP) {
        *out = cs_bool(0);
        return 0;
    }
    
    cs_value args[2];
    args[0] = argv[0];
    args[1] = cs_str(vm, "msg");
    cs_value has_msg;
    nf_mhas(vm, NULL, 2, args, &has_msg);
    cs_value_release(args[1]);
    
    args[1] = cs_str(vm, "code");
    cs_value has_code;
    nf_mhas(vm, NULL, 2, args, &has_code);
    cs_value_release(args[1]);
    
    int is_err = has_msg.as.b && has_code.as.b;
    *out = cs_bool(is_err);
    return 0;
}

static int nf_format_error(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_MAP) {
        *out = cs_str(vm, "Error");
        return 0;
    }
    
    // Format: "[code] message"
    cs_value msg_val = cs_map_get(argv[0], "msg");
    cs_value code_val = cs_map_get(argv[0], "code");
    
    const char* msg = (msg_val.type == CS_T_STR) ? cs_to_cstr(msg_val) : "Unknown error";
    const char* code = (code_val.type == CS_T_STR) ? cs_to_cstr(code_val) : "ERROR";
    
    char buf[512];
    snprintf(buf, sizeof(buf), "[%s] %s", code, msg);
    
    cs_value_release(msg_val);
    cs_value_release(code_val);
    
    *out = cs_str(vm, buf);
    return 0;
}

void cs_register_stdlib(cs_vm* vm) {
    cs_register_native(vm, "print",  nf_print,  NULL);
    cs_register_native(vm, "typeof", nf_typeof, NULL);
    cs_register_native(vm, "getenv", nf_getenv, NULL);
    cs_register_native(vm, "assert", nf_assert, NULL);
    cs_register_native(vm, "load",   nf_load,   NULL);
    cs_register_native(vm, "require", nf_require, NULL);
    cs_register_native(vm, "require_optional", nf_require_optional, NULL);
    cs_register_native(vm, "list",   nf_list,   NULL);
    cs_register_native(vm, "map",    nf_map,    NULL);
    cs_register_native(vm, "strbuf", nf_strbuf, NULL);
    cs_register_native(vm, "len",    nf_len,    NULL);
    cs_register_native(vm, "push",   nf_push,   NULL);
    cs_register_native(vm, "pop",    nf_pop,    NULL);
    cs_register_native(vm, "extend", nf_extend, NULL);
    cs_register_native(vm, "index_of", nf_index_of, NULL);
    cs_register_native(vm, "sort",   nf_sort,   NULL);
    cs_register_native(vm, "mget",   nf_mget,   NULL);
    cs_register_native(vm, "mset",   nf_mset,   NULL);
    cs_register_native(vm, "mhas",   nf_mhas,   NULL);
    cs_register_native(vm, "mdel",   nf_mdel,   NULL);
    cs_register_native(vm, "keys",   nf_keys,   NULL);
    cs_register_native(vm, "values", nf_values, NULL);
    cs_register_native(vm, "items",  nf_items,  NULL);
    cs_register_native(vm, "enumerate", nf_enumerate, NULL);
    cs_register_native(vm, "zip",       nf_zip,       NULL);
    cs_register_native(vm, "any",       nf_any,       NULL);
    cs_register_native(vm, "all",       nf_all,       NULL);
    cs_register_native(vm, "filter",    nf_filter,    NULL);
    cs_register_native(vm, "map",       nf_map,       NULL);
    cs_register_native(vm, "reduce",    nf_reduce,    NULL);
    cs_register_native(vm, "insert", nf_insert, NULL);
    cs_register_native(vm, "remove", nf_remove, NULL);
    cs_register_native(vm, "slice",  nf_slice,  NULL);
    cs_register_native(vm, "list_unique",  nf_list_unique,  NULL);
    cs_register_native(vm, "list_flatten", nf_list_flatten, NULL);
    cs_register_native(vm, "list_chunk",   nf_list_chunk,   NULL);
    cs_register_native(vm, "list_compact", nf_list_compact, NULL);
    cs_register_native(vm, "list_sum",     nf_list_sum,     NULL);
    cs_register_native(vm, "substr", nf_substr, NULL);
    cs_register_native(vm, "join",   nf_join,   NULL);
    cs_register_native(vm, "to_int", nf_to_int, NULL);
    cs_register_native(vm, "to_str", nf_to_str, NULL);
    cs_register_native(vm, "gc",     nf_gc,     NULL);
    cs_register_native(vm, "range",  nf_range,  NULL);
    
    // Type predicates
    cs_register_native(vm, "is_nil",      nf_is_nil,      NULL);
    cs_register_native(vm, "is_bool",     nf_is_bool,     NULL);
    cs_register_native(vm, "is_int",      nf_is_int,      NULL);
    cs_register_native(vm, "is_float",    nf_is_float,    NULL);
    cs_register_native(vm, "is_string",   nf_is_string,   NULL);
    cs_register_native(vm, "is_list",     nf_is_list,     NULL);
    cs_register_native(vm, "is_map",      nf_is_map,      NULL);
    cs_register_native(vm, "is_function", nf_is_function, NULL);
    
    // Math functions
    cs_register_native(vm, "abs",   nf_abs,   NULL);
    cs_register_native(vm, "min",   nf_min,   NULL);
    cs_register_native(vm, "max",   nf_max,   NULL);
    cs_register_native(vm, "floor", nf_floor, NULL);
    cs_register_native(vm, "ceil",  nf_ceil,  NULL);
    cs_register_native(vm, "round", nf_round, NULL);
    cs_register_native(vm, "sqrt",  nf_sqrt,  NULL);
    cs_register_native(vm, "pow",   nf_pow,   NULL);
    
    cs_register_native(vm, "str_find",    nf_str_find,    NULL);
    cs_register_native(vm, "str_replace", nf_str_replace, NULL);
    cs_register_native(vm, "str_split",   nf_str_split,   NULL);
    cs_register_native(vm, "str_contains", nf_str_contains, NULL);
    cs_register_native(vm, "str_count",    nf_str_count,    NULL);
    cs_register_native(vm, "str_pad_start", nf_str_pad_start, NULL);
    cs_register_native(vm, "str_pad_end",   nf_str_pad_end,   NULL);
    cs_register_native(vm, "str_reverse",   nf_str_reverse,   NULL);
    cs_register_native(vm, "regex_is_match", nf_regex_is_match, NULL);
    cs_register_native(vm, "regex_match",    nf_regex_match,    NULL);
    cs_register_native(vm, "regex_find",     nf_regex_find,     NULL);
    cs_register_native(vm, "regex_find_all", nf_regex_find_all, NULL);
    cs_register_native(vm, "regex_replace",  nf_regex_replace,  NULL);
    cs_register_native(vm, "path_join",   nf_path_join,   NULL);
    cs_register_native(vm, "path_dirname", nf_path_dirname, NULL);
    cs_register_native(vm, "path_basename", nf_path_basename, NULL);
    cs_register_native(vm, "path_ext",    nf_path_ext,    NULL);
    cs_register_native(vm, "json_parse",    nf_json_parse,    NULL);
    cs_register_native(vm, "json_stringify", nf_json_stringify, NULL);
    cs_register_native(vm, "read_file",  nf_read_file,  NULL);
    cs_register_native(vm, "write_file", nf_write_file, NULL);
    cs_register_native(vm, "exists",     nf_exists,     NULL);
    cs_register_native(vm, "is_dir",     nf_is_dir,     NULL);
    cs_register_native(vm, "is_file",    nf_is_file,    NULL);
    cs_register_native(vm, "list_dir",   nf_list_dir,   NULL);
    cs_register_native(vm, "mkdir",      nf_mkdir,      NULL);
    cs_register_native(vm, "rm",         nf_rm,         NULL);
    cs_register_native(vm, "rename",     nf_rename,     NULL);
    cs_register_native(vm, "cwd",        nf_cwd,        NULL);
    cs_register_native(vm, "chdir",      nf_chdir,      NULL);
    cs_register_native(vm, "fmt",         nf_fmt,         NULL);
    cs_register_native(vm, "now_ms",      nf_now_ms,      NULL);
    cs_register_native(vm, "unix_ms",     nf_unix_ms,     NULL);
    cs_register_native(vm, "unix_s",      nf_unix_s,      NULL);
    cs_register_native(vm, "datetime_now",            nf_datetime_now,            NULL);
    cs_register_native(vm, "datetime_utc",            nf_datetime_utc,            NULL);
    cs_register_native(vm, "datetime_from_unix_ms",    nf_datetime_from_unix_ms,   NULL);
    cs_register_native(vm, "datetime_from_unix_ms_utc", nf_datetime_from_unix_ms_utc, NULL);
    cs_register_native(vm, "sleep",       nf_sleep,       NULL);
    cs_register_native(vm, "delay",       nf_sleep,       NULL);
    cs_register_native(vm, "promise",     nf_promise,     NULL);
    cs_register_native(vm, "resolve",     nf_resolve,     NULL);
    cs_register_native(vm, "reject",      nf_reject,      NULL);
    cs_register_native(vm, "is_promise",  nf_is_promise,  NULL);
    cs_register_native(vm, "await_all",   nf_await_all,   NULL);
    cs_register_native(vm, "await_any",   nf_await_any,   NULL);
    cs_register_native(vm, "await_all_settled", nf_await_all_settled, NULL);
    cs_register_native(vm, "timeout",     nf_timeout,     NULL);
    
    // String ergonomics
    cs_register_native(vm, "str_trim",       nf_str_trim,       NULL);
    cs_register_native(vm, "str_ltrim",      nf_str_ltrim,      NULL);
    cs_register_native(vm, "str_rtrim",      nf_str_rtrim,      NULL);
    cs_register_native(vm, "str_lower",      nf_str_lower,      NULL);
    cs_register_native(vm, "str_upper",      nf_str_upper,      NULL);
    cs_register_native(vm, "str_startswith", nf_str_startswith, NULL);
    cs_register_native(vm, "str_endswith",   nf_str_endswith,   NULL);
    cs_register_native(vm, "str_repeat",     nf_str_repeat,     NULL);
    cs_register_native(vm, "split_lines",    nf_split_lines,    NULL);

    // String ergonomics aliases
    cs_register_native(vm, "trim",        nf_str_trim,       NULL);
    cs_register_native(vm, "ltrim",       nf_str_ltrim,      NULL);
    cs_register_native(vm, "rtrim",       nf_str_rtrim,      NULL);
    cs_register_native(vm, "lower",       nf_str_lower,      NULL);
    cs_register_native(vm, "upper",       nf_str_upper,      NULL);
    cs_register_native(vm, "starts_with", nf_str_startswith, NULL);
    cs_register_native(vm, "ends_with",   nf_str_endswith,   NULL);
    
    // Data quality-of-life
    cs_register_native(vm, "map_values", nf_map_values, NULL);
    cs_register_native(vm, "copy",       nf_copy,       NULL);
    cs_register_native(vm, "deepcopy",   nf_deepcopy,   NULL);
    cs_register_native(vm, "reverse",    nf_reverse,    NULL);
    cs_register_native(vm, "reversed",   nf_reversed,   NULL);
    cs_register_native(vm, "contains",   nf_contains,   NULL);
    
    // Error objects
    cs_register_native(vm, "error",    nf_error,    NULL);
    cs_register_native(vm, "is_error", nf_is_error, NULL);
    cs_register_native(vm, "format_error", nf_format_error, NULL);
    
    // GC functions
    cs_register_native(vm, "gc_stats",  nf_gc_stats,  NULL);
    cs_register_native(vm, "gc_config", nf_gc_config, NULL);
    
    // Safety control functions
    cs_register_native(vm, "set_timeout",            nf_set_timeout,            NULL);
    cs_register_native(vm, "set_instruction_limit",  nf_set_instruction_limit,  NULL);
    cs_register_native(vm, "get_timeout",            nf_get_timeout,            NULL);
    cs_register_native(vm, "get_instruction_limit",  nf_get_instruction_limit,  NULL);
    cs_register_native(vm, "get_instruction_count",  nf_get_instruction_count,  NULL);
    
    // Error code constants (ERR.*)
    cs_value err_map = cs_map(vm);
    if (err_map.as.p) {
        cs_map_set(err_map, "INVALID_ARG", cs_str(vm, "INVALID_ARG"));
        cs_map_set(err_map, "TYPE_ERROR", cs_str(vm, "TYPE_ERROR"));
        cs_map_set(err_map, "DIV_ZERO", cs_str(vm, "DIV_ZERO"));
        cs_map_set(err_map, "OUT_OF_BOUNDS", cs_str(vm, "OUT_OF_BOUNDS"));
        cs_map_set(err_map, "NOT_FOUND", cs_str(vm, "NOT_FOUND"));
        cs_map_set(err_map, "ASSERTION", cs_str(vm, "ASSERTION"));
        cs_map_set(err_map, "GENERIC", cs_str(vm, "ERROR"));

        // Network error codes
        cs_map_set(err_map, "NET_RESOLVE", cs_str(vm, "NET_RESOLVE"));
        cs_map_set(err_map, "NET_CONNECT", cs_str(vm, "NET_CONNECT"));
        cs_map_set(err_map, "NET_TIMEOUT", cs_str(vm, "NET_TIMEOUT"));
        cs_map_set(err_map, "NET_CLOSED", cs_str(vm, "NET_CLOSED"));
        cs_map_set(err_map, "NET_SEND", cs_str(vm, "NET_SEND"));
        cs_map_set(err_map, "NET_RECV", cs_str(vm, "NET_RECV"));
        cs_map_set(err_map, "HTTP_PARSE", cs_str(vm, "HTTP_PARSE"));
        cs_map_set(err_map, "HTTP_REDIRECT", cs_str(vm, "HTTP_REDIRECT"));
        cs_map_set(err_map, "HTTP_NO_TLS", cs_str(vm, "HTTP_NO_TLS"));
        cs_map_set(err_map, "TLS_INIT", cs_str(vm, "TLS_INIT"));
        cs_map_set(err_map, "TLS_HANDSHAKE", cs_str(vm, "TLS_HANDSHAKE"));
        cs_map_set(err_map, "TLS_CERT", cs_str(vm, "TLS_CERT"));
        cs_map_set(err_map, "TLS_READ", cs_str(vm, "TLS_READ"));
        cs_map_set(err_map, "TLS_WRITE", cs_str(vm, "TLS_WRITE"));
        
        // Register as global ERR constant
        cs_register_global(vm, "ERR", err_map);
        cs_value_release(err_map);
    }

    cs_register_net_stdlib(vm);
    cs_register_tls_stdlib(vm);
    cs_register_http_stdlib(vm);
}
