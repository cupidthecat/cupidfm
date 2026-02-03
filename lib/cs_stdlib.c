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
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/inotify.h>
#include <poll.h>
#include <zlib.h>
#endif

#if defined(_WIN32)
#define CS_POPEN  _popen
#define CS_PCLOSE _pclose
#else
#define CS_POPEN  popen
#define CS_PCLOSE pclose
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
    (void)vm; (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    *out = cs_int((int64_t)wall_clock_ms());
    return 0;
}

static int nf_unix_s(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud; (void)argc; (void)argv;
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
        case CS_T_SET: {
            cs_map_obj* m = (cs_map_obj*)v.as.p;
            snprintf(buf, buf_sz, "<set len=%lld>", (long long)(m ? m->len : 0));
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
        case CS_T_TUPLE: {
            cs_tuple_obj* t = (cs_tuple_obj*)v.as.p;
            snprintf(buf, buf_sz, "<tuple len=%lld>", (long long)(t ? t->len : 0));
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

static int nf_set(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;

    cs_value s = cs_set(vm);
    if (!s.as.p) { cs_error(vm, "out of memory"); return 1; }

    if (argc == 0) { *out = s; return 0; }
    if (argc != 1) { cs_value_release(s); *out = cs_nil(); return 0; }

    // ...existing code...

    if (argv[0].type == CS_T_LIST) {
        cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
        for (size_t i = 0; l && i < l->len; i++) {
            if (cs_map_set_value(s, l->items[i], cs_bool(1)) != 0) {
                cs_value_release(s);
                cs_error(vm, "out of memory");
                return 1;
            }
        }
        *out = s;
        return 0;
    }

    if (argv[0].type == CS_T_MAP || argv[0].type == CS_T_SET) {
        cs_map_obj* m = (cs_map_obj*)argv[0].as.p;
        for (size_t i = 0; m && i < m->cap; i++) {
            if (!m->entries[i].in_use) continue;
            if (cs_map_set_value(s, m->entries[i].key, cs_bool(1)) != 0) {
                cs_value_release(s);
                cs_error(vm, "out of memory");
                return 1;
            }
        }
        *out = s;
        return 0;
    }

    cs_value_release(s);
    *out = cs_nil();
    return 0;
}

static int nf_strbuf(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    *out = cs_strbuf(vm);
    if (!out->as.p) { cs_error(vm, "out of memory"); return 1; }
    return 0;
}

static int nf_bytes(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc == 0) {
        *out = cs_bytes(vm, NULL, 0);
        if (!out->as.p) { cs_error(vm, "out of memory"); return 1; }
        return 0;
    }
    if (argc != 1) { *out = cs_nil(); return 0; }

    if (argv[0].type == CS_T_BYTES) { *out = cs_value_copy(argv[0]); return 0; }
    if (argv[0].type == CS_T_INT) {
        if (argv[0].as.i < 0) { *out = cs_nil(); return 0; }
        *out = cs_bytes(vm, NULL, (size_t)argv[0].as.i);
        if (!out->as.p) { cs_error(vm, "out of memory"); return 1; }
        return 0;
    }
    if (argv[0].type == CS_T_STR) {
        cs_string* s = (cs_string*)argv[0].as.p;
        *out = cs_bytes(vm, (const uint8_t*)s->data, s->len);
        if (!out->as.p) { cs_error(vm, "out of memory"); return 1; }
        return 0;
    }
    if (argv[0].type == CS_T_LIST) {
        cs_list_obj* l = (cs_list_obj*)argv[0].as.p;
        size_t len = l ? l->len : 0;
        uint8_t* buf = (uint8_t*)malloc(len ? len : 1);
        if (!buf) { cs_error(vm, "out of memory"); return 1; }
        for (size_t i = 0; i < len; i++) {
            cs_value v = l->items[i];
            if (v.type != CS_T_INT || v.as.i < 0 || v.as.i > 255) {
                free(buf);
                cs_error(vm, "bytes() list must contain ints 0..255");
                return 1;
            }
            buf[i] = (uint8_t)v.as.i;
        }
        *out = cs_bytes_take(vm, buf, len);
        if (!out->as.p) { cs_error(vm, "out of memory"); return 1; }
        return 0;
    }

    *out = cs_nil();
    return 0;
}

static int nf_len(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_int(0); return 0; }
    if (argv[0].type == CS_T_STR) { *out = cs_int((int64_t)((cs_string*)argv[0].as.p)->len); return 0; }
    if (argv[0].type == CS_T_LIST) { *out = cs_int((int64_t)((cs_list_obj*)argv[0].as.p)->len); return 0; }
    if (argv[0].type == CS_T_MAP) { *out = cs_int((int64_t)((cs_map_obj*)argv[0].as.p)->len); return 0; }
    if (argv[0].type == CS_T_SET) { *out = cs_int((int64_t)((cs_map_obj*)argv[0].as.p)->len); return 0; }
    if (argv[0].type == CS_T_STRBUF) { *out = cs_int((int64_t)((cs_strbuf_obj*)argv[0].as.p)->len); return 0; }
    if (argv[0].type == CS_T_BYTES) { *out = cs_int((int64_t)((cs_bytes_obj*)argv[0].as.p)->len); return 0; }
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

    size_t ol = strlen(old);
    size_t rl = strlen(rep);

    // Early exit: no-op replacement (old == rep)
    if (ol == rl && strcmp(old, rep) == 0) {
        *out = cs_str(vm, s);
        return 0;
    }

    // Early exit: pattern not found
    const char* first_hit = strstr(s, old);
    if (!first_hit) {
        *out = cs_str(vm, s);
        return 0;
    }

    // Single-pass build with dynamic buffer
    size_t cap = strlen(s) + 256;  // Start with input size + headroom
    char* buf = (char*)malloc(cap);
    if (!buf) { cs_error(vm, "out of memory"); return 1; }

    size_t w = 0;
    const char* p = s;

    while (1) {
        const char* hit = strstr(p, old);
        if (!hit) {
            // Copy remaining tail
            size_t tail_len = strlen(p);

            // Ensure capacity
            while (w + tail_len >= cap) {
                cap *= 2;
                char* new_buf = (char*)realloc(buf, cap);
                if (!new_buf) {
                    free(buf);
                    cs_error(vm, "out of memory");
                    return 1;
                }
                buf = new_buf;
            }

            memcpy(buf + w, p, tail_len);
            w += tail_len;
            break;
        }

        // Copy part before match
        size_t pre_len = (size_t)(hit - p);

        // Ensure capacity for pre + replacement
        while (w + pre_len + rl >= cap) {
            cap *= 2;
            char* new_buf = (char*)realloc(buf, cap);
            if (!new_buf) {
                free(buf);
                cs_error(vm, "out of memory");
                return 1;
            }
            buf = new_buf;
        }

        memcpy(buf + w, p, pre_len);
        w += pre_len;
        memcpy(buf + w, rep, rl);
        w += rl;

        p = hit + ol;
    }

    buf[w] = '\0';
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

static int nf_read_file_bytes(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
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
    uint8_t* buf = (uint8_t*)malloc(n ? n : 1);
    if (!buf) { fclose(f); cs_error(vm, "out of memory"); return 1; }
    size_t r = fread(buf, 1, n, f);
    fclose(f);
    if (r != n) { free(buf); *out = cs_nil(); return 0; }
    *out = cs_bytes_take(vm, buf, n);
    if (!out->as.p) { cs_error(vm, "out of memory"); return 1; }
    return 0;
}

static int nf_write_file(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || (argv[1].type != CS_T_STR && argv[1].type != CS_T_BYTES)) { *out = cs_bool(0); return 0; }

    const char* path = ((cs_string*)argv[0].as.p)->data;
    const char* data = NULL;
    size_t len = 0;
    if (argv[1].type == CS_T_STR) {
        data = ((cs_string*)argv[1].as.p)->data;
        len = ((cs_string*)argv[1].as.p)->len;
    } else {
        cs_bytes_obj* b = (cs_bytes_obj*)argv[1].as.p;
        data = b ? (const char*)b->data : NULL;
        len = b ? b->len : 0;
    }

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

static int nf_write_file_bytes(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_BYTES) { *out = cs_bool(0); return 0; }

    const char* path = ((cs_string*)argv[0].as.p)->data;
    cs_bytes_obj* b = (cs_bytes_obj*)argv[1].as.p;
    const char* data = b ? (const char*)b->data : NULL;
    size_t len = b ? b->len : 0;

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

static int sb_append_quoted_arg(char** buf, size_t* len, size_t* cap, const char* s) {
    if (!sb_append(buf, len, cap, "\"", 1)) return 0;
    for (size_t i = 0; s && s[i]; i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            if (!sb_append(buf, len, cap, "\\", 1)) return 0;
        }
        if (!sb_append(buf, len, cap, &c, 1)) return 0;
    }
    if (!sb_append(buf, len, cap, "\"", 1)) return 0;
    return 1;
}

static int nf_subprocess(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argc > 2 || argv[0].type != CS_T_STR) { *out = cs_nil(); return 0; }

    cs_list_obj* args = NULL;
    if (argc == 2) {
        if (argv[1].type != CS_T_LIST) { *out = cs_nil(); return 0; }
        args = (cs_list_obj*)argv[1].as.p;
    }

    const char* cmd = cs_to_cstr(argv[0]);
    char* cmd_buf = NULL;
    size_t cmd_len = 0, cmd_cap = 0;

    if (!sb_append_quoted_arg(&cmd_buf, &cmd_len, &cmd_cap, cmd)) {
        free(cmd_buf);
        cs_error(vm, "out of memory");
        return 1;
    }

    if (args) {
        for (size_t i = 0; i < args->len; i++) {
            cs_value v = args->items[i];
            char tmp[128];
            const char* s = (v.type == CS_T_STR) ? cs_to_cstr(v) : value_repr(v, tmp, sizeof(tmp));
            if (!sb_append(&cmd_buf, &cmd_len, &cmd_cap, " ", 1) || !sb_append_quoted_arg(&cmd_buf, &cmd_len, &cmd_cap, s)) {
                free(cmd_buf);
                cs_error(vm, "out of memory");
                return 1;
            }
        }
    }

    if (!sb_append(&cmd_buf, &cmd_len, &cmd_cap, " 2>&1", 5)) {
        free(cmd_buf);
        cs_error(vm, "out of memory");
        return 1;
    }

    FILE* fp = CS_POPEN(cmd_buf, "r");
    if (!fp) {
        free(cmd_buf);
        cs_error(vm, "subprocess() failed to spawn command");
        return 1;
    }

    char* out_buf = NULL;
    size_t out_len = 0, out_cap = 0;
    char chunk[4096];
    size_t nread;
    while ((nread = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (!sb_append(&out_buf, &out_len, &out_cap, chunk, nread)) {
            CS_PCLOSE(fp);
            free(cmd_buf);
            free(out_buf);
            cs_error(vm, "out of memory");
            return 1;
        }
    }

    int status = CS_PCLOSE(fp);
    free(cmd_buf);

    int exit_code = status;
#if !defined(_WIN32)
    if (WIFEXITED(status)) exit_code = WEXITSTATUS(status);
#endif

    cs_value out_map = cs_map(vm);
    if (!out_map.as.p) { free(out_buf); cs_error(vm, "out of memory"); return 1; }

    cs_value out_str = out_buf ? cs_str_take(vm, out_buf, (uint64_t)out_len) : cs_str(vm, "");
    if (out_str.type == CS_T_NIL) { cs_value_release(out_map); cs_error(vm, "out of memory"); return 1; }
    if (cs_map_set(out_map, "out", out_str) != 0) { cs_value_release(out_str); cs_value_release(out_map); cs_error(vm, "out of memory"); return 1; }
    cs_value_release(out_str);

    cs_value code_val = cs_int((int64_t)exit_code);
    if (cs_map_set(out_map, "code", code_val) != 0) { cs_value_release(out_map); cs_error(vm, "out of memory"); return 1; }

    *out = out_map;
    return 0;
}

// ---------- Advanced File Handling (Linux only) ----------

#if defined(__linux__)

// ---------- Glob Patterns ----------

typedef struct {
    char** patterns;
    size_t pattern_count;
    int recursive;
    const char* base_path;
} glob_matcher;

static int glob_match_simple(const char* pattern, const char* str) {
    // Simple glob matching with *, ?, [], {}
    return fnmatch(pattern, str, FNM_PATHNAME | FNM_PERIOD) == 0;
}

static int glob_has_double_star(const char* pattern) {
    const char* p = pattern;
    while (*p) {
        if (p[0] == '*' && p[1] == '*') return 1;
        p++;
    }
    return 0;
}

static void glob_scan_directory(cs_vm* vm, const char* path, const char* pattern, cs_value result, int depth, int max_depth) {
    if (depth > max_depth) return;
    
    DIR* dir = opendir(path);
    if (!dir) return;
    
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        const char* name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        
        char* full_path = path_join_alloc(path, name);
        if (!full_path) continue;
        
        struct stat st;
        if (stat(full_path, &st) != 0) {
            free(full_path);
            continue;
        }
        
        // Match the pattern against the filename
        if (glob_match_simple(pattern, name)) {
            cs_value sv = cs_str(vm, full_path);
            if (sv.type == CS_T_STR) {
                cs_list_push(result, sv);
                cs_value_release(sv);
            }
        }
        
        // Recursively scan subdirectories if we haven't hit max depth
        if (S_ISDIR(st.st_mode) && depth < max_depth) {
            glob_scan_directory(vm, full_path, pattern, result, depth + 1, max_depth);
        }
        
        free(full_path);
    }
    closedir(dir);
}

static int nf_glob(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argc > 2 || argv[0].type != CS_T_STR) {
        *out = cs_nil();
        return 0;
    }
    
    const char* pattern = cs_to_cstr(argv[0]);
    const char* base_path = ".";
    
    if (argc == 2 && argv[1].type == CS_T_STR) {
        base_path = cs_to_cstr(argv[1]);
    }
    
    char* resolved_base = resolve_path_alloc(vm, base_path);
    if (!resolved_base) {
        cs_error(vm, "out of memory");
        return 1;
    }
    
    cs_value listv = cs_list(vm);
    if (listv.type != CS_T_LIST) {
        free(resolved_base);
        *out = cs_nil();
        return 0;
    }
    
    // Check if pattern has ** for recursive matching
    int is_recursive = glob_has_double_star(pattern);
    int max_depth = is_recursive ? 100 : 0;
    
    // Remove ** from pattern for matching
    char* clean_pattern = cs_strdup2_local(pattern);
    if (!clean_pattern) {
        free(resolved_base);
        cs_value_release(listv);
        cs_error(vm, "out of memory");
        return 1;
    }
    
    // Replace **/  with empty string (for patterns like **/*.txt)
    char* p = clean_pattern;
    while (*p) {
        if (p[0] == '*' && p[1] == '*' && p[2] == '/') {
            // Remove **/ entirely
            memmove(p, p + 3, strlen(p + 3) + 1);
        } else if (p[0] == '*' && p[1] == '*') {
            // Just ** without / - replace with *
            p[0] = '*';
            memmove(p + 1, p + 2, strlen(p + 2) + 1);
        } else {
            p++;
        }
    }
    
    glob_scan_directory(vm, resolved_base, clean_pattern, listv, 0, max_depth);
    
    free(clean_pattern);
    free(resolved_base);
    *out = listv;
    return 0;
}

// ---------- File Watching with inotify ----------

#define MAX_WATCHES 256
#define INOTIFY_EVENT_SIZE (sizeof(struct inotify_event))
#define INOTIFY_BUF_LEN (1024 * (INOTIFY_EVENT_SIZE + 16))

typedef struct {
    int handle_id;
    int inotify_fd;
    int watch_fd;
    char* path;
    cs_value callback;
    int is_directory;
    int recursive;
} file_watch;

static file_watch* watches[MAX_WATCHES];
static int next_watch_handle = 1;
static int inotify_global_fd = -1;

static void init_inotify() {
    if (inotify_global_fd < 0) {
        inotify_global_fd = inotify_init1(IN_NONBLOCK);
    }
}

static int find_watch_by_handle(int handle) {
    for (int i = 0; i < MAX_WATCHES; i++) {
        if (watches[i] && watches[i]->handle_id == handle) return i;
    }
    return -1;
}

static void cleanup_watch(int idx) {
    if (idx < 0 || idx >= MAX_WATCHES || !watches[idx]) return;
    
    file_watch* w = watches[idx];
    if (w->watch_fd >= 0 && inotify_global_fd >= 0) {
        inotify_rm_watch(inotify_global_fd, w->watch_fd);
    }
    free(w->path);
    cs_value_release(w->callback);
    free(w);
    watches[idx] = NULL;
}

static const char* inotify_event_type(uint32_t mask) {
    if (mask & IN_CREATE) return "created";
    if (mask & IN_MODIFY) return "modified";
    if (mask & IN_DELETE) return "deleted";
    if (mask & IN_MOVED_FROM || mask & IN_MOVED_TO) return "renamed";
    return "changed";
}

static void process_inotify_events(cs_vm* vm) {
    if (inotify_global_fd < 0) return;
    
    char buffer[INOTIFY_BUF_LEN];
    ssize_t len = read(inotify_global_fd, buffer, INOTIFY_BUF_LEN);
    if (len < 0) return;
    
    int i = 0;
    while (i < len) {
        struct inotify_event* event = (struct inotify_event*)&buffer[i];
        
        // Find the watch that corresponds to this event
        for (int w = 0; w < MAX_WATCHES; w++) {
            if (watches[w] && watches[w]->watch_fd == event->wd) {
                file_watch* watch = watches[w];
                
                // Build full path
                char* event_path = watch->path;
                if (event->len > 0) {
                    event_path = path_join_alloc(watch->path, event->name);
                }
                
                // Call the callback
                cs_value event_type = cs_str(vm, inotify_event_type(event->mask));
                cs_value path_val = cs_str(vm, event_path);
                
                cs_value args[2] = {event_type, path_val};
                cs_value result = cs_nil();
                cs_call_value(vm, watch->callback, 2, args, &result);
                
                cs_value_release(event_type);
                cs_value_release(path_val);
                cs_value_release(result);
                
                if (event->len > 0 && event_path != watch->path) {
                    free(event_path);
                }
                break;
            }
        }
        
        i += INOTIFY_EVENT_SIZE + event->len;
    }
}

static int nf_watch_file(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || (argv[1].type != CS_T_FUNC && argv[1].type != CS_T_NATIVE)) {
        *out = cs_int(-1);
        return 0;
    }
    
    init_inotify();
    if (inotify_global_fd < 0) {
        cs_error(vm, "failed to initialize inotify");
        return 1;
    }
    
    const char* path = cs_to_cstr(argv[0]);
    char* resolved = resolve_path_alloc(vm, path);
    if (!resolved) {
        cs_error(vm, "out of memory");
        return 1;
    }
    
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_WATCHES; i++) {
        if (!watches[i]) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        free(resolved);
        cs_error(vm, "too many watches");
        return 1;
    }
    
    int wd = inotify_add_watch(inotify_global_fd, resolved, 
                               IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVE);
    if (wd < 0) {
        free(resolved);
        cs_error(vm, "failed to add watch");
        return 1;
    }
    
    file_watch* w = (file_watch*)malloc(sizeof(file_watch));
    if (!w) {
        inotify_rm_watch(inotify_global_fd, wd);
        free(resolved);
        cs_error(vm, "out of memory");
        return 1;
    }
    
    w->handle_id = next_watch_handle++;
    w->inotify_fd = inotify_global_fd;
    w->watch_fd = wd;
    w->path = resolved;
    w->callback = cs_value_copy(argv[1]);
    w->is_directory = 0;
    w->recursive = 0;
    
    watches[slot] = w;
    *out = cs_int(w->handle_id);
    return 0;
}

static int nf_watch_dir(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 2 || argc > 3 || argv[0].type != CS_T_STR || (argv[1].type != CS_T_FUNC && argv[1].type != CS_T_NATIVE)) {
        *out = cs_int(-1);
        return 0;
    }
    
    int recursive = 0;
    if (argc == 3 && argv[2].type == CS_T_BOOL) {
        recursive = argv[2].as.b;
    }
    
    init_inotify();
    if (inotify_global_fd < 0) {
        cs_error(vm, "failed to initialize inotify");
        return 1;
    }
    
    const char* path = cs_to_cstr(argv[0]);
    char* resolved = resolve_path_alloc(vm, path);
    if (!resolved) {
        cs_error(vm, "out of memory");
        return 1;
    }
    
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_WATCHES; i++) {
        if (!watches[i]) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        free(resolved);
        cs_error(vm, "too many watches");
        return 1;
    }
    
    uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVE;
    if (recursive) mask |= IN_CREATE; // Watch for new subdirectories
    
    int wd = inotify_add_watch(inotify_global_fd, resolved, mask);
    if (wd < 0) {
        free(resolved);
        cs_error(vm, "failed to add watch");
        return 1;
    }
    
    file_watch* w = (file_watch*)malloc(sizeof(file_watch));
    if (!w) {
        inotify_rm_watch(inotify_global_fd, wd);
        free(resolved);
        cs_error(vm, "out of memory");
        return 1;
    }
    
    w->handle_id = next_watch_handle++;
    w->inotify_fd = inotify_global_fd;
    w->watch_fd = wd;
    w->path = resolved;
    w->callback = cs_value_copy(argv[1]);
    w->is_directory = 1;
    w->recursive = recursive;
    
    watches[slot] = w;
    *out = cs_int(w->handle_id);
    return 0;
}

static int nf_unwatch(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm;
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_INT) {
        *out = cs_bool(0);
        return 0;
    }
    
    int handle = (int)argv[0].as.i;
    int idx = find_watch_by_handle(handle);
    if (idx < 0) {
        *out = cs_bool(0);
        return 0;
    }
    
    cleanup_watch(idx);
    *out = cs_bool(1);
    return 0;
}

static int nf_process_file_watches(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    (void)argc;
    (void)argv;
    if (!out) return 0;
    
    process_inotify_events(vm);
    *out = cs_nil();
    return 0;
}

// ---------- Temp Files ----------

#define MAX_TEMP_FILES 512
static char* temp_files[MAX_TEMP_FILES];
static int temp_file_count = 0;
static int temp_cleanup_registered = 0;

static void cleanup_temp_files() {
    for (int i = 0; i < temp_file_count; i++) {
        if (temp_files[i]) {
            remove(temp_files[i]);
            rmdir(temp_files[i]); // Try both
            free(temp_files[i]);
            temp_files[i] = NULL;
        }
    }
}

static void register_temp_cleanup(const char* path) {
    if (!temp_cleanup_registered) {
        atexit(cleanup_temp_files);
        temp_cleanup_registered = 1;
    }
    
    if (temp_file_count < MAX_TEMP_FILES) {
        temp_files[temp_file_count++] = cs_strdup2_local(path);
    }
}

static char* create_temp_path(const char* prefix, const char* suffix, int is_dir) {
    const char* tmp_dir = getenv("TMPDIR");
    if (!tmp_dir) tmp_dir = "/tmp";
    
    char template[1024];
    // Template must end with XXXXXX for mkstemp/mkdtemp
    snprintf(template, sizeof(template), "%s/%sXXXXXX", 
             tmp_dir, prefix ? prefix : "cs_");
    
    char* path = cs_strdup2_local(template);
    if (!path) return NULL;
    
    if (is_dir) {
        if (mkdtemp(path) == NULL) {
            free(path);
            return NULL;
        }
    } else {
        // Create temp file with mkstemp
        int fd = mkstemp(path);
        if (fd < 0) {
            free(path);
            return NULL;
        }
        close(fd);
        
        // If suffix requested, rename the file to add suffix
        if (suffix && suffix[0]) {
            size_t path_len = strlen(path);
            size_t suffix_len = strlen(suffix);
            char* new_path = (char*)malloc(path_len + suffix_len + 1);
            if (new_path) {
                memcpy(new_path, path, path_len);
                memcpy(new_path + path_len, suffix, suffix_len + 1);
                if (rename(path, new_path) == 0) {
                    free(path);
                    path = new_path;
                } else {
                    // Rename failed, keep original path
                    free(new_path);
                }
            }
        }
    }
    
    register_temp_cleanup(path);
    return path;
}

static int nf_temp_file(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    
    const char* prefix = NULL;
    const char* suffix = NULL;
    
    if (argc >= 1 && argv[0].type == CS_T_STR) {
        prefix = cs_to_cstr(argv[0]);
    }
    if (argc >= 2 && argv[1].type == CS_T_STR) {
        suffix = cs_to_cstr(argv[1]);
    }
    
    char* path = create_temp_path(prefix, suffix, 0);
    if (!path) {
        cs_error(vm, "failed to create temp file");
        return 1;
    }
    
    cs_value result = cs_str(vm, path);
    free(path);
    
    *out = result;
    return 0;
}

static int nf_temp_dir(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    
    const char* prefix = NULL;
    if (argc >= 1 && argv[0].type == CS_T_STR) {
        prefix = cs_to_cstr(argv[0]);
    }
    
    char* path = create_temp_path(prefix, NULL, 1);
    if (!path) {
        cs_error(vm, "failed to create temp directory");
        return 1;
    }
    
    cs_value result = cs_str(vm, path);
    free(path);
    
    *out = result;
    return 0;
}

// ---------- Archive Operations ----------

// Zip using miniz (we'll use zlib for gzip and implement tar manually)

static int nf_gzip_compress(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) {
        *out = cs_bool(0);
        return 0;
    }
    
    const char* input_path = cs_to_cstr(argv[0]);
    const char* output_path = cs_to_cstr(argv[1]);
    
    char* resolved_in = resolve_path_alloc(vm, input_path);
    char* resolved_out = resolve_path_alloc(vm, output_path);
    if (!resolved_in || !resolved_out) {
        free(resolved_in);
        free(resolved_out);
        cs_error(vm, "out of memory");
        return 1;
    }
    
    FILE* in = fopen(resolved_in, "rb");
    if (!in) {
        free(resolved_in);
        free(resolved_out);
        *out = cs_bool(0);
        return 0;
    }
    
    gzFile out_gz = gzopen(resolved_out, "wb");
    if (!out_gz) {
        fclose(in);
        free(resolved_in);
        free(resolved_out);
        *out = cs_bool(0);
        return 0;
    }
    
    char buffer[8192];
    size_t bytes_read;
    int success = 1;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (gzwrite(out_gz, buffer, (unsigned)bytes_read) != (int)bytes_read) {
            success = 0;
            break;
        }
    }
    
    fclose(in);
    gzclose(out_gz);
    free(resolved_in);
    free(resolved_out);
    
    *out = cs_bool(success);
    return 0;
}

static int nf_gzip_decompress(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) {
        *out = cs_bool(0);
        return 0;
    }
    
    const char* input_path = cs_to_cstr(argv[0]);
    const char* output_path = cs_to_cstr(argv[1]);
    
    char* resolved_in = resolve_path_alloc(vm, input_path);
    char* resolved_out = resolve_path_alloc(vm, output_path);
    if (!resolved_in || !resolved_out) {
        free(resolved_in);
        free(resolved_out);
        cs_error(vm, "out of memory");
        return 1;
    }
    
    gzFile in_gz = gzopen(resolved_in, "rb");
    if (!in_gz) {
        free(resolved_in);
        free(resolved_out);
        *out = cs_bool(0);
        return 0;
    }
    
    FILE* out_file = fopen(resolved_out, "wb");
    if (!out_file) {
        gzclose(in_gz);
        free(resolved_in);
        free(resolved_out);
        *out = cs_bool(0);
        return 0;
    }
    
    char buffer[8192];
    int bytes_read;
    int success = 1;
    
    while ((bytes_read = gzread(in_gz, buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, 1, bytes_read, out_file) != (size_t)bytes_read) {
            success = 0;
            break;
        }
    }
    
    gzclose(in_gz);
    fclose(out_file);
    free(resolved_in);
    free(resolved_out);
    
    *out = cs_bool(success);
    return 0;
}

// Simple TAR implementation (POSIX tar format)
#define TAR_BLOCK_SIZE 512
#define TAR_NAME_SIZE 100
#define TAR_MODE_SIZE 8
#define TAR_UID_SIZE 8
#define TAR_GID_SIZE 8
#define TAR_SIZE_SIZE 12
#define TAR_MTIME_SIZE 12
#define TAR_CHKSUM_SIZE 8
#define TAR_TYPEFLAG_SIZE 1
#define TAR_LINKNAME_SIZE 100
#define TAR_MAGIC_SIZE 6
#define TAR_VERSION_SIZE 2
#define TAR_UNAME_SIZE 32
#define TAR_GNAME_SIZE 32
#define TAR_DEVMAJOR_SIZE 8
#define TAR_DEVMINOR_SIZE 8
#define TAR_PREFIX_SIZE 155

typedef struct {
    char name[TAR_NAME_SIZE];
    char mode[TAR_MODE_SIZE];
    char uid[TAR_UID_SIZE];
    char gid[TAR_GID_SIZE];
    char size[TAR_SIZE_SIZE];
    char mtime[TAR_MTIME_SIZE];
    char chksum[TAR_CHKSUM_SIZE];
    char typeflag[TAR_TYPEFLAG_SIZE];
    char linkname[TAR_LINKNAME_SIZE];
    char magic[TAR_MAGIC_SIZE];
    char version[TAR_VERSION_SIZE];
    char uname[TAR_UNAME_SIZE];
    char gname[TAR_GNAME_SIZE];
    char devmajor[TAR_DEVMAJOR_SIZE];
    char devminor[TAR_DEVMINOR_SIZE];
    char prefix[TAR_PREFIX_SIZE];
    char padding[12];
} tar_header;

static void tar_format_octal(char* dest, size_t size, uint64_t value) {
    // Format as octal, null-terminated, filling the field
    // TAR format uses null or space padding
    if (size == 0) return;
    int len = snprintf(dest, size, "%0*lo", (int)(size - 1), (unsigned long)value);
    if (len < 0 || (size_t)len >= size) {
        // Truncated - just zero-fill as fallback
        memset(dest, '0', size - 1);
        dest[size - 1] = '\0';
    }
}

static uint64_t tar_parse_octal(const char* str, size_t size) {
    char buf[256];
    size_t len = size < sizeof(buf) ? size : sizeof(buf) - 1;
    memcpy(buf, str, len);
    buf[len] = 0;
    return (uint64_t)strtoul(buf, NULL, 8);
}

static void tar_calculate_checksum(tar_header* header) {
    memset(header->chksum, ' ', TAR_CHKSUM_SIZE);
    unsigned int checksum = 0;
    unsigned char* p = (unsigned char*)header;
    for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
        checksum += p[i];
    }
    tar_format_octal(header->chksum, TAR_CHKSUM_SIZE, checksum);
}

static int tar_add_file(FILE* tar, const char* filename, const char* arcname) {
    FILE* f = fopen(filename, "rb");
    if (!f) return 0;
    
    // Get file stats
    struct stat st;
    if (fstat(fileno(f), &st) != 0) {
        fclose(f);
        return 0;
    }
    
    // Create header
    tar_header header;
    memset(&header, 0, sizeof(header));
    
    strncpy(header.name, arcname, TAR_NAME_SIZE - 1);
    tar_format_octal(header.mode, TAR_MODE_SIZE, st.st_mode & 0777);
    tar_format_octal(header.uid, TAR_UID_SIZE, st.st_uid);
    tar_format_octal(header.gid, TAR_GID_SIZE, st.st_gid);
    tar_format_octal(header.size, TAR_SIZE_SIZE, st.st_size);
    tar_format_octal(header.mtime, TAR_MTIME_SIZE, st.st_mtime);
    header.typeflag[0] = '0'; // Regular file
    memcpy(header.magic, "ustar", 5);
    memcpy(header.version, "00", 2);
    
    tar_calculate_checksum(&header);
    
    // Write header
    if (fwrite(&header, TAR_BLOCK_SIZE, 1, tar) != 1) {
        fclose(f);
        return 0;
    }
    
    // Write file content
    char buffer[TAR_BLOCK_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, TAR_BLOCK_SIZE, f)) > 0) {
        if (bytes_read < TAR_BLOCK_SIZE) {
            memset(buffer + bytes_read, 0, TAR_BLOCK_SIZE - bytes_read);
        }
        if (fwrite(buffer, TAR_BLOCK_SIZE, 1, tar) != 1) {
            fclose(f);
            return 0;
        }
    }
    
    fclose(f);
    return 1;
}

static int nf_tar_create(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 2 || argc > 3 || argv[0].type != CS_T_STR || argv[1].type != CS_T_LIST) {
        *out = cs_bool(0);
        return 0;
    }
    
    const char* archive_path = cs_to_cstr(argv[0]);
    cs_list_obj* files = (cs_list_obj*)argv[1].as.p;
    const char* compress = NULL;
    
    if (argc == 3 && argv[2].type == CS_T_STR) {
        compress = cs_to_cstr(argv[2]);
    }
    
    char* resolved = resolve_path_alloc(vm, archive_path);
    if (!resolved) {
        cs_error(vm, "out of memory");
        return 1;
    }
    
    // Determine output file (use temp file if gzip compression)
    char* tar_path = resolved;
    char temp_tar[1024];
    if (compress && strcmp(compress, "gzip") == 0) {
        snprintf(temp_tar, sizeof(temp_tar), "%s.tmp", resolved);
        tar_path = temp_tar;
    }
    
    FILE* tar = fopen(tar_path, "wb");
    if (!tar) {
        free(resolved);
        *out = cs_bool(0);
        return 0;
    }
    
    int success = 1;
    for (size_t i = 0; i < files->len; i++) {
        cs_value v = files->items[i];
        if (v.type != CS_T_STR) continue;
        
        const char* file = cs_to_cstr(v);
        char* file_resolved = resolve_path_alloc(vm, file);
        if (!file_resolved) continue;
        
        // Use basename as archive name
        const char* arcname = strrchr(file, '/');
        if (!arcname) arcname = strrchr(file, '\\');
        arcname = arcname ? arcname + 1 : file;
        
        if (!tar_add_file(tar, file_resolved, arcname)) {
            success = 0;
        }
        
        free(file_resolved);
    }
    
    // Write two empty blocks to mark end of archive
    char empty[TAR_BLOCK_SIZE];
    memset(empty, 0, TAR_BLOCK_SIZE);
    fwrite(empty, TAR_BLOCK_SIZE, 1, tar);
    fwrite(empty, TAR_BLOCK_SIZE, 1, tar);
    
    fclose(tar);
    
    // Compress if needed
    if (success && compress && strcmp(compress, "gzip") == 0) {
        gzFile gz = gzopen(resolved, "wb");
        if (gz) {
            FILE* in = fopen(tar_path, "rb");
            if (in) {
                char buffer[8192];
                size_t bytes;
                while ((bytes = fread(buffer, 1, sizeof(buffer), in)) > 0) {
                    gzwrite(gz, buffer, (unsigned)bytes);
                }
                fclose(in);
            }
            gzclose(gz);
            remove(tar_path);
        } else {
            success = 0;
        }
    }
    
    free(resolved);
    *out = cs_bool(success);
    return 0;
}

static int nf_tar_list(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) {
        *out = cs_nil();
        return 0;
    }
    
    const char* archive_path = cs_to_cstr(argv[0]);
    char* resolved = resolve_path_alloc(vm, archive_path);
    if (!resolved) {
        cs_error(vm, "out of memory");
        return 1;
    }
    
    // Check if gzipped
    int is_gzipped = 0;
    const char* ext = strrchr(archive_path, '.');
    if (ext && strcmp(ext, ".gz") == 0) {
        is_gzipped = 1;
    }
    
    FILE* tar = NULL;
    gzFile gz = NULL;
    char temp_tar[1024];
    
    if (is_gzipped) {
        // Decompress to temp file
        snprintf(temp_tar, sizeof(temp_tar), "/tmp/cs_tar_XXXXXX");
        int fd = mkstemp(temp_tar);
        if (fd < 0) {
            free(resolved);
            *out = cs_nil();
            return 0;
        }
        close(fd);
        
        gz = gzopen(resolved, "rb");
        tar = fopen(temp_tar, "wb");
        if (gz && tar) {
            char buffer[8192];
            int bytes;
            while ((bytes = gzread(gz, buffer, sizeof(buffer))) > 0) {
                fwrite(buffer, 1, bytes, tar);
            }
            fclose(tar);
            gzclose(gz);
            tar = fopen(temp_tar, "rb");
        }
    } else {
        tar = fopen(resolved, "rb");
    }
    
    if (!tar) {
        free(resolved);
        if (is_gzipped) remove(temp_tar);
        *out = cs_nil();
        return 0;
    }
    
    cs_value listv = cs_list(vm);
    if (listv.type != CS_T_LIST) {
        fclose(tar);
        free(resolved);
        if (is_gzipped) remove(temp_tar);
        *out = cs_nil();
        return 0;
    }
    
    tar_header header;
    while (fread(&header, TAR_BLOCK_SIZE, 1, tar) == 1) {
        // Check for end of archive
        if (header.name[0] == '\0') break;
        
        // Get file name
        cs_value sv = cs_str(vm, header.name);
        if (sv.type == CS_T_STR) {
            cs_list_push(listv, sv);
            cs_value_release(sv);
        }
        
        // Skip file content
        uint64_t size = tar_parse_octal(header.size, TAR_SIZE_SIZE);
        uint64_t blocks = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
        fseek(tar, blocks * TAR_BLOCK_SIZE, SEEK_CUR);
    }
    
    fclose(tar);
    free(resolved);
    if (is_gzipped) remove(temp_tar);
    
    *out = listv;
    return 0;
}

static int nf_tar_extract(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_STR || argv[1].type != CS_T_STR) {
        *out = cs_bool(0);
        return 0;
    }
    
    const char* archive_path = cs_to_cstr(argv[0]);
    const char* dest_path = cs_to_cstr(argv[1]);
    
    char* resolved_arc = resolve_path_alloc(vm, archive_path);
    char* resolved_dest = resolve_path_alloc(vm, dest_path);
    if (!resolved_arc || !resolved_dest) {
        free(resolved_arc);
        free(resolved_dest);
        cs_error(vm, "out of memory");
        return 1;
    }
    
    // Create destination directory if needed
    mkdir(resolved_dest, 0777);
    
    // Check if gzipped
    int is_gzipped = 0;
    const char* ext = strrchr(archive_path, '.');
    if (ext && strcmp(ext, ".gz") == 0) {
        is_gzipped = 1;
    }
    
    FILE* tar = NULL;
    char temp_tar[1024];
    
    if (is_gzipped) {
        // Decompress to temp file
        snprintf(temp_tar, sizeof(temp_tar), "/tmp/cs_tar_XXXXXX");
        int fd = mkstemp(temp_tar);
        if (fd < 0) {
            free(resolved_arc);
            free(resolved_dest);
            *out = cs_bool(0);
            return 0;
        }
        close(fd);
        
        gzFile gz = gzopen(resolved_arc, "rb");
        tar = fopen(temp_tar, "wb");
        if (gz && tar) {
            char buffer[8192];
            int bytes;
            while ((bytes = gzread(gz, buffer, sizeof(buffer))) > 0) {
                fwrite(buffer, 1, bytes, tar);
            }
            fclose(tar);
            gzclose(gz);
            tar = fopen(temp_tar, "rb");
        }
    } else {
        tar = fopen(resolved_arc, "rb");
    }
    
    if (!tar) {
        free(resolved_arc);
        free(resolved_dest);
        if (is_gzipped) remove(temp_tar);
        *out = cs_bool(0);
        return 0;
    }
    
    int success = 1;
    tar_header header;
    while (fread(&header, TAR_BLOCK_SIZE, 1, tar) == 1) {
        if (header.name[0] == '\0') break;
        
        char* out_path = path_join_alloc(resolved_dest, header.name);
        if (!out_path) {
            success = 0;
            break;
        }
        
        uint64_t size = tar_parse_octal(header.size, TAR_SIZE_SIZE);
        
        // Extract regular file
        if (header.typeflag[0] == '0' || header.typeflag[0] == '\0') {
            FILE* f = fopen(out_path, "wb");
            if (f) {
                uint64_t remaining = size;
                char buffer[TAR_BLOCK_SIZE];
                while (remaining > 0) {
                    size_t to_read = remaining > TAR_BLOCK_SIZE ? TAR_BLOCK_SIZE : (size_t)remaining;
                    if (fread(buffer, 1, TAR_BLOCK_SIZE, tar) != TAR_BLOCK_SIZE) {
                        success = 0;
                        break;
                    }
                    fwrite(buffer, 1, to_read, f);
                    remaining -= to_read;
                }
                fclose(f);
            } else {
                success = 0;
                // Skip blocks
                uint64_t blocks = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
                fseek(tar, blocks * TAR_BLOCK_SIZE, SEEK_CUR);
            }
        } else {
            // Skip non-regular files
            uint64_t blocks = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
            fseek(tar, blocks * TAR_BLOCK_SIZE, SEEK_CUR);
        }
        
        free(out_path);
    }
    
    fclose(tar);
    free(resolved_arc);
    free(resolved_dest);
    if (is_gzipped) remove(temp_tar);
    
    *out = cs_bool(success);
    return 0;
}

#endif // __linux__

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
                case '"': c = '"'; if (!sb_append(&buf, &len, &cap, &c, 1)) { *ok = 0; } break;
                case '\\': c = '\\'; if (!sb_append(&buf, &len, &cap, &c, 1)) { *ok = 0; } break;
                case '/': c = '/'; if (!sb_append(&buf, &len, &cap, &c, 1)) { *ok = 0; } break;
                case 'b': c = '\b'; if (!sb_append(&buf, &len, &cap, &c, 1)) { *ok = 0; } break;
                case 'f': c = '\f'; if (!sb_append(&buf, &len, &cap, &c, 1)) { *ok = 0; } break;
                case 'n': c = '\n'; if (!sb_append(&buf, &len, &cap, &c, 1)) { *ok = 0; } break;
                case 'r': c = '\r'; if (!sb_append(&buf, &len, &cap, &c, 1)) { *ok = 0; } break;
                case 't': c = '\t'; if (!sb_append(&buf, &len, &cap, &c, 1)) { *ok = 0; } break;
                case 'u': {
                    if (p->pos + 4 > p->len) { *ok = 0; break; }
                    int h1 = json_hex_val(p->s[p->pos++]);
                    int h2 = json_hex_val(p->s[p->pos++]);
                    int h3 = json_hex_val(p->s[p->pos++]);
                    int h4 = json_hex_val(p->s[p->pos++]);
                    if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) { *ok = 0; break; }
                    int code = (h1 << 12) | (h2 << 8) | (h3 << 4) | h4;
                    // Handle surrogate pairs
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        // High surrogate, expect another \uXXXX
                        if (p->pos + 6 <= p->len && p->s[p->pos] == '\\' && p->s[p->pos+1] == 'u') {
                            p->pos += 2;
                            int l1 = json_hex_val(p->s[p->pos++]);
                            int l2 = json_hex_val(p->s[p->pos++]);
                            int l3 = json_hex_val(p->s[p->pos++]);
                            int l4 = json_hex_val(p->s[p->pos++]);
                            if (l1 < 0 || l2 < 0 || l3 < 0 || l4 < 0) { *ok = 0; break; }
                            int low = (l1 << 12) | (l2 << 8) | (l3 << 4) | l4;
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                // Valid surrogate pair
                                uint32_t full = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
                                char utf8[4];
                                int utf8len = 0;
                                if (full <= 0x10FFFF) {
                                    utf8[0] = 0xF0 | ((full >> 18) & 0x07);
                                    utf8[1] = 0x80 | ((full >> 12) & 0x3F);
                                    utf8[2] = 0x80 | ((full >> 6) & 0x3F);
                                    utf8[3] = 0x80 | (full & 0x3F);
                                    utf8len = 4;
                                }
                                if (!sb_append(&buf, &len, &cap, utf8, utf8len)) { *ok = 0; break; }
                                break;
                            } else {
                                // Invalid low surrogate
                                *ok = 0; break;
                            }
                        } else {
                            // Missing low surrogate
                            *ok = 0; break;
                        }
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        // Unexpected low surrogate
                        *ok = 0; break;
                    } else {
                        // Encode as UTF-8
                        char utf8[3];
                        int utf8len = 0;
                        if (code < 0x80) {
                            utf8[0] = (char)code;
                            utf8len = 1;
                        } else if (code < 0x800) {
                            utf8[0] = 0xC0 | ((code >> 6) & 0x1F);
                            utf8[1] = 0x80 | (code & 0x3F);
                            utf8len = 2;
                        } else {
                            utf8[0] = 0xE0 | ((code >> 12) & 0x0F);
                            utf8[1] = 0x80 | ((code >> 6) & 0x3F);
                            utf8[2] = 0x80 | (code & 0x3F);
                            utf8len = 3;
                        }
                        if (!sb_append(&buf, &len, &cap, utf8, utf8len)) { *ok = 0; break; }
                    }
                    break;
                }
                default:
                    c = e;
                    if (!sb_append(&buf, &len, &cap, &c, 1)) { *ok = 0; }
                    break;
            }
            if (!*ok) break;
            continue;
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
                // Only allow string keys
                cs_value key = m->entries[i].key;
                if (key.type != CS_T_STR) {
                    cs_error(vm, "json_stringify(): object keys must be strings (RFC 8259)");
                    return 0;
                }
                if (!first && !sb_append(buf, len, cap, ",", 1)) return 0;
                first = 0;
                const char* kstr = cs_to_cstr(key);
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

// ============================================================================
// CSV Parser (RFC 4180 + extensions)
// ============================================================================

typedef struct {
    const char* s;
    size_t len;
    size_t pos;
    int row;
    int col;
    char delimiter;
    char quote;
    int skip_empty;
    int trim;
    cs_vm* vm;
} csv_parser;

static void csv_init(csv_parser* p, const char* s, size_t len, cs_vm* vm) {
    p->s = s;
    p->len = len;
    p->pos = 0;
    p->row = 1;
    p->col = 1;
    p->delimiter = ',';
    p->quote = '"';
    p->skip_empty = 0;
    p->trim = 0;
    p->vm = vm;
}

// Skip UTF-8 BOM if present
static void csv_skip_bom(csv_parser* p) {
    if (p->pos + 3 <= p->len &&
        (unsigned char)p->s[p->pos] == 0xEF &&
        (unsigned char)p->s[p->pos+1] == 0xBB &&
        (unsigned char)p->s[p->pos+2] == 0xBF) {
        p->pos += 3;
    }
}

// Check if at end of line or end of file
static int csv_at_eol(csv_parser* p) {
    if (p->pos >= p->len) return 1;
    char c = p->s[p->pos];
    return (c == '\r' || c == '\n');
}

// Skip to end of line
static void csv_skip_eol(csv_parser* p) {
    if (p->pos >= p->len) return;
    char c = p->s[p->pos];
    if (c == '\r') {
        p->pos++;
        if (p->pos < p->len && p->s[p->pos] == '\n') p->pos++; // \r\n
    } else if (c == '\n') {
        p->pos++;
    }
    p->row++;
    p->col = 1;
}

// Parse a single CSV field
static cs_value csv_parse_field(csv_parser* p, int* ok) {
    if (!ok) return cs_nil();
    *ok = 1;

    size_t start_col = p->col;
    char* buf = NULL;
    size_t len = 0, cap = 0;

    // Check if field starts with quote
    if (p->pos < p->len && p->s[p->pos] == p->quote) {
        p->pos++;
        p->col++;

        // Quoted field
        while (p->pos < p->len) {
            char c = p->s[p->pos];
            if (c == p->quote) {
                p->pos++;
                p->col++;
                // Check for escaped quote (double quote)
                if (p->pos < p->len && p->s[p->pos] == p->quote) {
                    // Escaped quote - add single quote to field
                    if (!sb_append(&buf, &len, &cap, &p->quote, 1)) {
                        free(buf);
                        cs_error(p->vm, "out of memory");
                        *ok = 0;
                        return cs_nil();
                    }
                    p->pos++;
                    p->col++;
                } else {
                    // End of quoted field
                    break;
                }
            } else {
                // Regular character (including newlines in quoted fields)
                if (!sb_append(&buf, &len, &cap, &c, 1)) {
                    free(buf);
                    cs_error(p->vm, "out of memory");
                    *ok = 0;
                    return cs_nil();
                }
                p->pos++;
                if (c == '\n') {
                    p->row++;
                    p->col = 1;
                } else {
                    p->col++;
                }
            }
        }

        // Check that we properly closed the quote
        if (p->pos > 0 && p->s[p->pos-1] != p->quote) {
            free(buf);
            char err[256];
            snprintf(err, sizeof(err), "unterminated quoted field at row %d, col %zu", p->row, start_col);
            cs_error(p->vm, err);
            *ok = 0;
            return cs_nil();
        }
    } else {
        // Unquoted field - read until delimiter or EOL
        while (p->pos < p->len) {
            char c = p->s[p->pos];
            if (c == p->delimiter || c == '\r' || c == '\n') break;
            if (!sb_append(&buf, &len, &cap, &c, 1)) {
                free(buf);
                cs_error(p->vm, "out of memory");
                *ok = 0;
                return cs_nil();
            }
            p->pos++;
            p->col++;
        }
    }

    // Trim whitespace if requested
    if (p->trim && buf && len > 0) {
        // Trim leading whitespace
        size_t start = 0;
        while (start < len && isspace((unsigned char)buf[start])) start++;

        // Trim trailing whitespace
        size_t end = len;
        while (end > start && isspace((unsigned char)buf[end-1])) end--;

        if (start > 0 || end < len) {
            size_t new_len = end - start;
            if (new_len > 0) {
                memmove(buf, buf + start, new_len);
            }
            len = new_len;
        }
    }

    // Create string value
    if (!buf) buf = cs_strdup2_local("");
    if (!buf) {
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }
    buf[len] = '\0';
    return cs_str_take(p->vm, buf, (uint64_t)len);
}

// Parse a single CSV row
static cs_value csv_parse_row(csv_parser* p, int* ok) {
    if (!ok) return cs_nil();
    *ok = 1;

    cs_value row = cs_list(p->vm);
    if (row.type != CS_T_LIST) {
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }

    int first_field = 1;
    while (p->pos < p->len && !csv_at_eol(p)) {
        // Add delimiter check (skip delimiter except before first field)
        if (!first_field) {
            if (p->pos < p->len && p->s[p->pos] == p->delimiter) {
                p->pos++;
                p->col++;
            }
        }
        first_field = 0;

        cs_value field = csv_parse_field(p, ok);
        if (!*ok) {
            cs_value_release(row);
            return cs_nil();
        }

        cs_list_push(row, field);
        cs_value_release(field);

        // Check if we're at delimiter or EOL
        if (p->pos < p->len && p->s[p->pos] == p->delimiter) {
            // More fields coming
            continue;
        } else {
            // End of row
            break;
        }
    }

    return row;
}

static int nf_csv_parse(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_STR) {
        cs_error(vm, "csv_parse() requires a string argument");
        return 1;
    }

    cs_string* str_obj = (cs_string*)argv[0].as.p;
    const char* text = str_obj->data;
    size_t text_len = str_obj->len;

    csv_parser p;
    csv_init(&p, text, text_len, vm);

    // Parse options if provided
    int use_headers = 0;
    if (argc >= 2 && argv[1].type == CS_T_MAP) {
        cs_value opt_delimiter = cs_map_get(argv[1], "delimiter");
        if (opt_delimiter.type == CS_T_STR) {
            cs_string* delim_str = (cs_string*)opt_delimiter.as.p;
            if (delim_str->len == 1) {
                p.delimiter = delim_str->data[0];
            }
        }
        cs_value_release(opt_delimiter);

        cs_value opt_quote = cs_map_get(argv[1], "quote");
        if (opt_quote.type == CS_T_STR) {
            cs_string* quote_str = (cs_string*)opt_quote.as.p;
            if (quote_str->len == 1) {
                p.quote = quote_str->data[0];
            }
        }
        cs_value_release(opt_quote);

        cs_value opt_headers = cs_map_get(argv[1], "headers");
        if (opt_headers.type == CS_T_BOOL) {
            use_headers = opt_headers.as.b;
        }
        cs_value_release(opt_headers);

        cs_value opt_skip_empty = cs_map_get(argv[1], "skip_empty");
        if (opt_skip_empty.type == CS_T_BOOL) {
            p.skip_empty = opt_skip_empty.as.b;
        }
        cs_value_release(opt_skip_empty);

        cs_value opt_trim = cs_map_get(argv[1], "trim");
        if (opt_trim.type == CS_T_BOOL) {
            p.trim = opt_trim.as.b;
        }
        cs_value_release(opt_trim);
    }

    // Skip BOM if present
    csv_skip_bom(&p);

    // Parse all rows
    cs_value rows = cs_list(vm);
    if (rows.type != CS_T_LIST) {
        cs_error(vm, "out of memory");
        return 1;
    }

    cs_value header_row = cs_nil();
    int first_row = 1;

    while (p.pos < p.len) {
        // Handle empty lines
        if (csv_at_eol(&p)) {
            if (p.skip_empty) {
                csv_skip_eol(&p);
                continue;
            } else {
                // Parse as empty row (single empty field)
                cs_value empty_row = cs_list(vm);
                if (empty_row.type != CS_T_LIST) {
                    cs_value_release(rows);
                    cs_value_release(header_row);
                    cs_error(vm, "out of memory");
                    return 1;
                }
                cs_value empty_field = cs_str(vm, "");
                cs_list_push(empty_row, empty_field);
                cs_value_release(empty_field);

                if (first_row && use_headers) {
                    header_row = empty_row;
                    first_row = 0;
                } else {
                    cs_list_push(rows, empty_row);
                    cs_value_release(empty_row);
                    first_row = 0;
                }

                csv_skip_eol(&p);
                continue;
            }
        }

        int ok = 1;
        cs_value row = csv_parse_row(&p, &ok);
        if (!ok) {
            cs_value_release(rows);
            cs_value_release(header_row);
            return 1;
        }

        // Check if this is an empty row
        int is_empty = 1;
        size_t row_len = cs_list_len(row);
        for (size_t i = 0; i < row_len; i++) {
            cs_value field = cs_list_get(row, i);
            if (field.type == CS_T_STR) {
                cs_string* s = (cs_string*)field.as.p;
                if (s->len > 0) {
                    is_empty = 0;
                }
            }
            cs_value_release(field);
            if (!is_empty) break;
        }

        if (is_empty && p.skip_empty) {
            cs_value_release(row);
            csv_skip_eol(&p);
            continue;
        }

        // Handle header mode
        if (first_row && use_headers) {
            header_row = row;
            first_row = 0;
            csv_skip_eol(&p);
            continue;
        }

        // Convert row to map if using headers
        if (use_headers && header_row.type == CS_T_LIST) {
            cs_value map_row = cs_map(vm);
            if (map_row.type != CS_T_MAP) {
                cs_value_release(row);
                cs_value_release(rows);
                cs_value_release(header_row);
                cs_error(vm, "out of memory");
                return 1;
            }

            size_t header_len = cs_list_len(header_row);
            row_len = cs_list_len(row);
            for (size_t i = 0; i < row_len && i < header_len; i++) {
                cs_value key = cs_list_get(header_row, i);
                cs_value val = cs_list_get(row, i);

                if (key.type == CS_T_STR) {
                    cs_string* key_str = (cs_string*)key.as.p;
                    cs_map_set(map_row, key_str->data, val);
                }

                cs_value_release(key);
                cs_value_release(val);
            }

            cs_list_push(rows, map_row);
            cs_value_release(map_row);
            cs_value_release(row);
        } else {
            cs_list_push(rows, row);
            cs_value_release(row);
        }

        first_row = 0;
        csv_skip_eol(&p);
    }

    cs_value_release(header_row);
    *out = rows;
    return 0;
}

static int nf_csv_stringify(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_LIST) {
        cs_error(vm, "csv_stringify() requires a list argument");
        return 1;
    }

    char delimiter = ',';
    char quote = '"';

    // Parse options if provided
    if (argc >= 2 && argv[1].type == CS_T_MAP) {
        cs_value opt_delimiter = cs_map_get(argv[1], "delimiter");
        if (opt_delimiter.type == CS_T_STR) {
            cs_string* delim_str = (cs_string*)opt_delimiter.as.p;
            if (delim_str->len == 1) {
                delimiter = delim_str->data[0];
            }
        }
        cs_value_release(opt_delimiter);

        cs_value opt_quote = cs_map_get(argv[1], "quote");
        if (opt_quote.type == CS_T_STR) {
            cs_string* quote_str = (cs_string*)opt_quote.as.p;
            if (quote_str->len == 1) {
                quote = quote_str->data[0];
            }
        }
        cs_value_release(opt_quote);
    }

    char* buf = NULL;
    size_t len = 0, cap = 0;

    size_t num_rows = cs_list_len(argv[0]);
    for (size_t r = 0; r < num_rows; r++) {
        cs_value row = cs_list_get(argv[0], r);

        // Handle both list of lists and list of maps
        cs_value fields_list = cs_nil();
        int is_map = 0;

        if (row.type == CS_T_LIST) {
            fields_list = row;
        } else if (row.type == CS_T_MAP) {
            // Convert map to list of values
            is_map = 1;
            cs_value keys = cs_map_keys(vm, row);
            if (keys.type != CS_T_LIST) {
                cs_value_release(row);
                free(buf);
                cs_error(vm, "out of memory");
                return 1;
            }

            fields_list = cs_list(vm);
            if (fields_list.type != CS_T_LIST) {
                cs_value_release(row);
                cs_value_release(keys);
                free(buf);
                cs_error(vm, "out of memory");
                return 1;
            }

            // Add values in key order
            size_t num_keys = cs_list_len(keys);
            for (size_t k = 0; k < num_keys; k++) {
                cs_value key = cs_list_get(keys, k);
                if (key.type == CS_T_STR) {
                    cs_string* key_str = (cs_string*)key.as.p;
                    cs_value val = cs_map_get(row, key_str->data);
                    cs_list_push(fields_list, val);
                    cs_value_release(val);
                }
                cs_value_release(key);
            }
            cs_value_release(keys);
        } else {
            cs_value_release(row);
            continue; // Skip non-list/map rows
        }

        size_t num_fields = cs_list_len(fields_list);
        for (size_t f = 0; f < num_fields; f++) {
            if (f > 0) {
                // Add delimiter between fields
                if (!sb_append(&buf, &len, &cap, &delimiter, 1)) {
                    if (is_map) cs_value_release(fields_list);
                    cs_value_release(row);
                    free(buf);
                    cs_error(vm, "out of memory");
                    return 1;
                }
            }

            cs_value field = cs_list_get(fields_list, f);

            // Convert field to string
            const char* field_str = NULL;
            char tmp[128];

            if (field.type == CS_T_STR) {
                cs_string* s = (cs_string*)field.as.p;
                field_str = s->data;
            } else if (field.type == CS_T_INT) {
                snprintf(tmp, sizeof(tmp), "%lld", (long long)field.as.i);
                field_str = tmp;
            } else if (field.type == CS_T_FLOAT) {
                snprintf(tmp, sizeof(tmp), "%g", field.as.f);
                field_str = tmp;
            } else if (field.type == CS_T_BOOL) {
                field_str = field.as.b ? "true" : "false";
            } else if (field.type == CS_T_NIL) {
                field_str = "";
            } else {
                field_str = "";
            }

            // Check if field needs quoting (contains delimiter, quote, or newline)
            int needs_quote = 0;
            for (const char* p = field_str; *p; p++) {
                if (*p == delimiter || *p == quote || *p == '\r' || *p == '\n') {
                    needs_quote = 1;
                    break;
                }
            }

            if (needs_quote) {
                // Add opening quote
                if (!sb_append(&buf, &len, &cap, &quote, 1)) {
                    cs_value_release(field);
                    if (is_map) cs_value_release(fields_list);
                    cs_value_release(row);
                    free(buf);
                    cs_error(vm, "out of memory");
                    return 1;
                }

                // Add field content, escaping quotes
                for (const char* p = field_str; *p; p++) {
                    if (*p == quote) {
                        // Escape quote by doubling it
                        if (!sb_append(&buf, &len, &cap, &quote, 1)) {
                            cs_value_release(field);
                            if (is_map) cs_value_release(fields_list);
                            cs_value_release(row);
                            free(buf);
                            cs_error(vm, "out of memory");
                            return 1;
                        }
                    }
                    if (!sb_append(&buf, &len, &cap, p, 1)) {
                        cs_value_release(field);
                        if (is_map) cs_value_release(fields_list);
                        cs_value_release(row);
                        free(buf);
                        cs_error(vm, "out of memory");
                        return 1;
                    }
                }

                // Add closing quote
                if (!sb_append(&buf, &len, &cap, &quote, 1)) {
                    cs_value_release(field);
                    if (is_map) cs_value_release(fields_list);
                    cs_value_release(row);
                    free(buf);
                    cs_error(vm, "out of memory");
                    return 1;
                }
            } else {
                // No quoting needed - add field as-is
                size_t field_len = strlen(field_str);
                if (!sb_append(&buf, &len, &cap, field_str, field_len)) {
                    cs_value_release(field);
                    if (is_map) cs_value_release(fields_list);
                    cs_value_release(row);
                    free(buf);
                    cs_error(vm, "out of memory");
                    return 1;
                }
            }

            cs_value_release(field);
        }

        if (is_map) cs_value_release(fields_list);
        cs_value_release(row);

        // Add newline after each row
        if (!sb_append(&buf, &len, &cap, "\n", 1)) {
            free(buf);
            cs_error(vm, "out of memory");
            return 1;
        }
    }

    if (!buf) buf = cs_strdup2_local("");
    if (!buf) {
        cs_error(vm, "out of memory");
        return 1;
    }
    *out = cs_str_take(vm, buf, (uint64_t)len);
    return 0;
}

// ============================================================================
// YAML Parser (YAML 1.2.2 specification)
// ============================================================================

#define YAML_MAX_ANCHORS 256
#define YAML_MAX_INDENT_DEPTH 64
#define YAML_MAX_TAG_DIRECTIVES 32

// Tag directive
typedef struct {
    char* handle;
    char* prefix;
} yaml_tag_directive;

typedef struct {
    char* name;
    cs_value value;
} yaml_anchor;

typedef struct {
    const char* s;
    size_t len;
    size_t pos;
    int line;
    int col;
    int indent_stack[YAML_MAX_INDENT_DEPTH];
    int indent_depth;
    cs_vm* vm;
    yaml_anchor anchors[YAML_MAX_ANCHORS];
    size_t anchor_count;
    yaml_tag_directive tag_directives[YAML_MAX_TAG_DIRECTIVES];
    size_t tag_directive_count;
    int yaml_version_major;
    int yaml_version_minor;
} yaml_parser;

static void yaml_init(yaml_parser* p, const char* s, size_t len, cs_vm* vm) {
    p->s = s;
    p->len = len;
    p->pos = 0;
    p->line = 1;
    p->col = 1;
    p->indent_depth = 0;
    p->vm = vm;
    p->anchor_count = 0;
    p->tag_directive_count = 0;
    p->yaml_version_major = 1;
    p->yaml_version_minor = 2;
    for (size_t i = 0; i < YAML_MAX_INDENT_DEPTH; i++) {
        p->indent_stack[i] = -1;
    }
    // Initialize all anchor and tag directive pointers to NULL
    for (size_t i = 0; i < YAML_MAX_ANCHORS; i++) {
        p->anchors[i].name = NULL;
        p->anchors[i].value = cs_nil();
    }
    for (size_t i = 0; i < YAML_MAX_TAG_DIRECTIVES; i++) {
        p->tag_directives[i].handle = NULL;
        p->tag_directives[i].prefix = NULL;
    }
}

static void yaml_cleanup(yaml_parser* p) {
    for (size_t i = 0; i < p->anchor_count; i++) {
        if (p->anchors[i].name) {
            free(p->anchors[i].name);
            p->anchors[i].name = NULL;
        }
        // Only release if value is not already nil (avoid double-release)
        if (p->anchors[i].value.type != CS_T_NIL) {
            cs_value_release(p->anchors[i].value);
            p->anchors[i].value = cs_nil();
        }
    }
    for (size_t i = 0; i < p->tag_directive_count; i++) {
        if (p->tag_directives[i].handle) {
            free(p->tag_directives[i].handle);
            p->tag_directives[i].handle = NULL;
        }
        if (p->tag_directives[i].prefix) {
            free(p->tag_directives[i].prefix);
            p->tag_directives[i].prefix = NULL;
        }
    }
}

static int yaml_peek(yaml_parser* p) {
    if (p->pos >= p->len) return -1;
    return (unsigned char)p->s[p->pos];
}

static int yaml_advance(yaml_parser* p) {
    if (p->pos >= p->len) return -1;
    int ch = (unsigned char)p->s[p->pos++];
    if (ch == '\n') {
        p->line++;
        p->col = 1;
    } else {
        p->col++;
    }
    return ch;
}

static void yaml_skip_ws_inline(yaml_parser* p) {
    while (p->pos < p->len) {
        int ch = yaml_peek(p);
        if (ch == ' ') {
            yaml_advance(p);
        } else {
            break;
        }
    }
}

static void yaml_skip_comment(yaml_parser* p) {
    if (yaml_peek(p) == '#') {
        while (p->pos < p->len && yaml_peek(p) != '\n') {
            yaml_advance(p);
        }
    }
}

static void yaml_skip_ws_and_comments(yaml_parser* p) {
    while (p->pos < p->len) {
        int ch = yaml_peek(p);
        if (ch == ' ' || ch == '\t') {
            yaml_advance(p);
        } else if (ch == '#') {
            yaml_skip_comment(p);
        } else {
            break;
        }
    }
}

static int yaml_get_indent(yaml_parser* p) {
    int indent = 0;
    size_t start_pos = p->pos;
    while (p->pos < p->len) {
        int ch = yaml_peek(p);
        if (ch == ' ') {
            indent++;
            yaml_advance(p);
        } else if (ch == '\t') {
            // Tabs not allowed in indentation per YAML spec
            cs_error(p->vm, "tabs not allowed in YAML indentation");
            p->pos = start_pos;
            return -1;
        } else {
            break;
        }
    }
    p->pos = start_pos; // Reset position
    return indent;
}

__attribute__((unused))
static int yaml_skip_to_indent(yaml_parser* p, int expected_indent) {
    while (p->pos < p->len) {
        int ch = yaml_peek(p);
        if (ch == '\n') {
            yaml_advance(p);
            int indent = yaml_get_indent(p);
            if (indent == expected_indent) {
                // Skip the actual indentation
                for (int i = 0; i < indent && p->pos < p->len; i++) {
                    yaml_advance(p);
                }
                return 1;
            }
        } else {
            break;
        }
    }
    return 0;
}

__attribute__((unused))
static cs_value yaml_parse_null(yaml_parser* p, const char* str, size_t len) {
    if (len == 0) return cs_str(p->vm, "");

    if ((len == 4 && strncmp(str, "null", 4) == 0) ||
        (len == 1 && str[0] == '~')) {
        return cs_nil();
    }

    return cs_nil(); // Will be overridden if not null
}

static cs_value yaml_parse_bool(yaml_parser* p, const char* str, size_t len, int* is_bool) {
    (void)p;
    *is_bool = 0;

    if (len == 4 && strncmp(str, "true", 4) == 0) {
        *is_bool = 1;
        return cs_bool(1);
    }
    if (len == 5 && strncmp(str, "false", 5) == 0) {
        *is_bool = 1;
        return cs_bool(0);
    }
    if (len == 3 && strncmp(str, "yes", 3) == 0) {
        *is_bool = 1;
        return cs_bool(1);
    }
    if (len == 2 && strncmp(str, "no", 2) == 0) {
        *is_bool = 1;
        return cs_bool(0);
    }
    if (len == 2 && strncmp(str, "on", 2) == 0) {
        *is_bool = 1;
        return cs_bool(1);
    }
    if (len == 3 && strncmp(str, "off", 3) == 0) {
        *is_bool = 1;
        return cs_bool(0);
    }

    return cs_nil();
}

static cs_value yaml_parse_number(yaml_parser* p, const char* str, size_t len, int* is_number) {
    (void)p;
    *is_number = 0;

    if (len == 0) return cs_nil();

    // Check for special float values
    if (len == 4 && strncmp(str, ".inf", 4) == 0) {
        *is_number = 1;
        return cs_float(1.0 / 0.0);
    }
    if (len == 5 && strncmp(str, "-.inf", 5) == 0) {
        *is_number = 1;
        return cs_float(-1.0 / 0.0);
    }
    if (len == 4 && strncmp(str, ".nan", 4) == 0) {
        *is_number = 1;
        return cs_float(0.0 / 0.0);
    }

    // Try to parse as number
    char* end = NULL;
    char buf[256];
    if (len >= sizeof(buf)) return cs_nil();
    memcpy(buf, str, len);
    buf[len] = '\0';

    // Check for hex (0x prefix)
    if (len > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        long long val = strtoll(buf, &end, 16);
        if (end && *end == '\0') {
            *is_number = 1;
            return cs_int(val);
        }
    }

    // Check for octal (0o prefix)
    if (len > 2 && buf[0] == '0' && (buf[1] == 'o' || buf[1] == 'O')) {
        long long val = strtoll(buf + 2, &end, 8);
        if (end && *end == '\0') {
            *is_number = 1;
            return cs_int(val);
        }
    }

    // Try integer
    long long ival = strtoll(buf, &end, 10);
    if (end && *end == '\0') {
        *is_number = 1;
        return cs_int(ival);
    }

    // Try float
    double fval = strtod(buf, &end);
    if (end && *end == '\0') {
        *is_number = 1;
        return cs_float(fval);
    }

    return cs_nil();
}

static cs_value yaml_parse_plain_scalar(yaml_parser* p, int indent, int* ok);
static cs_value yaml_parse_value(yaml_parser* p, int indent, int* ok);

// Parse Unicode escape sequence (\uXXXX or \UXXXXXXXX)
// Helper function to encode a Unicode codepoint to UTF-8
static int yaml_encode_utf8(uint32_t codepoint, char** buf, size_t* len, size_t* cap) {
    if (codepoint <= 0x7F) {
        char c = (char)codepoint;
        return sb_append(buf, len, cap, &c, 1);
    } else if (codepoint <= 0x7FF) {
        char c1 = (char)(0xC0 | (codepoint >> 6));
        char c2 = (char)(0x80 | (codepoint & 0x3F));
        return sb_append(buf, len, cap, &c1, 1) && sb_append(buf, len, cap, &c2, 1);
    } else if (codepoint <= 0xFFFF) {
        char c1 = (char)(0xE0 | (codepoint >> 12));
        char c2 = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        char c3 = (char)(0x80 | (codepoint & 0x3F));
        return sb_append(buf, len, cap, &c1, 1) &&
               sb_append(buf, len, cap, &c2, 1) &&
               sb_append(buf, len, cap, &c3, 1);
    } else if (codepoint <= 0x10FFFF) {
        char c1 = (char)(0xF0 | (codepoint >> 18));
        char c2 = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        char c3 = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        char c4 = (char)(0x80 | (codepoint & 0x3F));
        return sb_append(buf, len, cap, &c1, 1) &&
               sb_append(buf, len, cap, &c2, 1) &&
               sb_append(buf, len, cap, &c3, 1) &&
               sb_append(buf, len, cap, &c4, 1);
    }

    return 0;
}

static int yaml_parse_unicode_escape(yaml_parser* p, int num_digits, char** buf, size_t* len, size_t* cap) {
    uint32_t codepoint = 0;
    for (int i = 0; i < num_digits; i++) {
        int ch = yaml_peek(p);
        if (ch == -1) return 0;
        yaml_advance(p);

        if (ch >= '0' && ch <= '9') {
            codepoint = codepoint * 16 + (ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            codepoint = codepoint * 16 + (ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            codepoint = codepoint * 16 + (ch - 'A' + 10);
        } else {
            return 0; // Invalid hex digit
        }
    }

    return yaml_encode_utf8(codepoint, buf, len, cap);
}

// Base64 decoding for !!binary tag
static int base64_char_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -1; // padding
    return -2; // invalid
}

static uint8_t* base64_decode(const char* input, size_t input_len, size_t* output_len) {
    // Remove whitespace and count valid chars
    size_t valid_len = 0;
    for (size_t i = 0; i < input_len; i++) {
        char c = input[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            valid_len++;
        }
    }
    
    // Empty string is valid (0 bytes)
    if (valid_len == 0) {
        *output_len = 0;
        // Return empty bytes - allocate minimum buffer
        uint8_t* empty = (uint8_t*)malloc(1);
        if (!empty) return NULL;
        return empty;
    }
    
    if (valid_len % 4 != 0) {
        return NULL;
    }
    
    size_t max_output_len = (valid_len / 4) * 3;
    uint8_t* output = (uint8_t*)malloc(max_output_len);
    if (!output) return NULL;
    
    size_t out_pos = 0;
    int values[4];
    int val_count = 0;
    
    for (size_t i = 0; i < input_len; i++) {
        char c = input[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue; // skip whitespace
        }
        
        int val = base64_char_value(c);
        if (val == -2) {
            free(output);
            return NULL; // invalid character
        }
        
        values[val_count++] = val;
        
        if (val_count == 4) {
            // Decode 4 base64 chars to 3 bytes
            if (values[0] >= 0 && values[1] >= 0) {
                output[out_pos++] = (values[0] << 2) | (values[1] >> 4);
            }
            if (values[1] >= 0 && values[2] >= 0) {
                output[out_pos++] = (values[1] << 4) | (values[2] >> 2);
            }
            if (values[2] >= 0 && values[3] >= 0) {
                output[out_pos++] = (values[2] << 6) | values[3];
            }
            val_count = 0;
        }
    }
    
    *output_len = out_pos;
    return output;
}

// ISO 8601 timestamp validation for !!timestamp tag
static int is_valid_iso8601_timestamp(const char* s) {
    // Basic validation for common ISO 8601 formats:
    // YYYY-MM-DD
    // YYYY-MM-DDTHH:MM:SS
    // YYYY-MM-DDTHH:MM:SS.sss
    // YYYY-MM-DDTHH:MM:SSZ
    // YYYY-MM-DDTHH:MM:SS+HH:MM
    // YYYY-MM-DD HH:MM:SS
    
    if (!s || strlen(s) < 10) return 0;
    
    // Check year-month-day format (YYYY-MM-DD)
    if (!(s[0] >= '0' && s[0] <= '9')) return 0;
    if (!(s[1] >= '0' && s[1] <= '9')) return 0;
    if (!(s[2] >= '0' && s[2] <= '9')) return 0;
    if (!(s[3] >= '0' && s[3] <= '9')) return 0;
    if (s[4] != '-') return 0;
    if (!(s[5] >= '0' && s[5] <= '9')) return 0;
    if (!(s[6] >= '0' && s[6] <= '9')) return 0;
    if (s[7] != '-') return 0;
    if (!(s[8] >= '0' && s[8] <= '9')) return 0;
    if (!(s[9] >= '0' && s[9] <= '9')) return 0;
    
    // If just date, that's valid
    if (s[10] == '\0') return 1;
    
    // Check for time separator (T or space)
    if (s[10] != 'T' && s[10] != 't' && s[10] != ' ') return 0;
    
    // Check time format (HH:MM:SS)
    if (strlen(s) < 19) return 0;
    if (!(s[11] >= '0' && s[11] <= '9')) return 0;
    if (!(s[12] >= '0' && s[12] <= '9')) return 0;
    if (s[13] != ':') return 0;
    if (!(s[14] >= '0' && s[14] <= '9')) return 0;
    if (!(s[15] >= '0' && s[15] <= '9')) return 0;
    if (s[16] != ':') return 0;
    if (!(s[17] >= '0' && s[17] <= '9')) return 0;
    if (!(s[18] >= '0' && s[18] <= '9')) return 0;
    
    // End of string is valid
    if (s[19] == '\0') return 1;
    
    // Check for fractional seconds (.sss...)
    if (s[19] == '.') {
        size_t i = 20;
        while (s[i] >= '0' && s[i] <= '9') i++;
        if (s[i] == '\0' || s[i] == 'Z' || s[i] == 'z' || s[i] == '+' || s[i] == '-') {
            // Valid fractional seconds
            if (s[i] == '\0' || s[i] == 'Z' || s[i] == 'z') return 1;
            // Continue to check timezone
        } else {
            return 0;
        }
    }
    
    // Check for timezone (Z or ±HH:MM)
    size_t pos = 19;
    while (s[pos] && s[pos] != 'Z' && s[pos] != 'z' && s[pos] != '+' && s[pos] != '-') {
        pos++;
    }
    
    if (s[pos] == 'Z' || s[pos] == 'z') {
        return s[pos + 1] == '\0';
    }
    
    if (s[pos] == '+' || s[pos] == '-') {
        // Check ±HH:MM format
        if (strlen(s + pos) < 6) return 0;
        if (!(s[pos + 1] >= '0' && s[pos + 1] <= '9')) return 0;
        if (!(s[pos + 2] >= '0' && s[pos + 2] <= '9')) return 0;
        if (s[pos + 3] != ':') return 0;
        if (!(s[pos + 4] >= '0' && s[pos + 4] <= '9')) return 0;
        if (!(s[pos + 5] >= '0' && s[pos + 5] <= '9')) return 0;
        return s[pos + 6] == '\0';
    }
    
    return 1;
}

// Apply tag to value
static cs_value yaml_apply_tag(yaml_parser* p, const char* tag, cs_value val, int* ok) {
    if (!tag) return val;
    
    // Standard YAML 1.2 tags
    if (strcmp(tag, "!!null") == 0 || strcmp(tag, "tag:yaml.org,2002:null") == 0) {
        cs_value_release(val);
        return cs_nil();
    }
    if (strcmp(tag, "!!bool") == 0 || strcmp(tag, "tag:yaml.org,2002:bool") == 0) {
        if (val.type == CS_T_STR) {
            cs_string* s = (cs_string*)val.as.p;
            cs_value result;
            if (strcmp(s->data, "true") == 0 || strcmp(s->data, "yes") == 0 || strcmp(s->data, "on") == 0) {
                result = cs_bool(1);
            } else if (strcmp(s->data, "false") == 0 || strcmp(s->data, "no") == 0 || strcmp(s->data, "off") == 0) {
                result = cs_bool(0);
            } else {
                cs_error(p->vm, "invalid boolean value for !!bool tag");
                *ok = 0;
                cs_value_release(val);
                return cs_nil();
            }
            cs_value_release(val);
            return result;
        }
        return val;
    }
    if (strcmp(tag, "!!int") == 0 || strcmp(tag, "tag:yaml.org,2002:int") == 0) {
        if (val.type == CS_T_STR) {
            cs_string* s = (cs_string*)val.as.p;
            char* end = NULL;
            long long ival = strtoll(s->data, &end, 0);
            if (end && *end == '\0') {
                cs_value_release(val);
                return cs_int(ival);
            }
            cs_error(p->vm, "invalid integer value for !!int tag");
            *ok = 0;
            cs_value_release(val);
            return cs_nil();
        }
        return val;
    }
    if (strcmp(tag, "!!float") == 0 || strcmp(tag, "tag:yaml.org,2002:float") == 0) {
        if (val.type == CS_T_STR) {
            cs_string* s = (cs_string*)val.as.p;
            char* end = NULL;
            double fval = strtod(s->data, &end);
            if (end && *end == '\0') {
                cs_value_release(val);
                return cs_float(fval);
            }
            cs_error(p->vm, "invalid float value for !!float tag");
            *ok = 0;
            cs_value_release(val);
            return cs_nil();
        }
        return val;
    }
    if (strcmp(tag, "!!str") == 0 || strcmp(tag, "tag:yaml.org,2002:str") == 0) {
        // Force to string
        if (val.type != CS_T_STR) {
            // Convert to string representation
            char tmp[64];
            switch (val.type) {
                case CS_T_NIL:
                    cs_value_release(val);
                    return cs_str(p->vm, "null");
                case CS_T_BOOL:
                    cs_value_release(val);
                    return cs_str(p->vm, val.as.b ? "true" : "false");
                case CS_T_INT:
                    snprintf(tmp, sizeof(tmp), "%lld", (long long)val.as.i);
                    cs_value_release(val);
                    return cs_str(p->vm, tmp);
                case CS_T_FLOAT:
                    snprintf(tmp, sizeof(tmp), "%g", val.as.f);
                    cs_value_release(val);
                    return cs_str(p->vm, tmp);
                default:
                    break;
            }
        }
        return val;
    }
    
    // !!binary - Base64-encoded binary data
    if (strcmp(tag, "!!binary") == 0 || strcmp(tag, "tag:yaml.org,2002:binary") == 0) {
        if (val.type == CS_T_STR) {
            cs_string* s = (cs_string*)val.as.p;
            size_t decoded_len;
            uint8_t* decoded = base64_decode(s->data, s->len, &decoded_len);
            if (decoded) {
                cs_value result = cs_bytes_take(p->vm, decoded, decoded_len);
                cs_value_release(val);
                return result;
            }
        }
        cs_error(p->vm, "!!binary requires valid base64-encoded string");
        *ok = 0;
        cs_value_release(val);
        return cs_nil();
    }
    
    // !!timestamp - ISO 8601 timestamp
    if (strcmp(tag, "!!timestamp") == 0 || strcmp(tag, "tag:yaml.org,2002:timestamp") == 0) {
        if (val.type == CS_T_STR) {
            cs_string* s = (cs_string*)val.as.p;
            if (is_valid_iso8601_timestamp(s->data)) {
                return val; // Return validated timestamp string
            }
        }
        cs_error(p->vm, "!!timestamp requires valid ISO 8601 format (e.g., '2024-01-27T15:30:00Z')");
        *ok = 0;
        cs_value_release(val);
        return cs_nil();
    }
    
    // !!set - Unordered set (map with null values)
    if (strcmp(tag, "!!set") == 0 || strcmp(tag, "tag:yaml.org,2002:set") == 0) {
        if (val.type == CS_T_MAP) {
            // Verify all values are null
            cs_map_obj* m = (cs_map_obj*)val.as.p;
            for (size_t i = 0; i < m->cap; i++) {
                if (m->entries[i].in_use && m->entries[i].val.type != CS_T_NIL) {
                    cs_error(p->vm, "!!set requires all map values to be null");
                    *ok = 0;
                    cs_value_release(val);
                    return cs_nil();
                }
            }
            return val; // Valid set (map with null values)
        } else if (val.type == CS_T_LIST) {
            // Convert list to map with null values
            cs_value set = cs_map(p->vm);
            cs_list_obj* list = (cs_list_obj*)val.as.p;
            for (size_t i = 0; i < list->len; i++) {
                if (list->items[i].type == CS_T_STR) {
                    cs_string* key = (cs_string*)list->items[i].as.p;
                    cs_map_set(set, key->data, cs_nil());
                } else {
                    // For non-string keys, convert to string
                    char tmp[64];
                    const char* key_str = NULL;
                    if (list->items[i].type == CS_T_INT) {
                        snprintf(tmp, sizeof(tmp), "%lld", (long long)list->items[i].as.i);
                        key_str = tmp;
                    } else if (list->items[i].type == CS_T_FLOAT) {
                        snprintf(tmp, sizeof(tmp), "%g", list->items[i].as.f);
                        key_str = tmp;
                    } else if (list->items[i].type == CS_T_BOOL) {
                        key_str = list->items[i].as.b ? "true" : "false";
                    }
                    if (key_str) {
                        cs_map_set(set, key_str, cs_nil());
                    }
                }
            }
            cs_value_release(val);
            return set;
        }
        cs_error(p->vm, "!!set requires map or list");
        *ok = 0;
        cs_value_release(val);
        return cs_nil();
    }
    
    // !!omap - Ordered map (list of single-entry maps)
    if (strcmp(tag, "!!omap") == 0 || strcmp(tag, "tag:yaml.org,2002:omap") == 0) {
        if (val.type == CS_T_LIST) {
            // Verify it's a list of single-entry maps
            cs_list_obj* list = (cs_list_obj*)val.as.p;
            for (size_t i = 0; i < list->len; i++) {
                if (list->items[i].type != CS_T_MAP) {
                    cs_error(p->vm, "!!omap requires list of maps");
                    *ok = 0;
                    cs_value_release(val);
                    return cs_nil();
                }
                cs_map_obj* entry = (cs_map_obj*)list->items[i].as.p;
                if (entry->len != 1) {
                    cs_error(p->vm, "!!omap entries must have exactly one key-value pair");
                    *ok = 0;
                    cs_value_release(val);
                    return cs_nil();
                }
            }
            // Valid omap - return as list
            return val;
        }
        cs_error(p->vm, "!!omap requires list");
        *ok = 0;
        cs_value_release(val);
        return cs_nil();
    }
    
    // !!pairs - Ordered pairs (list of single-entry maps, duplicates allowed)
    if (strcmp(tag, "!!pairs") == 0 || strcmp(tag, "tag:yaml.org,2002:pairs") == 0) {
        if (val.type == CS_T_LIST) {
            // Similar to !!omap but allows duplicate keys
            cs_list_obj* list = (cs_list_obj*)val.as.p;
            for (size_t i = 0; i < list->len; i++) {
                if (list->items[i].type != CS_T_MAP) {
                    cs_error(p->vm, "!!pairs requires list of maps");
                    *ok = 0;
                    cs_value_release(val);
                    return cs_nil();
                }
                cs_map_obj* entry = (cs_map_obj*)list->items[i].as.p;
                if (entry->len != 1) {
                    cs_error(p->vm, "!!pairs entries must have exactly one key-value pair");
                    *ok = 0;
                    cs_value_release(val);
                    return cs_nil();
                }
            }
            // Valid pairs - return as list
            return val;
        }
        cs_error(p->vm, "!!pairs requires list");
        *ok = 0;
        cs_value_release(val);
        return cs_nil();
    }
    
    // For unknown tags, just return the value as-is
    // A full implementation would allow custom tag handlers
    return val;
}

static cs_value yaml_parse_quoted_string(yaml_parser* p, char quote_char, int* ok) {
    *ok = 1;
    yaml_advance(p); // Skip opening quote

    char* buf = NULL;
    size_t len = 0, cap = 0;

    while (p->pos < p->len) {
        int ch = yaml_peek(p);

        if (ch == quote_char) {
            yaml_advance(p);
            // Check for escaped quote (double quote)
            if (quote_char == '"' && yaml_peek(p) == '"') {
                yaml_advance(p);
                if (!sb_append(&buf, &len, &cap, "\"", 1)) {
                    free(buf);
                    cs_error(p->vm, "out of memory");
                    *ok = 0;
                    return cs_nil();
                }
                continue;
            }
            // End of string
            if (!buf) buf = cs_strdup2_local("");
            if (!buf) {
                cs_error(p->vm, "out of memory");
                *ok = 0;
                return cs_nil();
            }
            return cs_str_take(p->vm, buf, (uint64_t)len);
        }

        yaml_advance(p);

        if (ch == '\\' && quote_char == '"') {
            // Handle escape sequences in double-quoted strings
            int next = yaml_peek(p);
            if (next == -1) break;
            yaml_advance(p);

            char esc;
            int is_simple_esc = 1;
            switch (next) {
                case '0': esc = '\0'; break;
                case 'a': esc = '\a'; break;
                case 'b': esc = '\b'; break;
                case 't': esc = '\t'; break;
                case 'n': esc = '\n'; break;
                case 'v': esc = '\v'; break;
                case 'f': esc = '\f'; break;
                case 'r': esc = '\r'; break;
                case 'e': esc = '\x1B'; break;
                case ' ': esc = ' '; break;
                case '"': esc = '"'; break;
                case '/': esc = '/'; break;
                case '\\': esc = '\\'; break;
                case 'x':
                    // \xHH - 2-digit hex escape (YAML 1.2.2)
                    is_simple_esc = 0;
                    if (!yaml_parse_unicode_escape(p, 2, &buf, &len, &cap)) {
                        free(buf);
                        cs_error(p->vm, "invalid \\xHH escape sequence");
                        *ok = 0;
                        return cs_nil();
                    }
                    break;
                case 'u':
                    is_simple_esc = 0;
                    if (!yaml_parse_unicode_escape(p, 4, &buf, &len, &cap)) {
                        free(buf);
                        cs_error(p->vm, "invalid \\uXXXX escape sequence");
                        *ok = 0;
                        return cs_nil();
                    }
                    break;
                case 'U':
                    is_simple_esc = 0;
                    if (!yaml_parse_unicode_escape(p, 8, &buf, &len, &cap)) {
                        free(buf);
                        cs_error(p->vm, "invalid \\UXXXXXXXX escape sequence");
                        *ok = 0;
                        return cs_nil();
                    }
                    break;
                case 'N':
                    // Next line (NEL) - U+0085 (YAML 1.2.2)
                    is_simple_esc = 0;
                    if (!yaml_encode_utf8(0x0085, &buf, &len, &cap)) {
                        free(buf);
                        cs_error(p->vm, "out of memory");
                        *ok = 0;
                        return cs_nil();
                    }
                    break;
                case '_':
                    // Non-breaking space - U+00A0 (YAML 1.2.2)
                    is_simple_esc = 0;
                    if (!yaml_encode_utf8(0x00A0, &buf, &len, &cap)) {
                        free(buf);
                        cs_error(p->vm, "out of memory");
                        *ok = 0;
                        return cs_nil();
                    }
                    break;
                case 'L':
                    // Line separator - U+2028 (YAML 1.2.2)
                    is_simple_esc = 0;
                    if (!yaml_encode_utf8(0x2028, &buf, &len, &cap)) {
                        free(buf);
                        cs_error(p->vm, "out of memory");
                        *ok = 0;
                        return cs_nil();
                    }
                    break;
                case 'P':
                    // Paragraph separator - U+2029 (YAML 1.2.2)
                    is_simple_esc = 0;
                    if (!yaml_encode_utf8(0x2029, &buf, &len, &cap)) {
                        free(buf);
                        cs_error(p->vm, "out of memory");
                        *ok = 0;
                        return cs_nil();
                    }
                    break;
                default:
                    esc = (char)next;
                    break;
            }

            if (is_simple_esc && !sb_append(&buf, &len, &cap, &esc, 1)) {
                free(buf);
                cs_error(p->vm, "out of memory");
                *ok = 0;
                return cs_nil();
            }
        } else {
            char c = (char)ch;
            if (!sb_append(&buf, &len, &cap, &c, 1)) {
                free(buf);
                cs_error(p->vm, "out of memory");
                *ok = 0;
                return cs_nil();
            }
        }
    }

    free(buf);
    cs_error(p->vm, "unterminated quoted string");
    *ok = 0;
    return cs_nil();
}

static cs_value yaml_parse_multiline(yaml_parser* p, char style, int base_indent, int* ok) {
    *ok = 1;
    yaml_advance(p); // Skip | or >

    // Parse optional chomping indicator and/or indentation indicator
    // Format: | or |- or |+ or |2 or |2- or |-2 etc.
    char chomp = 'c'; // 'c' = clip (default), '-' = strip, '+' = keep
    int explicit_indent = -1;
    
    // Read up to 2 indicators (chomp and indent, in either order)
    for (int i = 0; i < 2; i++) {
        int ch = yaml_peek(p);
        if (ch == '+' || ch == '-') {
            chomp = (char)ch;
            yaml_advance(p);
        } else if (ch >= '1' && ch <= '9') {
            explicit_indent = ch - '0';
            yaml_advance(p);
        } else {
            break;
        }
    }

    yaml_skip_ws_inline(p);

    // Skip to next line
    if (yaml_peek(p) != '\n') {
        yaml_skip_comment(p);
    }
    if (yaml_peek(p) == '\n') {
        yaml_advance(p);
    }

    // Determine content indent
    int content_indent = explicit_indent >= 0 ? (base_indent + explicit_indent) : -1;
    
    if (content_indent < 0) {
        // Auto-detect from first non-empty line
        size_t scan_pos = p->pos;
        while (scan_pos < p->len) {
            int indent = 0;
            while (scan_pos < p->len && p->s[scan_pos] == ' ') {
                indent++;
                scan_pos++;
            }
            if (scan_pos < p->len && p->s[scan_pos] != '\n') {
                content_indent = indent;
                break;
            }
            if (scan_pos < p->len && p->s[scan_pos] == '\n') {
                scan_pos++;
            }
        }
    }

    if (content_indent <= base_indent) {
        content_indent = base_indent + 2;
    }

    char* buf = NULL;
    size_t len = 0, cap = 0;

    int first_line = 1;
    int trailing_newlines = 0;
    
    while (p->pos < p->len) {
        // Check indent
        int indent = 0;
        while (p->pos < p->len && yaml_peek(p) == ' ') {
            indent++;
            yaml_advance(p);
        }

        // End of multiline block if indent is less than content indent
        if (p->pos < p->len && yaml_peek(p) != '\n' && indent < content_indent) {
            break;
        }

        // Empty line
        if (yaml_peek(p) == '\n') {
            if (style == '|') {
                trailing_newlines++;
            } else {
                trailing_newlines++;
            }
            yaml_advance(p);
            continue;
        }

        // Flush any pending newlines (these already include line breaks)
        int had_trailing = 0;
        if (trailing_newlines > 0) {
            had_trailing = 1;
            for (int i = 0; i < trailing_newlines; i++) {
                if (!sb_append(&buf, &len, &cap, "\n", 1)) {
                    free(buf);
                    cs_error(p->vm, "out of memory");
                    *ok = 0;
                    return cs_nil();
                }
            }
            trailing_newlines = 0;
        }

        // Add line separator for previous line (only if we didn't just flush trailing newlines)
        if (!first_line && !had_trailing) {
            if (style == '|') {
                if (!sb_append(&buf, &len, &cap, "\n", 1)) {
                    free(buf);
                    cs_error(p->vm, "out of memory");
                    *ok = 0;
                    return cs_nil();
                }
            } else { // style == '>'
                if (!sb_append(&buf, &len, &cap, " ", 1)) {
                    free(buf);
                    cs_error(p->vm, "out of memory");
                    *ok = 0;
                    return cs_nil();
                }
            }
        }
        first_line = 0;

        // Read line content
        while (p->pos < p->len && yaml_peek(p) != '\n') {
            char c = (char)yaml_advance(p);
            if (!sb_append(&buf, &len, &cap, &c, 1)) {
                free(buf);
                cs_error(p->vm, "out of memory");
                *ok = 0;
                return cs_nil();
            }
        }

        if (yaml_peek(p) == '\n') {
            yaml_advance(p);
            trailing_newlines = 1;
        }
    }

    // Apply chomping indicator
    if (chomp == 'c') {
        // Clip: single newline at end
        if (!sb_append(&buf, &len, &cap, "\n", 1)) {
            free(buf);
            cs_error(p->vm, "out of memory");
            *ok = 0;
            return cs_nil();
        }
    } else if (chomp == '+') {
        // Keep: preserve all trailing newlines
        for (int i = 0; i < trailing_newlines; i++) {
            if (!sb_append(&buf, &len, &cap, "\n", 1)) {
                free(buf);
                cs_error(p->vm, "out of memory");
                *ok = 0;
                return cs_nil();
            }
        }
    }
    // else chomp == '-': strip, don't add any trailing newlines

    if (!buf) buf = cs_strdup2_local("");
    if (!buf) {
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }
    return cs_str_take(p->vm, buf, (uint64_t)len);
}

static cs_value yaml_parse_flow_list(yaml_parser* p, int* ok) {
    *ok = 1;
    yaml_advance(p); // Skip [

    cs_value list = cs_list(p->vm);
    if (list.type != CS_T_LIST) {
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }

    yaml_skip_ws_and_comments(p);

    if (yaml_peek(p) == ']') {
        yaml_advance(p);
        return list;
    }

    while (p->pos < p->len) {
        yaml_skip_ws_and_comments(p);

        if (yaml_peek(p) == ']') {
            yaml_advance(p);
            return list;
        }

        cs_value item = yaml_parse_value(p, -1, ok);
        if (!*ok) {
            cs_value_release(list);
            return cs_nil();
        }

        cs_list_push(list, item);
        cs_value_release(item);

        yaml_skip_ws_and_comments(p);

        if (yaml_peek(p) == ',') {
            yaml_advance(p);
        } else if (yaml_peek(p) == ']') {
            yaml_advance(p);
            return list;
        }
    }

    cs_value_release(list);
    cs_error(p->vm, "unterminated flow list");
    *ok = 0;
    return cs_nil();
}

static cs_value yaml_parse_flow_map(yaml_parser* p, int* ok) {
    *ok = 1;
    yaml_advance(p); // Skip {

    cs_value map = cs_map(p->vm);
    if (map.type != CS_T_MAP) {
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }

    yaml_skip_ws_and_comments(p);

    if (yaml_peek(p) == '}') {
        yaml_advance(p);
        return map;
    }

    while (p->pos < p->len) {
        yaml_skip_ws_and_comments(p);

        if (yaml_peek(p) == '}') {
            yaml_advance(p);
            return map;
        }

        // Parse key
        cs_value key = yaml_parse_value(p, -1, ok);
        if (!*ok) {
            cs_value_release(map);
            return cs_nil();
        }

        yaml_skip_ws_and_comments(p);

        if (yaml_peek(p) != ':') {
            cs_value_release(key);
            cs_value_release(map);
            cs_error(p->vm, "expected ':' in flow map");
            *ok = 0;
            return cs_nil();
        }
        yaml_advance(p);

        yaml_skip_ws_and_comments(p);

        // Parse value
        cs_value val = yaml_parse_value(p, -1, ok);
        if (!*ok) {
            cs_value_release(key);
            cs_value_release(map);
            return cs_nil();
        }

        // Set in map
        if (key.type == CS_T_STR) {
            cs_string* key_str = (cs_string*)key.as.p;
            cs_map_set(map, key_str->data, val);
        }

        cs_value_release(key);
        cs_value_release(val);

        yaml_skip_ws_and_comments(p);

        if (yaml_peek(p) == ',') {
            yaml_advance(p);
        } else if (yaml_peek(p) == '}') {
            yaml_advance(p);
            return map;
        }
    }

    cs_value_release(map);
    cs_error(p->vm, "unterminated flow map");
    *ok = 0;
    return cs_nil();
}

static cs_value yaml_parse_plain_scalar(yaml_parser* p, int indent, int* ok) {
    *ok = 1;

    size_t start = p->pos;
    char* buf = NULL;
    size_t len = 0, cap = 0;

    // YAML 1.2.2: Reserved indicators @ and ` cannot start plain scalars
    int first_ch = yaml_peek(p);
    if (first_ch == '@' || first_ch == '`') {
        cs_error(p->vm, "reserved indicators @ and ` cannot start plain scalars");
        *ok = 0;
        return cs_nil();
    }

    while (p->pos < p->len) {
        int ch = yaml_peek(p);

        // End conditions
        if (ch == '\n' || ch == ':' || ch == '#' || ch == ',' || ch == ']' || ch == '}' || ch == -1) {
            // Special handling for ':'
            if (ch == ':') {
                // In flow context (indent < 0), ':' is ALWAYS a delimiter
                if (indent < 0) {
                    break;
                }
                // In block context, ':' must be followed by space or EOL
                size_t save_pos = p->pos;
                yaml_advance(p);
                int next = yaml_peek(p);
                p->pos = save_pos;
                if (next == ' ' || next == '\n' || next == '\t' || next == -1) {
                    break;
                }
            } else {
                break;
            }
        }

        char c = (char)yaml_advance(p);
        if (!sb_append(&buf, &len, &cap, &c, 1)) {
            free(buf);
            cs_error(p->vm, "out of memory");
            *ok = 0;
            return cs_nil();
        }
    }

    // Trim trailing whitespace
    while (len > 0 && (buf[len-1] == ' ' || buf[len-1] == '\t')) {
        len--;
    }

    if (!buf || len == 0) {
        free(buf);
        return cs_str(p->vm, "");
    }

    // Check if this is a special value
    if ((len == 4 && strncmp(buf, "null", 4) == 0) ||
        (len == 1 && buf[0] == '~')) {
        free(buf);
        return cs_nil();
    }

    // Check for boolean
    int is_bool = 0;
    cs_value bool_val = yaml_parse_bool(p, buf, len, &is_bool);
    if (is_bool) {
        free(buf);
        return bool_val;
    }

    // Check for number
    int is_number = 0;
    cs_value num_val = yaml_parse_number(p, buf, len, &is_number);
    if (is_number) {
        free(buf);
        return num_val;
    }

    // Return as string
    buf[len] = '\0';
    return cs_str_take(p->vm, buf, (uint64_t)len);
}

static cs_value yaml_parse_alias(yaml_parser* p, int* ok) {
    *ok = 1;
    yaml_advance(p); // Skip *

    size_t start = p->pos;
    while (p->pos < p->len) {
        int ch = yaml_peek(p);
        if (ch == ' ' || ch == '\n' || ch == '\t' || ch == ',' || ch == ']' || ch == '}' || ch == ':' || ch == '#') {
            break;
        }
        yaml_advance(p);
    }

    size_t name_len = p->pos - start;
    if (name_len == 0) {
        cs_error(p->vm, "empty alias name");
        *ok = 0;
        return cs_nil();
    }

    // Look up anchor
    for (size_t i = 0; i < p->anchor_count; i++) {
        if (strlen(p->anchors[i].name) == name_len &&
            strncmp(p->anchors[i].name, p->s + start, name_len) == 0) {
            return cs_value_copy(p->anchors[i].value);
        }
    }

    cs_error(p->vm, "undefined alias");
    *ok = 0;
    return cs_nil();
}

static cs_value yaml_parse_list(yaml_parser* p, int base_indent, int* ok) {
    *ok = 1;

    cs_value list = cs_list(p->vm);
    if (list.type != CS_T_LIST) {
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }

    while (p->pos < p->len) {
        // Check current indent
        int indent = yaml_get_indent(p);
        if (indent < base_indent) {
            break;
        }
        if (indent > base_indent) {
            // Skip over-indented lines
            yaml_advance(p);
            continue;
        }

        // Skip indent
        for (int i = 0; i < indent && p->pos < p->len; i++) {
            yaml_advance(p);
        }

        // Check for list marker
        if (yaml_peek(p) != '-') {
            break;
        }

        // Look ahead to ensure it's followed by space or newline
        size_t save_pos = p->pos;
        yaml_advance(p);
        int next = yaml_peek(p);
        if (next != ' ' && next != '\n' && next != '\t' && next != -1) {
            p->pos = save_pos;
            break;
        }

        yaml_skip_ws_inline(p);
        yaml_skip_comment(p);

        // If end of line, item is on next line(s)
        if (yaml_peek(p) == '\n') {
            yaml_advance(p);
            cs_value item = yaml_parse_value(p, indent + 2, ok);
            if (!*ok) {
                cs_value_release(list);
                return cs_nil();
            }
            cs_list_push(list, item);
            cs_value_release(item);
        } else {
            // Item is inline - parse at indent 0 but ensure we only parse current line
            // The skip to end of line below ensures we don't consume multiple list items
            cs_value item = yaml_parse_value(p, 0, ok);
            if (!*ok) {
                cs_value_release(list);
                return cs_nil();
            }
            cs_list_push(list, item);
            cs_value_release(item);

            // Skip to end of line
            yaml_skip_ws_inline(p);
            yaml_skip_comment(p);
            if (yaml_peek(p) == '\n') {
                yaml_advance(p);
            }
        }
    }

    return list;
}

static cs_value yaml_parse_map(yaml_parser* p, int base_indent, int* ok) {
    *ok = 1;

    cs_value map = cs_map(p->vm);
    if (map.type != CS_T_MAP) {
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }

    while (p->pos < p->len) {
        // Check current indent
        int indent = yaml_get_indent(p);
        
        // In block context, check indentation
        if (base_indent >= 0) {
            if (indent < base_indent) {
                break;
            }
            if (indent > base_indent) {
                break;
            }
        }

        // Skip indent
        for (int i = 0; i < indent && p->pos < p->len; i++) {
            yaml_advance(p);
        }

        yaml_skip_ws_inline(p);

        // Empty line or comment
        if (yaml_peek(p) == '\n' || yaml_peek(p) == '#') {
            if (yaml_peek(p) == '#') {
                yaml_skip_comment(p);
            }
            if (yaml_peek(p) == '\n') {
                // In base_indent=0 context (inline list items), stop at newline
                if (base_indent == 0) {
                    break;
                }
                yaml_advance(p);
            }
            continue;
        }

        // Check for list marker (not a map) or document markers
        if (yaml_peek(p) == '-') {
            size_t save = p->pos;
            yaml_advance(p);
            int next = yaml_peek(p);
            p->pos = save;
            if (next == ' ' || next == '\n' || next == '\t') {
                break;
            }
            // Check for document marker ---
            if (next == '-' && p->pos + 2 < p->len && p->s[p->pos + 2] == '-') {
                // Check if followed by whitespace or EOF
                if (p->pos + 3 >= p->len || 
                    p->s[p->pos + 3] == ' ' || p->s[p->pos + 3] == '\t' || p->s[p->pos + 3] == '\n') {
                    break;
                }
            }
        }
        
        // Check for document end marker ...
        if (yaml_peek(p) == '.') {
            if (p->pos + 2 < p->len && p->s[p->pos + 1] == '.' && p->s[p->pos + 2] == '.') {
                // Check if followed by whitespace or EOF
                if (p->pos + 3 >= p->len || 
                    p->s[p->pos + 3] == ' ' || p->s[p->pos + 3] == '\t' || p->s[p->pos + 3] == '\n') {
                    break;
                }
            }
        }

        // Check for explicit key indicator '?'
        cs_value key;
        if (yaml_peek(p) == '?') {
            int explicit_key = 1;
            yaml_advance(p);
            yaml_skip_ws_inline(p);
            
            // Parse complex key (can be any value type)
            if (yaml_peek(p) == '\n') {
                yaml_advance(p);
                key = yaml_parse_value(p, base_indent + 2, ok);
            } else {
                key = yaml_parse_value(p, base_indent, ok);
            }
            
            if (!*ok) {
                cs_value_release(map);
                return cs_nil();
            }
            
            // Skip to value indicator ':'
            yaml_skip_ws_inline(p);
            if (yaml_peek(p) == '\n') {
                yaml_advance(p);
                // Value on next line
                int val_indent = yaml_get_indent(p);
                // Skip indent
                for (int i = 0; i < val_indent && p->pos < p->len; i++) {
                    yaml_advance(p);
                }
                yaml_skip_ws_inline(p);
                // Now check for ':'
                if (yaml_peek(p) != ':') {
                    cs_error(p->vm, "expected ':' for explicit key");
                    cs_value_release(key);
                    cs_value_release(map);
                    *ok = 0;
                    return cs_nil();
                }
            }
        } else {
            // Regular key parsing
            key = yaml_parse_plain_scalar(p, indent, ok);
            if (!*ok) {
                cs_value_release(map);
                return cs_nil();
            }
        }

        yaml_skip_ws_inline(p);

        // Expect colon
        if (yaml_peek(p) != ':') {
            cs_value_release(key);
            // Don't release map - just break and return the partial map
            // (no colon means this isn't a map entry, stop parsing)
            break;
        }
        yaml_advance(p);

        yaml_skip_ws_inline(p);
        yaml_skip_comment(p);

        // Check for merge operator '<<'
        int is_merge = 0;
        if (key.type == CS_T_STR) {
            cs_string* key_str = (cs_string*)key.as.p;
            if (strcmp(key_str->data, "<<") == 0) {
                is_merge = 1;
            }
        }

        // Parse value
        cs_value val;
        if (yaml_peek(p) == '\n') {
            yaml_advance(p);
            val = yaml_parse_value(p, base_indent + 2, ok);
        } else {
            val = yaml_parse_value(p, base_indent, ok);
        }

        if (!*ok) {
            cs_value_release(key);
            cs_value_release(map);
            return cs_nil();
        }

        // Handle merge operator
        if (is_merge) {
            // Merge the map(s) into current map
            if (val.type == CS_T_MAP) {
                // Merge single map - get all keys and iterate
                cs_value src_keys = cs_map_keys(p->vm, val);
                if (src_keys.type == CS_T_LIST) {
                    size_t num_keys = cs_list_len(src_keys);
                    for (size_t i = 0; i < num_keys; i++) {
                        cs_value src_key = cs_list_get(src_keys, i);
                        if (src_key.type == CS_T_STR) {
                            cs_string* key_str = (cs_string*)src_key.as.p;
                            cs_value existing = cs_map_get(map, key_str->data);
                            // Only merge if key doesn't already exist
                            if (existing.type == CS_T_NIL) {
                                cs_value src_val = cs_map_get(val, key_str->data);
                                cs_map_set(map, key_str->data, src_val);
                                cs_value_release(src_val);
                            }
                            cs_value_release(existing);
                        }
                        cs_value_release(src_key);
                    }
                }
                cs_value_release(src_keys);
            } else if (val.type == CS_T_LIST) {
                // Merge list of maps
                size_t list_len = cs_list_len(val);
                for (size_t i = 0; i < list_len; i++) {
                    cs_value item = cs_list_get(val, i);
                    if (item.type == CS_T_MAP) {
                        // Get all keys from this map and iterate
                        cs_value src_keys = cs_map_keys(p->vm, item);
                        if (src_keys.type == CS_T_LIST) {
                            size_t num_keys = cs_list_len(src_keys);
                            for (size_t j = 0; j < num_keys; j++) {
                                cs_value src_key = cs_list_get(src_keys, j);
                                if (src_key.type == CS_T_STR) {
                                    cs_string* key_str = (cs_string*)src_key.as.p;
                                    cs_value existing = cs_map_get(map, key_str->data);
                                    if (existing.type == CS_T_NIL) {
                                        cs_value src_val = cs_map_get(item, key_str->data);
                                        cs_map_set(map, key_str->data, src_val);
                                        cs_value_release(src_val);
                                    }
                                    cs_value_release(existing);
                                }
                                cs_value_release(src_key);
                            }
                        }
                        cs_value_release(src_keys);
                    }
                    cs_value_release(item);
                }
            }
            cs_value_release(key);
            cs_value_release(val);
        } else {
            // Set in map (convert non-string keys to strings for map compatibility)
            if (key.type == CS_T_STR) {
                cs_string* key_str = (cs_string*)key.as.p;
                cs_map_set(map, key_str->data, val);
            } else {
                // Convert key to string representation
                char tmp[256];
                switch (key.type) {
                    case CS_T_INT:
                        snprintf(tmp, sizeof(tmp), "%lld", (long long)key.as.i);
                        cs_map_set(map, tmp, val);
                        break;
                    case CS_T_FLOAT:
                        snprintf(tmp, sizeof(tmp), "%g", key.as.f);
                        cs_map_set(map, tmp, val);
                        break;
                    case CS_T_BOOL:
                        cs_map_set(map, key.as.b ? "true" : "false", val);
                        break;
                    default:
                        // For complex keys (lists, maps), just skip
                        break;
                }
            }

            cs_value_release(key);
            cs_value_release(val);
        }

        // Skip to end of line
        yaml_skip_ws_inline(p);
        yaml_skip_comment(p);
        if (yaml_peek(p) == '\n') {
            yaml_advance(p);
        }
    }

    return map;
}

static cs_value yaml_parse_value(yaml_parser* p, int indent, int* ok) {
    *ok = 1;

    // Only skip inline whitespace in flow context (indent < 0)
    // In block context, indentation is meaningful and handled by structure parsers
    if (indent < 0) {
        yaml_skip_ws_inline(p);
    }

    int ch = yaml_peek(p);

    if (ch == -1) {
        return cs_nil();
    }

    // Tag (!!type or !tag)
    char* tag = NULL;
    if (ch == '!') {
        yaml_advance(p);
        size_t start = p->pos;
        
        // Check for !! (standard tag)
        if (yaml_peek(p) == '!') {
            yaml_advance(p);
            start = p->pos;
            // Read tag name
            while (p->pos < p->len) {
                int c = yaml_peek(p);
                if (c == ' ' || c == '\n' || c == '\t' || c == ':' || c == ',' || c == '[' || c == ']' || c == '{' || c == '}') {
                    break;
                }
                yaml_advance(p);
            }
            size_t tag_len = p->pos - start;
            if (tag_len > 0) {
                tag = (char*)malloc(tag_len + 3);
                if (tag) {
                    tag[0] = '!';
                    tag[1] = '!';
                    memcpy(tag + 2, p->s + start, tag_len);
                    tag[tag_len + 2] = '\0';
                }
            }
        } else {
            // Local tag or tag handle
            while (p->pos < p->len) {
                int c = yaml_peek(p);
                if (c == ' ' || c == '\n' || c == '\t' || c == ':' || c == ',' || c == '[' || c == ']' || c == '{' || c == '}') {
                    break;
                }
                yaml_advance(p);
            }
            size_t tag_len = p->pos - start;
            if (tag_len > 0) {
                tag = (char*)malloc(tag_len + 2);
                if (tag) {
                    tag[0] = '!';
                    memcpy(tag + 1, p->s + start, tag_len);
                    tag[tag_len + 1] = '\0';
                }
            }
        }

        yaml_skip_ws_inline(p);
        ch = yaml_peek(p);

        // If value is on next line after tag, handle it recursively
        if (ch == '\n' && indent >= 0) {
            yaml_advance(p);  // Skip newline
            // Get the indentation level but don't skip it - let the parser handle it
            int actual_indent = yaml_get_indent(p);
            // Parse value at the actual indentation level
            // Structured parsers (list/map) will handle skipping their own indentation
            cs_value result = yaml_parse_value(p, actual_indent, ok);
            if (*ok && tag) {
                result = yaml_apply_tag(p, tag, result, ok);
            }
            free(tag);
            return result;
        }
    }

    // Flow list
    if (ch == '[') {
        cs_value result = yaml_parse_flow_list(p, ok);
        if (*ok && tag) {
            result = yaml_apply_tag(p, tag, result, ok);
        }
        free(tag);
        return result;
    }

    // Flow map
    if (ch == '{') {
        cs_value result = yaml_parse_flow_map(p, ok);
        if (*ok && tag) {
            result = yaml_apply_tag(p, tag, result, ok);
        }
        free(tag);
        return result;
    }

    // Alias
    if (ch == '*') {
        free(tag);
        return yaml_parse_alias(p, ok);
    }

    // Anchor (store it but parse the value)
    char* anchor_name = NULL;
    if (ch == '&') {
        yaml_advance(p);
        size_t start = p->pos;
        while (p->pos < p->len) {
            int c = yaml_peek(p);
            if (c == ' ' || c == '\n' || c == '\t' || c == ':' || c == ',') {
                break;
            }
            yaml_advance(p);
        }
        size_t name_len = p->pos - start;
        if (name_len > 0) {
            anchor_name = (char*)malloc(name_len + 1);
            if (anchor_name) {
                memcpy(anchor_name, p->s + start, name_len);
                anchor_name[name_len] = '\0';
            }
        }
        yaml_skip_ws_inline(p);
        ch = yaml_peek(p);

        // Check for tag after anchor
        if (ch == '!' && !tag) {
            yaml_advance(p);
            size_t start = p->pos;

            // Check for !! (standard tag)
            if (yaml_peek(p) == '!') {
                yaml_advance(p);
                start = p->pos;
                // Read tag name
                while (p->pos < p->len) {
                    int c = yaml_peek(p);
                    if (c == ' ' || c == '\n' || c == '\t' || c == ':' || c == ',' || c == '[' || c == ']' || c == '{' || c == '}') {
                        break;
                    }
                    yaml_advance(p);
                }
                size_t tag_len = p->pos - start;
                if (tag_len > 0) {
                    tag = (char*)malloc(tag_len + 3);
                    if (tag) {
                        tag[0] = '!';
                        tag[1] = '!';
                        memcpy(tag + 2, p->s + start, tag_len);
                        tag[tag_len + 2] = '\0';
                    }
                }
            } else {
                // Local tag or tag handle
                while (p->pos < p->len) {
                    int c = yaml_peek(p);
                    if (c == ' ' || c == '\n' || c == '\t' || c == ':' || c == ',' || c == '[' || c == ']' || c == '{' || c == '}') {
                        break;
                    }
                    yaml_advance(p);
                }
                size_t tag_len = p->pos - start;
                if (tag_len > 0) {
                    tag = (char*)malloc(tag_len + 2);
                    if (tag) {
                        tag[0] = '!';
                        memcpy(tag + 1, p->s + start, tag_len);
                        tag[tag_len + 1] = '\0';
                    }
                }
            }

            yaml_skip_ws_inline(p);
            ch = yaml_peek(p);
        }

        // If value is on next line after anchor, handle it recursively
        if (ch == '\n' && indent >= 0) {
            yaml_advance(p);  // Skip newline
            cs_value result = yaml_parse_value(p, indent + 2, ok);
            if (*ok && tag) {
                result = yaml_apply_tag(p, tag, result, ok);
            }
            if (*ok && anchor_name && p->anchor_count < YAML_MAX_ANCHORS) {
                p->anchors[p->anchor_count].name = anchor_name;
                p->anchors[p->anchor_count].value = cs_value_copy(result);
                p->anchor_count++;
            } else {
                free(anchor_name);
            }
            free(tag);
            return result;
        }
    }

    // Quoted string
    if (ch == '"' || ch == '\'') {
        cs_value result = yaml_parse_quoted_string(p, (char)ch, ok);
        if (*ok && tag) {
            result = yaml_apply_tag(p, tag, result, ok);
        }
        free(tag);
        if (anchor_name && *ok && p->anchor_count < YAML_MAX_ANCHORS) {
            p->anchors[p->anchor_count].name = anchor_name;
            p->anchors[p->anchor_count].value = cs_value_copy(result);
            p->anchor_count++;
        } else {
            free(anchor_name);
        }
        return result;
    }

    // Multiline literal
    if (ch == '|' || ch == '>') {
        cs_value result = yaml_parse_multiline(p, (char)ch, indent, ok);
        if (*ok && tag) {
            result = yaml_apply_tag(p, tag, result, ok);
        }
        free(tag);
        if (anchor_name && *ok && p->anchor_count < YAML_MAX_ANCHORS) {
            p->anchors[p->anchor_count].name = anchor_name;
            p->anchors[p->anchor_count].value = cs_value_copy(result);
            p->anchor_count++;
        } else {
            free(anchor_name);
        }
        return result;
    }

    // List marker - check if we're at indentation followed by '-'
    // This handles cases where yaml_parse_value is called with parser at indentation
    if (indent >= 0) {
        size_t save = p->pos;
        int line_indent = yaml_get_indent(p);
        // Peek first non-space character on this line
        for (int i = 0; i < line_indent && p->pos < p->len; i++) {
            yaml_advance(p);
        }
        int first_char = yaml_peek(p);
        // Check what follows the '-'
        int next_char = -1;
        if (first_char == '-') {
            yaml_advance(p);
            next_char = yaml_peek(p);
        }
        p->pos = save;

        if (first_char == '-' && line_indent == indent && 
            (next_char == ' ' || next_char == '\n' || next_char == '\t')) {
            // This is a list at the expected indentation
            cs_value result = yaml_parse_list(p, line_indent, ok);
            if (*ok && tag) {
                result = yaml_apply_tag(p, tag, result, ok);
            }
            free(tag);
            if (anchor_name && *ok && p->anchor_count < YAML_MAX_ANCHORS) {
                p->anchors[p->anchor_count].name = anchor_name;
                p->anchors[p->anchor_count].value = cs_value_copy(result);
                p->anchor_count++;
            } else {
                free(anchor_name);
            }
            return result;
        }
    }

    // List marker (when already positioned at '-')
    if (ch == '-') {
        size_t save = p->pos;
        yaml_advance(p);
        int next = yaml_peek(p);
        p->pos = save;
        if (next == ' ' || next == '\n' || next == '\t') {
            cs_value result = yaml_parse_list(p, indent, ok);
            if (*ok && tag) {
                result = yaml_apply_tag(p, tag, result, ok);
            }
            free(tag);
            if (anchor_name && *ok && p->anchor_count < YAML_MAX_ANCHORS) {
                p->anchors[p->anchor_count].name = anchor_name;
                p->anchors[p->anchor_count].value = cs_value_copy(result);
                p->anchor_count++;
            } else {
                free(anchor_name);
            }
            return result;
        }
    }

    // Try to parse as map or plain scalar
    size_t save_pos = p->pos;

    // Check for explicit key indicator ? at start
    if (yaml_peek(p) == '?') {
        int next = p->pos + 1 < p->len ? p->s[p->pos + 1] : -1;
        if (next == ' ' || next == '\n' || next == '\t' || next == -1) {
            // Explicit key indicator - this is a map
            cs_value result = yaml_parse_map(p, indent, ok);
            if (*ok && tag) {
                result = yaml_apply_tag(p, tag, result, ok);
            }
            free(tag);
            if (anchor_name && *ok && p->anchor_count < YAML_MAX_ANCHORS) {
                p->anchors[p->anchor_count].name = anchor_name;
                p->anchors[p->anchor_count].value = cs_value_copy(result);
                p->anchor_count++;
            } else {
                free(anchor_name);
            }
            return result;
        }
    }

    // Look ahead for key: value pattern (only in block context)
    int looks_like_map = 0;
    if (indent >= 0) {  // Only do map lookahead in block context
        while (p->pos < p->len) {
            int c = yaml_peek(p);
            if (c == ':') {
                yaml_advance(p);
                int next = yaml_peek(p);
                if (next == ' ' || next == '\n' || next == '\t' || next == -1) {
                    looks_like_map = 1;
                }
                break;
            }
            // Break on flow collection separators and terminators
            if (c == '\n' || c == '[' || c == '{' || c == '-' || c == ',' || c == ']' || c == '}') {
                break;
            }
            yaml_advance(p);
        }
        p->pos = save_pos;
    }

    if (looks_like_map) {
        cs_value result = yaml_parse_map(p, indent, ok);
        if (*ok && tag) {
            result = yaml_apply_tag(p, tag, result, ok);
        }
        free(tag);
        if (anchor_name && *ok && p->anchor_count < YAML_MAX_ANCHORS) {
            p->anchors[p->anchor_count].name = anchor_name;
            p->anchors[p->anchor_count].value = cs_value_copy(result);
            p->anchor_count++;
        } else {
            free(anchor_name);
        }
        return result;
    }

    // Plain scalar
    cs_value result = yaml_parse_plain_scalar(p, indent, ok);
    if (*ok && tag) {
        result = yaml_apply_tag(p, tag, result, ok);
    }
    free(tag);
    if (anchor_name && *ok && p->anchor_count < YAML_MAX_ANCHORS) {
        p->anchors[p->anchor_count].name = anchor_name;
        p->anchors[p->anchor_count].value = cs_value_copy(result);
        p->anchor_count++;
    } else {
        free(anchor_name);
    }
    return result;
}

static int nf_yaml_parse(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_STR) {
        cs_error(vm, "yaml_parse() requires a string argument");
        return 1;
    }

    cs_string* str_obj = (cs_string*)argv[0].as.p;
    const char* text = str_obj->data;
    size_t text_len = str_obj->len;

    yaml_parser p;
    yaml_init(&p, text, text_len, vm);

    // Parse directives
    while (p.pos < p.len) {
        // Skip whitespace and comments
        while (p.pos < p.len) {
            int ch = yaml_peek(&p);
            if (ch == ' ' || ch == '\t') {
                yaml_advance(&p);
            } else if (ch == '#') {
                yaml_skip_comment(&p);
                if (yaml_peek(&p) == '\n') {
                    yaml_advance(&p);
                }
            } else if (ch == '\n') {
                yaml_advance(&p);
            } else {
                break;
            }
        }
        
        // Check for %YAML directive
        if (p.pos + 5 < p.len && strncmp(p.s + p.pos, "%YAML", 5) == 0) {
            p.pos += 5;
            yaml_skip_ws_inline(&p);
            
            // Parse version (e.g., "1.2")
            if (p.pos < p.len && yaml_peek(&p) >= '0' && yaml_peek(&p) <= '9') {
                p.yaml_version_major = yaml_peek(&p) - '0';
                yaml_advance(&p);
                if (yaml_peek(&p) == '.') {
                    yaml_advance(&p);
                    if (p.pos < p.len && yaml_peek(&p) >= '0' && yaml_peek(&p) <= '9') {
                        p.yaml_version_minor = yaml_peek(&p) - '0';
                        yaml_advance(&p);
                    }
                }
            }
            
            // Skip to end of line
            while (p.pos < p.len && yaml_peek(&p) != '\n') {
                yaml_advance(&p);
            }
            if (yaml_peek(&p) == '\n') {
                yaml_advance(&p);
            }
            continue;
        }
        
        // Check for %TAG directive
        if (p.pos + 4 < p.len && strncmp(p.s + p.pos, "%TAG", 4) == 0) {
            p.pos += 4;
            yaml_skip_ws_inline(&p);
            
            // Parse handle
            size_t handle_start = p.pos;
            while (p.pos < p.len && yaml_peek(&p) != ' ' && yaml_peek(&p) != '\t') {
                yaml_advance(&p);
            }
            size_t handle_len = p.pos - handle_start;
            
            yaml_skip_ws_inline(&p);
            
            // Parse prefix
            size_t prefix_start = p.pos;
            while (p.pos < p.len && yaml_peek(&p) != ' ' && yaml_peek(&p) != '\t' && yaml_peek(&p) != '\n') {
                yaml_advance(&p);
            }
            size_t prefix_len = p.pos - prefix_start;
            
            // Store tag directive
            if (handle_len > 0 && prefix_len > 0 && p.tag_directive_count < YAML_MAX_TAG_DIRECTIVES) {
                p.tag_directives[p.tag_directive_count].handle = (char*)malloc(handle_len + 1);
                p.tag_directives[p.tag_directive_count].prefix = (char*)malloc(prefix_len + 1);
                if (p.tag_directives[p.tag_directive_count].handle && p.tag_directives[p.tag_directive_count].prefix) {
                    memcpy(p.tag_directives[p.tag_directive_count].handle, p.s + handle_start, handle_len);
                    p.tag_directives[p.tag_directive_count].handle[handle_len] = '\0';
                    memcpy(p.tag_directives[p.tag_directive_count].prefix, p.s + prefix_start, prefix_len);
                    p.tag_directives[p.tag_directive_count].prefix[prefix_len] = '\0';
                    p.tag_directive_count++;
                }
            }
            
            // Skip to end of line
            while (p.pos < p.len && yaml_peek(&p) != '\n') {
                yaml_advance(&p);
            }
            if (yaml_peek(&p) == '\n') {
                yaml_advance(&p);
            }
            continue;
        }
        
        break;
    }

    // Skip document marker if present
    if (p.pos + 3 <= p.len && strncmp(p.s + p.pos, "---", 3) == 0) {
        p.pos += 3;
        if (yaml_peek(&p) == '\n') {
            yaml_advance(&p);
        }
    }

    // Skip leading whitespace and comments
    while (p.pos < p.len) {
        int ch = yaml_peek(&p);
        if (ch == ' ' || ch == '\t') {
            yaml_advance(&p);
        } else if (ch == '#') {
            yaml_skip_comment(&p);
            if (yaml_peek(&p) == '\n') {
                yaml_advance(&p);
            }
        } else if (ch == '\n') {
            yaml_advance(&p);
        } else {
            break;
        }
    }

    if (p.pos >= p.len) {
        yaml_cleanup(&p);
        *out = cs_nil();
        return 0;
    }

    int ok = 1;
    cs_value result = yaml_parse_value(&p, 0, &ok);

    yaml_cleanup(&p);

    if (!ok) {
        return 1;
    }

    *out = result;
    return 0;
}

// Helper function to stringify a YAML value (forward declaration)
static int yaml_stringify_value(cs_vm* vm, cs_value val, char** buf, size_t* len, size_t* cap, int depth, int indent);

// Helper function to stringify a YAML value
static int yaml_stringify_value(cs_vm* vm, cs_value val, char** buf, size_t* len, size_t* cap, int depth, int indent) {
    switch (val.type) {
        case CS_T_NIL:
            if (!sb_append(buf, len, cap, "null", 4)) {
                cs_error(vm, "out of memory");
                return 0;
            }
            break;

        case CS_T_BOOL: {
            const char* str = val.as.b ? "true" : "false";
            if (!sb_append(buf, len, cap, str, strlen(str))) {
                cs_error(vm, "out of memory");
                return 0;
            }
            break;
        }

        case CS_T_INT: {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%lld", (long long)val.as.i);
            if (!sb_append(buf, len, cap, tmp, strlen(tmp))) {
                cs_error(vm, "out of memory");
                return 0;
            }
            break;
        }

        case CS_T_FLOAT: {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%g", val.as.f);
            if (!sb_append(buf, len, cap, tmp, strlen(tmp))) {
                cs_error(vm, "out of memory");
                return 0;
            }
            break;
        }

        case CS_T_STR: {
            cs_string* s = (cs_string*)val.as.p;
            const char* str = s->data;

            // Check if string needs quoting (contains special chars)
            int needs_quote = 0;
            int has_newline = 0;
            for (size_t i = 0; i < s->len; i++) {
                char ch = str[i];
                if (ch == '\n') {
                    has_newline = 1;
                    needs_quote = 1;
                    break;
                }
                if (ch == ':' || ch == '#' || ch == '[' || ch == ']' ||
                    ch == '{' || ch == '}' || ch == ',' || ch == '&' ||
                    ch == '*' || ch == '!' || ch == '|' || ch == '>' ||
                    ch == '\'' || ch == '"' || ch == '%' || ch == '@' || ch == '`') {
                    needs_quote = 1;
                }
            }

            // Check if string looks like a number or boolean
            if (!needs_quote && s->len > 0) {
                if (strcmp(str, "true") == 0 || strcmp(str, "false") == 0 ||
                    strcmp(str, "null") == 0 || strcmp(str, "yes") == 0 ||
                    strcmp(str, "no") == 0 || strcmp(str, "on") == 0 ||
                    strcmp(str, "off") == 0) {
                    needs_quote = 1;
                }
            }

            // Use multiline literal style for strings with newlines
            if (has_newline) {
                if (!sb_append(buf, len, cap, "|\n", 2)) {
                    cs_error(vm, "out of memory");
                    return 0;
                }
                // Add indented content
                for (size_t i = 0; i < s->len; i++) {
                    if (i == 0 || str[i-1] == '\n') {
                        for (int j = 0; j < (depth + 1) * indent; j++) {
                            if (!sb_append(buf, len, cap, " ", 1)) {
                                cs_error(vm, "out of memory");
                                return 0;
                            }
                        }
                    }
                    if (!sb_append(buf, len, cap, &str[i], 1)) {
                        cs_error(vm, "out of memory");
                        return 0;
                    }
                }
            } else if (needs_quote) {
                // Use double quotes
                if (!sb_append(buf, len, cap, "\"", 1)) {
                    cs_error(vm, "out of memory");
                    return 0;
                }
                for (size_t i = 0; i < s->len; i++) {
                    char ch = str[i];
                    if (ch == '"' || ch == '\\') {
                        if (!sb_append(buf, len, cap, "\\", 1)) {
                            cs_error(vm, "out of memory");
                            return 0;
                        }
                    }
                    if (!sb_append(buf, len, cap, &ch, 1)) {
                        cs_error(vm, "out of memory");
                        return 0;
                    }
                }
                if (!sb_append(buf, len, cap, "\"", 1)) {
                    cs_error(vm, "out of memory");
                    return 0;
                }
            } else {
                // Plain string
                if (!sb_append(buf, len, cap, str, s->len)) {
                    cs_error(vm, "out of memory");
                    return 0;
                }
            }
            break;
        }

        case CS_T_LIST: {
            size_t list_len = cs_list_len(val);
            if (list_len == 0) {
                if (!sb_append(buf, len, cap, "[]", 2)) {
                    cs_error(vm, "out of memory");
                    return 0;
                }
            } else {
                // Block style list
                for (size_t i = 0; i < list_len; i++) {
                    if (i > 0 || depth > 0) {
                        if (!sb_append(buf, len, cap, "\n", 1)) {
                            cs_error(vm, "out of memory");
                            return 0;
                        }
                        for (int j = 0; j < depth * indent; j++) {
                            if (!sb_append(buf, len, cap, " ", 1)) {
                                cs_error(vm, "out of memory");
                                return 0;
                            }
                        }
                    }
                    if (!sb_append(buf, len, cap, "- ", 2)) {
                        cs_error(vm, "out of memory");
                        return 0;
                    }

                    cs_value item = cs_list_get(val, i);
                    if (item.type == CS_T_MAP || item.type == CS_T_LIST) {
                        if (!yaml_stringify_value(vm, item, buf, len, cap, depth + 1, indent)) {
                            cs_value_release(item);
                            return 0;
                        }
                    } else {
                        if (!yaml_stringify_value(vm, item, buf, len, cap, depth, indent)) {
                            cs_value_release(item);
                            return 0;
                        }
                    }
                    cs_value_release(item);
                }
            }
            break;
        }

        case CS_T_MAP: {
            cs_value keys = cs_map_keys(vm, val);
            if (keys.type != CS_T_LIST) {
                cs_error(vm, "out of memory");
                return 0;
            }

            size_t num_keys = cs_list_len(keys);
            if (num_keys == 0) {
                if (!sb_append(buf, len, cap, "{}", 2)) {
                    cs_value_release(keys);
                    cs_error(vm, "out of memory");
                    return 0;
                }
            } else {
                // Block style map
                for (size_t i = 0; i < num_keys; i++) {
                    if (i > 0 || depth > 0) {
                        if (!sb_append(buf, len, cap, "\n", 1)) {
                            cs_value_release(keys);
                            cs_error(vm, "out of memory");
                            return 0;
                        }
                        for (int j = 0; j < depth * indent; j++) {
                            if (!sb_append(buf, len, cap, " ", 1)) {
                                cs_value_release(keys);
                                cs_error(vm, "out of memory");
                                return 0;
                            }
                        }
                    }

                    cs_value key = cs_list_get(keys, i);
                    if (key.type == CS_T_STR) {
                        cs_string* key_str = (cs_string*)key.as.p;
                        if (!sb_append(buf, len, cap, key_str->data, key_str->len)) {
                            cs_value_release(key);
                            cs_value_release(keys);
                            cs_error(vm, "out of memory");
                            return 0;
                        }

                        cs_value map_val = cs_map_get(val, key_str->data);

                        // Add colon with or without space depending on value type
                        if (map_val.type == CS_T_MAP || map_val.type == CS_T_LIST) {
                            if (!sb_append(buf, len, cap, ":", 1)) {
                                cs_value_release(map_val);
                                cs_value_release(key);
                                cs_value_release(keys);
                                cs_error(vm, "out of memory");
                                return 0;
                            }
                            if (!yaml_stringify_value(vm, map_val, buf, len, cap, depth + 1, indent)) {
                                cs_value_release(map_val);
                                cs_value_release(key);
                                cs_value_release(keys);
                                return 0;
                            }
                        } else {
                            if (!sb_append(buf, len, cap, ": ", 2)) {
                                cs_value_release(map_val);
                                cs_value_release(key);
                                cs_value_release(keys);
                                cs_error(vm, "out of memory");
                                return 0;
                            }
                            if (!yaml_stringify_value(vm, map_val, buf, len, cap, depth, indent)) {
                                cs_value_release(map_val);
                                cs_value_release(key);
                                cs_value_release(keys);
                                return 0;
                            }
                        }
                        cs_value_release(map_val);
                    }
                    cs_value_release(key);
                }
            }
            cs_value_release(keys);
            break;
        }

        default:
            // For other types, use a simple representation
            if (!sb_append(buf, len, cap, "null", 4)) {
                cs_error(vm, "out of memory");
                return 0;
            }
            break;
    }

    return 1;
}

// Parse multiple YAML documents from a stream
static int nf_yaml_parse_all(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_STR) {
        cs_error(vm, "yaml_parse_all() requires a string argument");
        return 1;
    }

    cs_string* str_obj = (cs_string*)argv[0].as.p;
    const char* text = str_obj->data;
    size_t text_len = str_obj->len;

    // Create list to hold all documents
    cs_value docs = cs_list(vm);
    if (docs.type != CS_T_LIST) {
        cs_error(vm, "out of memory");
        return 1;
    }

    yaml_parser p;
    yaml_init(&p, text, text_len, vm);

    while (p.pos < p.len) {
        // Parse directives for this document
        while (p.pos < p.len) {
            // Skip whitespace and comments
            while (p.pos < p.len) {
                int ch = yaml_peek(&p);
                if (ch == ' ' || ch == '\t') {
                    yaml_advance(&p);
                } else if (ch == '#') {
                    yaml_skip_comment(&p);
                    if (yaml_peek(&p) == '\n') {
                        yaml_advance(&p);
                    }
                } else if (ch == '\n') {
                    yaml_advance(&p);
                } else {
                    break;
                }
            }
            
            // Check for %YAML or %TAG directives
            if (p.pos + 5 < p.len && strncmp(p.s + p.pos, "%YAML", 5) == 0) {
                p.pos += 5;
                yaml_skip_ws_inline(&p);
                if (p.pos < p.len && yaml_peek(&p) >= '0' && yaml_peek(&p) <= '9') {
                    p.yaml_version_major = yaml_peek(&p) - '0';
                    yaml_advance(&p);
                    if (yaml_peek(&p) == '.') {
                        yaml_advance(&p);
                        if (p.pos < p.len && yaml_peek(&p) >= '0' && yaml_peek(&p) <= '9') {
                            p.yaml_version_minor = yaml_peek(&p) - '0';
                            yaml_advance(&p);
                        }
                    }
                }
                while (p.pos < p.len && yaml_peek(&p) != '\n') {
                    yaml_advance(&p);
                }
                if (yaml_peek(&p) == '\n') {
                    yaml_advance(&p);
                }
                continue;
            }
            
            if (p.pos + 4 < p.len && strncmp(p.s + p.pos, "%TAG", 4) == 0) {
                p.pos += 4;
                yaml_skip_ws_inline(&p);
                size_t handle_start = p.pos;
                while (p.pos < p.len && yaml_peek(&p) != ' ' && yaml_peek(&p) != '\t') {
                    yaml_advance(&p);
                }
                size_t handle_len = p.pos - handle_start;
                yaml_skip_ws_inline(&p);
                size_t prefix_start = p.pos;
                while (p.pos < p.len && yaml_peek(&p) != ' ' && yaml_peek(&p) != '\t' && yaml_peek(&p) != '\n') {
                    yaml_advance(&p);
                }
                size_t prefix_len = p.pos - prefix_start;
                if (handle_len > 0 && prefix_len > 0 && p.tag_directive_count < YAML_MAX_TAG_DIRECTIVES) {
                    p.tag_directives[p.tag_directive_count].handle = (char*)malloc(handle_len + 1);
                    p.tag_directives[p.tag_directive_count].prefix = (char*)malloc(prefix_len + 1);
                    if (p.tag_directives[p.tag_directive_count].handle && p.tag_directives[p.tag_directive_count].prefix) {
                        memcpy(p.tag_directives[p.tag_directive_count].handle, p.s + handle_start, handle_len);
                        p.tag_directives[p.tag_directive_count].handle[handle_len] = '\0';
                        memcpy(p.tag_directives[p.tag_directive_count].prefix, p.s + prefix_start, prefix_len);
                        p.tag_directives[p.tag_directive_count].prefix[prefix_len] = '\0';
                        p.tag_directive_count++;
                    }
                }
                while (p.pos < p.len && yaml_peek(&p) != '\n') {
                    yaml_advance(&p);
                }
                if (yaml_peek(&p) == '\n') {
                    yaml_advance(&p);
                }
                continue;
            }
            
            break;
        }

        // Skip document start marker ---
        if (p.pos + 3 <= p.len && strncmp(p.s + p.pos, "---", 3) == 0) {
            p.pos += 3;
            // Ensure it's followed by whitespace or newline
            int ch = yaml_peek(&p);
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == -1) {
                if (ch == '\n') {
                    yaml_advance(&p);
                }
            } else {
                // Not a document marker, reset
                p.pos -= 3;
            }
        }

        // Skip whitespace and comments
        while (p.pos < p.len) {
            int ch = yaml_peek(&p);
            if (ch == ' ' || ch == '\t') {
                yaml_advance(&p);
            } else if (ch == '#') {
                yaml_skip_comment(&p);
                if (yaml_peek(&p) == '\n') {
                    yaml_advance(&p);
                }
            } else if (ch == '\n') {
                yaml_advance(&p);
            } else {
                break;
            }
        }

        if (p.pos >= p.len) {
            break;
        }

        // Check for document end marker ...
        if (p.pos + 3 <= p.len && strncmp(p.s + p.pos, "...", 3) == 0) {
            p.pos += 3;
            // Skip to next document
            while (p.pos < p.len && yaml_peek(&p) != '\n') {
                yaml_advance(&p);
            }
            if (yaml_peek(&p) == '\n') {
                yaml_advance(&p);
            }
            continue;
        }

        // Parse document
        int ok = 1;
        cs_value doc = yaml_parse_value(&p, 0, &ok);
        
        if (!ok) {
            cs_value_release(docs);
            yaml_cleanup(&p);
            return 1;
        }

        cs_list_push(docs, doc);
        cs_value_release(doc);

        // Clean up anchors from this document (they don't persist across documents)
        for (size_t i = 0; i < p.anchor_count; i++) {
            if (p.anchors[i].name) {
                free(p.anchors[i].name);
                p.anchors[i].name = NULL;
            }
            // Only release if value is not already nil (avoid double-release)
            if (p.anchors[i].value.type != CS_T_NIL) {
                cs_value_release(p.anchors[i].value);
                p.anchors[i].value = cs_nil();
            }
        }
        p.anchor_count = 0;
        // Note: tag_directives are NOT freed here - they're freed once at the end in yaml_cleanup()

        // Skip to document end or next document start
        while (p.pos < p.len) {
            // Skip whitespace
            while (p.pos < p.len) {
                int ch = yaml_peek(&p);
                if (ch == ' ' || ch == '\t' || ch == '\n') {
                    yaml_advance(&p);
                } else if (ch == '#') {
                    yaml_skip_comment(&p);
                } else {
                    break;
                }
            }

            // Check for document end ...
            if (p.pos + 3 <= p.len && strncmp(p.s + p.pos, "...", 3) == 0) {
                p.pos += 3;
                while (p.pos < p.len && yaml_peek(&p) != '\n') {
                    yaml_advance(&p);
                }
                if (yaml_peek(&p) == '\n') {
                    yaml_advance(&p);
                }
                break;
            }

            // Check for next document start ---
            if (p.pos + 3 <= p.len && strncmp(p.s + p.pos, "---", 3) == 0) {
                // Next document
                break;
            }

            // Otherwise continue parsing (single document)
            break;
        }
    }

    yaml_cleanup(&p);
    *out = docs;
    return 0;
}

static int nf_yaml_stringify(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1) {
        cs_error(vm, "yaml_stringify() requires a value argument");
        return 1;
    }

    int indent = 2;
    if (argc >= 2 && argv[1].type == CS_T_INT) {
        indent = (int)argv[1].as.i;
        if (indent < 0) indent = 0;
        if (indent > 8) indent = 8;
    }

    char* buf = NULL;
    size_t len = 0, cap = 0;

    if (!yaml_stringify_value(vm, argv[0], &buf, &len, &cap, 0, indent)) {
        free(buf);
        return 1;
    }

    if (!buf) buf = cs_strdup2_local("");
    if (!buf) {
        cs_error(vm, "out of memory");
        return 1;
    }
    *out = cs_str_take(vm, buf, (uint64_t)len);
    return 0;
}

// ============================================================================
// XML Parser (XML 1.0 specification)
// ============================================================================

typedef struct {
    const char* s;
    size_t len;
    size_t pos;
    cs_vm* vm;
} xml_parser;

static void xml_init(xml_parser* p, const char* s, size_t len, cs_vm* vm) {
    p->s = s;
    p->len = len;
    p->pos = 0;
    p->vm = vm;
}

static int xml_peek(xml_parser* p) {
    if (p->pos >= p->len) return -1;
    return (unsigned char)p->s[p->pos];
}

static int xml_advance(xml_parser* p) {
    if (p->pos >= p->len) return -1;
    return (unsigned char)p->s[p->pos++];
}

static void xml_skip_ws(xml_parser* p) {
    while (p->pos < p->len) {
        int ch = xml_peek(p);
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            xml_advance(p);
        } else {
            break;
        }
    }
}

static int xml_match(xml_parser* p, const char* lit) {
    size_t lit_len = strlen(lit);
    if (p->pos + lit_len > p->len) return 0;
    if (strncmp(p->s + p->pos, lit, lit_len) == 0) {
        p->pos += lit_len;
        return 1;
    }
    return 0;
}

static int xml_is_name_start(int ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' || ch == ':';
}

static int xml_is_name_char(int ch) {
    return xml_is_name_start(ch) || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.';
}

static cs_value xml_parse_name(xml_parser* p, int* ok) {
    *ok = 1;
    size_t start = p->pos;

    int first = xml_peek(p);
    if (!xml_is_name_start(first)) {
        cs_error(p->vm, "invalid XML name");
        *ok = 0;
        return cs_nil();
    }
    xml_advance(p);

    while (p->pos < p->len && xml_is_name_char(xml_peek(p))) {
        xml_advance(p);
    }

    size_t name_len = p->pos - start;
    char* name = (char*)malloc(name_len + 1);
    if (!name) {
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }
    memcpy(name, p->s + start, name_len);
    name[name_len] = '\0';

    return cs_str_take(p->vm, name, (uint64_t)name_len);
}

static cs_value xml_decode_entity(xml_parser* p, int* ok) {
    *ok = 1;

    if (xml_match(p, "&lt;")) return cs_str(p->vm, "<");
    if (xml_match(p, "&gt;")) return cs_str(p->vm, ">");
    if (xml_match(p, "&amp;")) return cs_str(p->vm, "&");
    if (xml_match(p, "&quot;")) return cs_str(p->vm, "\"");
    if (xml_match(p, "&apos;")) return cs_str(p->vm, "'");

    // Numeric entity: &#65; or &#x41;
    if (xml_match(p, "&#")) {
        int is_hex = 0;
        if (xml_peek(p) == 'x' || xml_peek(p) == 'X') {
            is_hex = 1;
            xml_advance(p);
        }

        int value = 0;
        int has_digits = 0;
        while (p->pos < p->len) {
            int ch = xml_peek(p);
            if (ch == ';') {
                xml_advance(p);
                if (has_digits && value >= 0 && value <= 0x10FFFF) {
                    char utf8[5] = {0};
                    if (value < 0x80) {
                        utf8[0] = (char)value;
                    } else {
                        utf8[0] = (char)value; // Simplified - just use the value
                    }
                    return cs_str(p->vm, utf8);
                }
                cs_error(p->vm, "invalid numeric entity");
                *ok = 0;
                return cs_nil();
            }

            int digit = -1;
            if (ch >= '0' && ch <= '9') digit = ch - '0';
            else if (is_hex && ch >= 'a' && ch <= 'f') digit = 10 + (ch - 'a');
            else if (is_hex && ch >= 'A' && ch <= 'F') digit = 10 + (ch - 'A');
            else break;

            value = value * (is_hex ? 16 : 10) + digit;
            has_digits = 1;
            xml_advance(p);
        }
    }

    cs_error(p->vm, "unknown entity");
    *ok = 0;
    return cs_nil();
}

static cs_value xml_parse_text(xml_parser* p, int* ok) {
    *ok = 1;
    char* buf = NULL;
    size_t len = 0, cap = 0;

    while (p->pos < p->len) {
        int ch = xml_peek(p);

        if (ch == '<') {
            break;
        }

        if (ch == '&') {
            cs_value entity = xml_decode_entity(p, ok);
            if (!*ok) {
                free(buf);
                return cs_nil();
            }

            if (entity.type == CS_T_STR) {
                cs_string* s = (cs_string*)entity.as.p;
                if (!sb_append(&buf, &len, &cap, s->data, s->len)) {
                    cs_value_release(entity);
                    free(buf);
                    cs_error(p->vm, "out of memory");
                    *ok = 0;
                    return cs_nil();
                }
            }
            cs_value_release(entity);
            continue;
        }

        xml_advance(p);
        char c = (char)ch;
        if (!sb_append(&buf, &len, &cap, &c, 1)) {
            free(buf);
            cs_error(p->vm, "out of memory");
            *ok = 0;
            return cs_nil();
        }
    }

    if (!buf) buf = cs_strdup2_local("");
    if (!buf) {
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }
    return cs_str_take(p->vm, buf, (uint64_t)len);
}

static cs_value xml_parse_cdata(xml_parser* p, int* ok) {
    *ok = 1;

    if (!xml_match(p, "<![CDATA[")) {
        cs_error(p->vm, "expected CDATA");
        *ok = 0;
        return cs_nil();
    }

    char* buf = NULL;
    size_t len = 0, cap = 0;

    while (p->pos < p->len) {
        if (xml_match(p, "]]>")) {
            if (!buf) buf = cs_strdup2_local("");
            if (!buf) {
                cs_error(p->vm, "out of memory");
                *ok = 0;
                return cs_nil();
            }
            return cs_str_take(p->vm, buf, (uint64_t)len);
        }

        char ch = (char)xml_advance(p);
        if (!sb_append(&buf, &len, &cap, &ch, 1)) {
            free(buf);
            cs_error(p->vm, "out of memory");
            *ok = 0;
            return cs_nil();
        }
    }

    free(buf);
    cs_error(p->vm, "unterminated CDATA");
    *ok = 0;
    return cs_nil();
}

static cs_value xml_parse_comment(xml_parser* p, int* ok) {
    *ok = 1;

    if (!xml_match(p, "<!--")) {
        cs_error(p->vm, "expected comment");
        *ok = 0;
        return cs_nil();
    }

    char* buf = NULL;
    size_t len = 0, cap = 0;

    while (p->pos < p->len) {
        if (xml_match(p, "-->")) {
            if (!buf) buf = cs_strdup2_local("");
            if (!buf) {
                cs_error(p->vm, "out of memory");
                *ok = 0;
                return cs_nil();
            }
            return cs_str_take(p->vm, buf, (uint64_t)len);
        }

        char ch = (char)xml_advance(p);
        if (!sb_append(&buf, &len, &cap, &ch, 1)) {
            free(buf);
            cs_error(p->vm, "out of memory");
            *ok = 0;
            return cs_nil();
        }
    }

    free(buf);
    cs_error(p->vm, "unterminated comment");
    *ok = 0;
    return cs_nil();
}

static cs_value xml_parse_pi(xml_parser* p, int* ok);
static cs_value xml_parse_element(xml_parser* p, int* ok);

static cs_value xml_parse_pi(xml_parser* p, int* ok) {
    *ok = 1;

    if (!xml_match(p, "<?")) {
        cs_error(p->vm, "expected processing instruction");
        *ok = 0;
        return cs_nil();
    }

    // Skip to ?>
    while (p->pos < p->len) {
        if (xml_match(p, "?>")) {
            return cs_nil(); // We don't store PIs in our simple representation
        }
        xml_advance(p);
    }

    cs_error(p->vm, "unterminated processing instruction");
    *ok = 0;
    return cs_nil();
}

static cs_value xml_parse_attributes(xml_parser* p, int* ok) {
    *ok = 1;

    cs_value attrs = cs_map(p->vm);
    if (attrs.type != CS_T_MAP) {
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }

    while (p->pos < p->len) {
        xml_skip_ws(p);

        int ch = xml_peek(p);
        if (ch == '>' || ch == '/' || ch == '?') {
            break;
        }

        // Parse attribute name
        cs_value name = xml_parse_name(p, ok);
        if (!*ok) {
            cs_value_release(attrs);
            return cs_nil();
        }

        xml_skip_ws(p);

        if (xml_peek(p) != '=') {
            cs_value_release(name);
            cs_value_release(attrs);
            cs_error(p->vm, "expected '=' in attribute");
            *ok = 0;
            return cs_nil();
        }
        xml_advance(p);

        xml_skip_ws(p);

        // Parse attribute value
        int quote = xml_peek(p);
        if (quote != '"' && quote != '\'') {
            cs_value_release(name);
            cs_value_release(attrs);
            cs_error(p->vm, "expected quote in attribute value");
            *ok = 0;
            return cs_nil();
        }
        xml_advance(p);

        char* buf = NULL;
        size_t len = 0, cap = 0;

        while (p->pos < p->len) {
            int ch2 = xml_peek(p);

            if (ch2 == quote) {
                xml_advance(p);
                break;
            }

            if (ch2 == '&') {
                cs_value entity = xml_decode_entity(p, ok);
                if (!*ok) {
                    free(buf);
                    cs_value_release(name);
                    cs_value_release(attrs);
                    return cs_nil();
                }

                if (entity.type == CS_T_STR) {
                    cs_string* s = (cs_string*)entity.as.p;
                    if (!sb_append(&buf, &len, &cap, s->data, s->len)) {
                        cs_value_release(entity);
                        free(buf);
                        cs_value_release(name);
                        cs_value_release(attrs);
                        cs_error(p->vm, "out of memory");
                        *ok = 0;
                        return cs_nil();
                    }
                }
                cs_value_release(entity);
                continue;
            }

            xml_advance(p);
            char c = (char)ch2;
            if (!sb_append(&buf, &len, &cap, &c, 1)) {
                free(buf);
                cs_value_release(name);
                cs_value_release(attrs);
                cs_error(p->vm, "out of memory");
                *ok = 0;
                return cs_nil();
            }
        }

        if (!buf) buf = cs_strdup2_local("");
        if (!buf) {
            cs_value_release(name);
            cs_value_release(attrs);
            cs_error(p->vm, "out of memory");
            *ok = 0;
            return cs_nil();
        }
        cs_value value = cs_str_take(p->vm, buf, (uint64_t)len);

        // Set attribute
        if (name.type == CS_T_STR) {
            cs_string* name_str = (cs_string*)name.as.p;
            cs_map_set(attrs, name_str->data, value);
        }

        cs_value_release(name);
        cs_value_release(value);
    }

    return attrs;
}

static cs_value xml_parse_element(xml_parser* p, int* ok) {
    *ok = 1;

    if (xml_peek(p) != '<') {
        cs_error(p->vm, "expected '<'");
        *ok = 0;
        return cs_nil();
    }
    xml_advance(p);

    // Parse tag name
    cs_value tag_name = xml_parse_name(p, ok);
    if (!*ok) {
        return cs_nil();
    }

    // Parse attributes
    cs_value attrs = xml_parse_attributes(p, ok);
    if (!*ok) {
        cs_value_release(tag_name);
        return cs_nil();
    }

    xml_skip_ws(p);

    // Check for self-closing tag
    if (xml_match(p, "/>")) {
        cs_value elem = cs_map(p->vm);
        if (elem.type != CS_T_MAP) {
            cs_value_release(tag_name);
            cs_value_release(attrs);
            cs_error(p->vm, "out of memory");
            *ok = 0;
            return cs_nil();
        }

        cs_map_set(elem, "name", tag_name);

        // Only add attrs if not empty
        cs_value keys = cs_map_keys(p->vm, attrs);
        if (keys.type == CS_T_LIST && cs_list_len(keys) > 0) {
            cs_map_set(elem, "attrs", attrs);
        }
        cs_value_release(keys);

        cs_value_release(tag_name);
        cs_value_release(attrs);
        return elem;
    }

    if (xml_peek(p) != '>') {
        cs_value_release(tag_name);
        cs_value_release(attrs);
        cs_error(p->vm, "expected '>'");
        *ok = 0;
        return cs_nil();
    }
    xml_advance(p);

    // Parse children
    cs_value children = cs_list(p->vm);
    if (children.type != CS_T_LIST) {
        cs_value_release(tag_name);
        cs_value_release(attrs);
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }

    cs_value text_content = cs_nil();

    while (p->pos < p->len) {
        xml_skip_ws(p);

        if (p->pos >= p->len) break;

        // Check for closing tag
        if (p->pos + 1 < p->len && p->s[p->pos] == '<' && p->s[p->pos + 1] == '/') {
            break;
        }

        // Check for various child node types
        if (xml_peek(p) == '<') {
            if (p->pos + 3 < p->len && strncmp(p->s + p->pos, "<!--", 4) == 0) {
                // Comment - skip it
                cs_value comment = xml_parse_comment(p, ok);
                if (!*ok) {
                    cs_value_release(children);
                    cs_value_release(tag_name);
                    cs_value_release(attrs);
                    return cs_nil();
                }
                cs_value_release(comment);
                continue;
            }

            if (p->pos + 8 < p->len && strncmp(p->s + p->pos, "<![CDATA[", 9) == 0) {
                // CDATA
                cs_value cdata_text = xml_parse_cdata(p, ok);
                if (!*ok) {
                    cs_value_release(children);
                    cs_value_release(tag_name);
                    cs_value_release(attrs);
                    return cs_nil();
                }

                if (text_content.type == CS_T_NIL) {
                    text_content = cdata_text;
                } else {
                    cs_value_release(cdata_text);
                }
                continue;
            }

            if (p->pos + 1 < p->len && p->s[p->pos + 1] == '?') {
                // Processing instruction - skip it
                cs_value pi = xml_parse_pi(p, ok);
                if (!*ok) {
                    cs_value_release(children);
                    cs_value_release(tag_name);
                    cs_value_release(attrs);
                    cs_value_release(text_content);
                    return cs_nil();
                }
                cs_value_release(pi);
                continue;
            }

            // Child element
            cs_value child = xml_parse_element(p, ok);
            if (!*ok) {
                cs_value_release(children);
                cs_value_release(tag_name);
                cs_value_release(attrs);
                cs_value_release(text_content);
                return cs_nil();
            }
            cs_list_push(children, child);
            cs_value_release(child);
        } else {
            // Text content
            cs_value text = xml_parse_text(p, ok);
            if (!*ok) {
                cs_value_release(children);
                cs_value_release(tag_name);
                cs_value_release(attrs);
                cs_value_release(text_content);
                return cs_nil();
            }

            // Trim and check if meaningful
            if (text.type == CS_T_STR) {
                cs_string* text_str = (cs_string*)text.as.p;
                int has_content = 0;
                for (size_t i = 0; i < text_str->len; i++) {
                    if (text_str->data[i] != ' ' && text_str->data[i] != '\t' &&
                        text_str->data[i] != '\r' && text_str->data[i] != '\n') {
                        has_content = 1;
                        break;
                    }
                }

                if (has_content) {
                    if (text_content.type == CS_T_NIL) {
                        text_content = cs_value_copy(text);
                    }
                }
            }

            cs_value_release(text);
        }
    }

    // Parse closing tag
    if (!xml_match(p, "</")) {
        cs_value_release(children);
        cs_value_release(tag_name);
        cs_value_release(attrs);
        cs_value_release(text_content);
        cs_error(p->vm, "expected closing tag");
        *ok = 0;
        return cs_nil();
    }

    cs_value close_name = xml_parse_name(p, ok);
    if (!*ok) {
        cs_value_release(children);
        cs_value_release(tag_name);
        cs_value_release(attrs);
        cs_value_release(text_content);
        return cs_nil();
    }

    // Verify tag names match
    if (tag_name.type == CS_T_STR && close_name.type == CS_T_STR) {
        cs_string* open_str = (cs_string*)tag_name.as.p;
        cs_string* close_str = (cs_string*)close_name.as.p;
        if (open_str->len != close_str->len ||
            strncmp(open_str->data, close_str->data, open_str->len) != 0) {
            cs_value_release(close_name);
            cs_value_release(children);
            cs_value_release(tag_name);
            cs_value_release(attrs);
            cs_value_release(text_content);
            cs_error(p->vm, "mismatched tags");
            *ok = 0;
            return cs_nil();
        }
    }
    cs_value_release(close_name);

    xml_skip_ws(p);
    if (xml_peek(p) != '>') {
        cs_value_release(children);
        cs_value_release(tag_name);
        cs_value_release(attrs);
        cs_value_release(text_content);
        cs_error(p->vm, "expected '>' in closing tag");
        *ok = 0;
        return cs_nil();
    }
    xml_advance(p);

    // Build element map
    cs_value elem = cs_map(p->vm);
    if (elem.type != CS_T_MAP) {
        cs_value_release(children);
        cs_value_release(tag_name);
        cs_value_release(attrs);
        cs_value_release(text_content);
        cs_error(p->vm, "out of memory");
        *ok = 0;
        return cs_nil();
    }

    cs_map_set(elem, "name", tag_name);

    // Add attrs if not empty
    cs_value keys = cs_map_keys(p->vm, attrs);
    if (keys.type == CS_T_LIST && cs_list_len(keys) > 0) {
        cs_map_set(elem, "attrs", attrs);
    }
    cs_value_release(keys);

    // Add text or children
    if (text_content.type != CS_T_NIL && cs_list_len(children) == 0) {
        cs_map_set(elem, "text", text_content);
    } else if (cs_list_len(children) > 0) {
        cs_map_set(elem, "children", children);
    }

    cs_value_release(tag_name);
    cs_value_release(attrs);
    cs_value_release(children);
    cs_value_release(text_content);

    return elem;
}

static int nf_xml_parse(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_STR) {
        cs_error(vm, "xml_parse() requires a string argument");
        return 1;
    }

    cs_string* str_obj = (cs_string*)argv[0].as.p;
    const char* text = str_obj->data;
    size_t text_len = str_obj->len;

    xml_parser p;
    xml_init(&p, text, text_len, vm);

    // Skip XML declaration and DOCTYPE
    xml_skip_ws(&p);
    while (p.pos < p.len) {
        if (xml_match(&p, "<?")) {
            // Skip processing instruction
            while (p.pos < p.len && !xml_match(&p, "?>")) {
                xml_advance(&p);
            }
            xml_skip_ws(&p);
        } else if (xml_match(&p, "<!DOCTYPE")) {
            // Skip DOCTYPE
            int depth = 1;
            while (p.pos < p.len && depth > 0) {
                int ch = xml_advance(&p);
                if (ch == '<') depth++;
                if (ch == '>') depth--;
            }
            xml_skip_ws(&p);
        } else if (p.pos + 3 < p.len && strncmp(p.s + p.pos, "<!--", 4) == 0) {
            // Skip comment
            int ok = 1;
            cs_value comment = xml_parse_comment(&p, &ok);
            cs_value_release(comment);
            if (!ok) return 1;
            xml_skip_ws(&p);
        } else {
            break;
        }
    }

    if (p.pos >= p.len) {
        cs_error(vm, "no root element found");
        return 1;
    }

    int ok = 1;
    cs_value result = xml_parse_element(&p, &ok);

    if (!ok) {
        return 1;
    }

    *out = result;
    return 0;
}

// Forward declaration for recursive stringify
static int xml_stringify_element(cs_vm* vm, cs_value elem, char** buf, size_t* len, size_t* cap, int depth, int indent);

static int xml_stringify_element(cs_vm* vm, cs_value elem, char** buf, size_t* len, size_t* cap, int depth, int indent) {
    if (elem.type != CS_T_MAP) {
        cs_error(vm, "xml_stringify expects map");
        return 0;
    }

    // Get element name
    cs_value name_val = cs_map_get(elem, "name");
    if (name_val.type != CS_T_STR) {
        cs_value_release(name_val);
        cs_error(vm, "element missing name");
        return 0;
    }
    cs_string* name = (cs_string*)name_val.as.p;

    // Add indentation
    for (int i = 0; i < depth * indent; i++) {
        if (!sb_append(buf, len, cap, " ", 1)) {
            cs_value_release(name_val);
            cs_error(vm, "out of memory");
            return 0;
        }
    }

    // Opening tag
    if (!sb_append(buf, len, cap, "<", 1) ||
        !sb_append(buf, len, cap, name->data, name->len)) {
        cs_value_release(name_val);
        cs_error(vm, "out of memory");
        return 0;
    }

    // Attributes
    cs_value attrs = cs_map_get(elem, "attrs");
    if (attrs.type == CS_T_MAP) {
        cs_value keys = cs_map_keys(vm, attrs);
        if (keys.type == CS_T_LIST) {
            size_t num_keys = cs_list_len(keys);
            for (size_t i = 0; i < num_keys; i++) {
                cs_value key = cs_list_get(keys, i);
                if (key.type == CS_T_STR) {
                    cs_string* key_str = (cs_string*)key.as.p;
                    cs_value val = cs_map_get(attrs, key_str->data);

                    if (!sb_append(buf, len, cap, " ", 1) ||
                        !sb_append(buf, len, cap, key_str->data, key_str->len) ||
                        !sb_append(buf, len, cap, "=\"", 2)) {
                        cs_value_release(val);
                        cs_value_release(key);
                        cs_value_release(keys);
                        cs_value_release(attrs);
                        cs_value_release(name_val);
                        cs_error(vm, "out of memory");
                        return 0;
                    }

                    // Escape attribute value
                    if (val.type == CS_T_STR) {
                        cs_string* val_str = (cs_string*)val.as.p;
                        for (size_t j = 0; j < val_str->len; j++) {
                            char ch = val_str->data[j];
                            const char* esc = NULL;
                            if (ch == '<') esc = "&lt;";
                            else if (ch == '>') esc = "&gt;";
                            else if (ch == '&') esc = "&amp;";
                            else if (ch == '"') esc = "&quot;";
                            else if (ch == '\'') esc = "&apos;";

                            if (esc) {
                                if (!sb_append(buf, len, cap, esc, strlen(esc))) {
                                    cs_value_release(val);
                                    cs_value_release(key);
                                    cs_value_release(keys);
                                    cs_value_release(attrs);
                                    cs_value_release(name_val);
                                    cs_error(vm, "out of memory");
                                    return 0;
                                }
                            } else {
                                if (!sb_append(buf, len, cap, &ch, 1)) {
                                    cs_value_release(val);
                                    cs_value_release(key);
                                    cs_value_release(keys);
                                    cs_value_release(attrs);
                                    cs_value_release(name_val);
                                    cs_error(vm, "out of memory");
                                    return 0;
                                }
                            }
                        }
                    }

                    if (!sb_append(buf, len, cap, "\"", 1)) {
                        cs_value_release(val);
                        cs_value_release(key);
                        cs_value_release(keys);
                        cs_value_release(attrs);
                        cs_value_release(name_val);
                        cs_error(vm, "out of memory");
                        return 0;
                    }

                    cs_value_release(val);
                }
                cs_value_release(key);
            }
        }
        cs_value_release(keys);
    }
    cs_value_release(attrs);

    // Check for text content
    cs_value text = cs_map_get(elem, "text");
    cs_value children = cs_map_get(elem, "children");

    // Self-closing tag if no content
    if (text.type == CS_T_NIL && (children.type != CS_T_LIST || cs_list_len(children) == 0)) {
        if (!sb_append(buf, len, cap, "/>", 2)) {
            cs_value_release(text);
            cs_value_release(children);
            cs_value_release(name_val);
            cs_error(vm, "out of memory");
            return 0;
        }
        cs_value_release(text);
        cs_value_release(children);
        cs_value_release(name_val);
        return 1;
    }

    // Close opening tag
    if (!sb_append(buf, len, cap, ">", 1)) {
        cs_value_release(text);
        cs_value_release(children);
        cs_value_release(name_val);
        cs_error(vm, "out of memory");
        return 0;
    }

    // Text content
    if (text.type == CS_T_STR) {
        cs_string* text_str = (cs_string*)text.as.p;
        for (size_t i = 0; i < text_str->len; i++) {
            char ch = text_str->data[i];
            const char* esc = NULL;
            if (ch == '<') esc = "&lt;";
            else if (ch == '>') esc = "&gt;";
            else if (ch == '&') esc = "&amp;";

            if (esc) {
                if (!sb_append(buf, len, cap, esc, strlen(esc))) {
                    cs_value_release(text);
                    cs_value_release(children);
                    cs_value_release(name_val);
                    cs_error(vm, "out of memory");
                    return 0;
                }
            } else {
                if (!sb_append(buf, len, cap, &ch, 1)) {
                    cs_value_release(text);
                    cs_value_release(children);
                    cs_value_release(name_val);
                    cs_error(vm, "out of memory");
                    return 0;
                }
            }
        }
    }

    // Child elements
    if (children.type == CS_T_LIST) {
        size_t num_children = cs_list_len(children);
        for (size_t i = 0; i < num_children; i++) {
            if (indent > 0) {
                if (!sb_append(buf, len, cap, "\n", 1)) {
                    cs_value_release(text);
                    cs_value_release(children);
                    cs_value_release(name_val);
                    cs_error(vm, "out of memory");
                    return 0;
                }
            }

            cs_value child = cs_list_get(children, i);
            if (!xml_stringify_element(vm, child, buf, len, cap, depth + 1, indent)) {
                cs_value_release(child);
                cs_value_release(text);
                cs_value_release(children);
                cs_value_release(name_val);
                return 0;
            }
            cs_value_release(child);
        }

        if (num_children > 0 && indent > 0) {
            if (!sb_append(buf, len, cap, "\n", 1)) {
                cs_value_release(text);
                cs_value_release(children);
                cs_value_release(name_val);
                cs_error(vm, "out of memory");
                return 0;
            }
            for (int i = 0; i < depth * indent; i++) {
                if (!sb_append(buf, len, cap, " ", 1)) {
                    cs_value_release(text);
                    cs_value_release(children);
                    cs_value_release(name_val);
                    cs_error(vm, "out of memory");
                    return 0;
                }
            }
        }
    }

    cs_value_release(text);
    cs_value_release(children);

    // Closing tag
    if (!sb_append(buf, len, cap, "</", 2) ||
        !sb_append(buf, len, cap, name->data, name->len) ||
        !sb_append(buf, len, cap, ">", 1)) {
        cs_value_release(name_val);
        cs_error(vm, "out of memory");
        return 0;
    }

    cs_value_release(name_val);
    return 1;
}

static int nf_xml_stringify(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1) {
        cs_error(vm, "xml_stringify() requires a value argument");
        return 1;
    }

    int indent = 2;
    if (argc >= 2 && argv[1].type == CS_T_INT) {
        indent = (int)argv[1].as.i;
        if (indent < 0) indent = 0;
        if (indent > 8) indent = 8;
    }

    char* buf = NULL;
    size_t len = 0, cap = 0;

    if (!xml_stringify_element(vm, argv[0], &buf, &len, &cap, 0, indent)) {
        free(buf);
        return 1;
    }

    if (!buf) buf = cs_strdup2_local("");
    if (!buf) {
        cs_error(vm, "out of memory");
        return 1;
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

static int nf_is_set(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    *out = cs_bool(argc > 0 && argv[0].type == CS_T_SET);
    return 0;
}

static int nf_is_bytes(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    *out = cs_bool(argc > 0 && argv[0].type == CS_T_BYTES);
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

static int is_number(cs_value v) {
    return v.type == CS_T_INT || v.type == CS_T_FLOAT;
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

static int nf_sin(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1 || !is_number(argv[0])) { *out = cs_nil(); return 0; }
    *out = cs_float(sin(to_number(argv[0])));
    return 0;
}

static int nf_cos(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1 || !is_number(argv[0])) { *out = cs_nil(); return 0; }
    *out = cs_float(cos(to_number(argv[0])));
    return 0;
}

static int nf_tan(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1 || !is_number(argv[0])) { *out = cs_nil(); return 0; }
    *out = cs_float(tan(to_number(argv[0])));
    return 0;
}

static int nf_log(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1 || !is_number(argv[0])) { *out = cs_nil(); return 0; }
    *out = cs_float(log(to_number(argv[0])));
    return 0;
}

static int nf_exp(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1 || !is_number(argv[0])) { *out = cs_nil(); return 0; }
    *out = cs_float(exp(to_number(argv[0])));
    return 0;
}

static int nf_random(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud; (void)argv;
    if (!out) return 0;
    if (argc != 0) { *out = cs_nil(); return 0; }
    static int seeded = 0;
    if (!seeded) { srand((unsigned int)time(NULL)); seeded = 1; }
    double r = (double)rand() / ((double)RAND_MAX + 1.0);
    *out = cs_float(r);
    return 0;
}

static int nf_clamp(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 3) { *out = cs_nil(); return 0; }
    
    double value = to_number(argv[0]);
    double min_val = to_number(argv[1]);
    double max_val = to_number(argv[2]);
    
    double result = value;
    if (result < min_val) result = min_val;
    if (result > max_val) result = max_val;
    
    // Return same type as input
    if (argv[0].type == CS_T_INT && argv[1].type == CS_T_INT && argv[2].type == CS_T_INT) {
        *out = cs_int((int64_t)result);
    } else {
        *out = cs_float(result);
    }
    return 0;
}

static int nf_trunc(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1) { *out = cs_nil(); return 0; }
    
    double v = to_number(argv[0]);
    *out = cs_int((int64_t)trunc(v));
    return 0;
}

static int nf_asin(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1 || !is_number(argv[0])) { *out = cs_nil(); return 0; }
    *out = cs_float(asin(to_number(argv[0])));
    return 0;
}

static int nf_acos(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1 || !is_number(argv[0])) { *out = cs_nil(); return 0; }
    *out = cs_float(acos(to_number(argv[0])));
    return 0;
}

static int nf_atan(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1 || !is_number(argv[0])) { *out = cs_nil(); return 0; }
    *out = cs_float(atan(to_number(argv[0])));
    return 0;
}

static int nf_atan2(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2 || !is_number(argv[0]) || !is_number(argv[1])) { 
        *out = cs_nil(); 
        return 0; 
    }
    double y = to_number(argv[0]);
    double x = to_number(argv[1]);
    *out = cs_float(atan2(y, x));
    return 0;
}

static int nf_log10(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1 || !is_number(argv[0])) { *out = cs_nil(); return 0; }
    *out = cs_float(log10(to_number(argv[0])));
    return 0;
}

static int nf_log2(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 1 || !is_number(argv[0])) { *out = cs_nil(); return 0; }
    *out = cs_float(log2(to_number(argv[0])));
    return 0;
}

static int nf_random_int(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc != 2 || !is_number(argv[0]) || !is_number(argv[1])) { 
        *out = cs_nil(); 
        return 0; 
    }
    
    static int seeded = 0;
    if (!seeded) { srand((unsigned int)time(NULL)); seeded = 1; }
    
    int64_t min_val = (int64_t)to_number(argv[0]);
    int64_t max_val = (int64_t)to_number(argv[1]);
    
    if (min_val > max_val) {
        int64_t temp = min_val;
        min_val = max_val;
        max_val = temp;
    }
    
    int64_t range = max_val - min_val + 1;
    int64_t result = min_val + (int64_t)(rand() % range);
    *out = cs_int(result);
    return 0;
}

static int nf_random_choice(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_LIST) { 
        *out = cs_nil(); 
        return 0; 
    }
    
    cs_list_obj* list = (cs_list_obj*)argv[0].as.p;
    if (list->len == 0) {
        *out = cs_nil();
        return 0;
    }
    
    static int seeded = 0;
    if (!seeded) { srand((unsigned int)time(NULL)); seeded = 1; }
    
    size_t idx = (size_t)(rand() % list->len);
    *out = cs_value_copy(list->items[idx]);
    return 0;
}

static int nf_shuffle(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_LIST) { 
        *out = cs_nil(); 
        return 0; 
    }
    
    cs_list_obj* list = (cs_list_obj*)argv[0].as.p;
    if (list->len <= 1) {
        *out = cs_nil();
        return 0;
    }
    
    static int seeded = 0;
    if (!seeded) { srand((unsigned int)time(NULL)); seeded = 1; }
    
    // Fisher-Yates shuffle
    for (size_t i = list->len - 1; i > 0; i--) {
        size_t j = (size_t)(rand() % (i + 1));
        cs_value temp = list->items[i];
        list->items[i] = list->items[j];
        list->items[j] = temp;
    }
    
    *out = cs_nil();
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

    cs_string* str_obj = (cs_string*)argv[0].as.p;
    const char* s = str_obj->data;
    const char* orig_s = s;

    while (*s && isspace((unsigned char)*s)) s++;

    const char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;

    size_t len = (size_t)(end - s);

    // Early exit: if nothing was trimmed, return original string
    if (s == orig_s && len == str_obj->len) {
        *out = argv[0];
        return 0;
    }

    char* trimmed = (char*)malloc(len + 1);
    if (!trimmed) { cs_error(vm, "out of memory"); return 1; }
    memcpy(trimmed, s, len);
    trimmed[len] = '\0';

    *out = cs_str_take(vm, trimmed, (uint64_t)len);
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

    cs_string* str_obj = (cs_string*)argv[0].as.p;
    const char* s = str_obj->data;
    size_t len = str_obj->len;

    char* lower = (char*)malloc(len + 1);
    if (!lower) { cs_error(vm, "out of memory"); return 1; }

    for (size_t i = 0; i < len; i++) {
        lower[i] = (char)tolower((unsigned char)s[i]);
    }
    lower[len] = '\0';

    *out = cs_str_take(vm, lower, (uint64_t)len);
    return 0;
}

static int nf_str_upper(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_STR) { cs_error(vm, "str_upper() requires a string argument"); return 1; }

    cs_string* str_obj = (cs_string*)argv[0].as.p;
    const char* s = str_obj->data;
    size_t len = str_obj->len;

    char* upper = (char*)malloc(len + 1);
    if (!upper) { cs_error(vm, "out of memory"); return 1; }

    for (size_t i = 0; i < len; i++) {
        upper[i] = (char)toupper((unsigned char)s[i]);
    }
    upper[len] = '\0';

    *out = cs_str_take(vm, upper, (uint64_t)len);
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
    (void)vm; (void)ud;
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

        case CS_T_SET: {
            cs_value new_set = cs_set(vm);
            if (!new_set.as.p) { cs_error(vm, "out of memory"); return 1; }
            cs_map_obj* src_map = (cs_map_obj*)src.as.p;
            for (size_t i = 0; i < src_map->cap; i++) {
                if (!src_map->entries[i].in_use) continue;
                if (cs_map_set_value(new_set, src_map->entries[i].key, cs_bool(1)) != 0) {
                    cs_value_release(new_set);
                    cs_error(vm, "out of memory");
                    return 1;
                }
            }
            *out = new_set;
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
    if (src.type == CS_T_LIST || src.type == CS_T_MAP || src.type == CS_T_SET) {
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

        case CS_T_SET: {
            cs_value new_set = cs_set(vm);
            cs_map_obj* src_map = (cs_map_obj*)src.as.p;

            char ptr_str[32];
            snprintf(ptr_str, sizeof(ptr_str), "%p", src.as.p);
            if (cs_map_set(visited_map, ptr_str, new_set) != 0) { cs_value_release(new_set); return cs_nil(); }

            for (size_t i = 0; i < src_map->cap; i++) {
                if (!src_map->entries[i].in_use) continue;
                cs_value key = deepcopy_impl(vm, src_map->entries[i].key, visited_map);
                if (key.type == CS_T_NIL && src_map->entries[i].key.type != CS_T_NIL) { cs_value_release(new_set); return cs_nil(); }
                if (cs_map_set_value(new_set, key, cs_bool(1)) != 0) { cs_value_release(key); cs_value_release(new_set); return cs_nil(); }
                cs_value_release(key);
            }

            return new_set;
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

        case CS_T_SET: {
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
            cs_error(vm, "contains() requires a list, map, set, or string");
            return 1;
    }
}

static int nf_set_add(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_SET) { *out = cs_bool(0); return 0; }

    int has = cs_map_has_value(argv[0], argv[1]);
    if (!has) {
        if (cs_map_set_value(argv[0], argv[1], cs_bool(1)) != 0) {
            cs_error(vm, "out of memory");
            return 1;
        }
    }
    *out = cs_bool(!has);
    return 0;
}

static int nf_set_has(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm;
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_SET) { *out = cs_bool(0); return 0; }
    *out = cs_bool(cs_map_has_value(argv[0], argv[1]) != 0);
    return 0;
}

static int nf_set_del(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)vm;
    (void)ud;
    if (!out) return 0;
    if (argc != 2 || argv[0].type != CS_T_SET) { *out = cs_bool(0); return 0; }
    *out = cs_bool(cs_map_del_value(argv[0], argv[1]) == 0);
    return 0;
}

static int nf_set_values(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_SET) { *out = cs_nil(); return 0; }

    cs_map_obj* m = (cs_map_obj*)argv[0].as.p;
    cs_value list_val = cs_list(vm);
    cs_list_obj* list = (cs_list_obj*)list_val.as.p;
    if (!list) { cs_error(vm, "out of memory"); return 1; }

    if (!list_ensure(list, m ? m->len : 0)) { cs_value_release(list_val); cs_error(vm, "out of memory"); return 1; }

    for (size_t i = 0; m && i < m->cap; i++) {
        if (!m->entries[i].in_use) continue;
        list->items[list->len++] = cs_value_copy(m->entries[i].key);
    }

    *out = list_val;
    return 0;
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

#if defined(__linux__)
static int nf_event_loop_start(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    int result = cs_event_loop_start(vm);
    *out = cs_bool(result);
    return 0;
}

static int nf_event_loop_stop(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    int result = cs_event_loop_stop(vm);
    *out = cs_bool(result);
    return 0;
}

static int nf_event_loop_running(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    int result = cs_event_loop_running(vm);
    *out = cs_bool(result);
    return 0;
}
#endif

void cs_register_stdlib(cs_vm* vm) {
    cs_register_native(vm, "print",  nf_print,  NULL);
    cs_register_native(vm, "println",  nf_print,  NULL);  // alias for print (already adds newline)
    cs_register_native(vm, "typeof", nf_typeof, NULL);
    cs_register_native(vm, "getenv", nf_getenv, NULL);
    cs_register_native(vm, "assert", nf_assert, NULL);
    cs_register_native(vm, "subprocess", nf_subprocess, NULL);
    cs_register_native(vm, "load",   nf_load,   NULL);
    cs_register_native(vm, "require", nf_require, NULL);
    cs_register_native(vm, "require_optional", nf_require_optional, NULL);
    cs_register_native(vm, "list",   nf_list,   NULL);
    cs_register_native(vm, "map",    nf_map,    NULL);
    cs_register_native(vm, "set",    nf_set,    NULL);
    cs_register_native(vm, "strbuf", nf_strbuf, NULL);
    cs_register_native(vm, "bytes",  nf_bytes,  NULL);
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
    cs_register_native(vm, "is_set",      nf_is_set,      NULL);
    cs_register_native(vm, "is_bytes",    nf_is_bytes,    NULL);
    cs_register_native(vm, "is_list",     nf_is_list,     NULL);
    cs_register_native(vm, "is_map",      nf_is_map,      NULL);
    cs_register_native(vm, "is_function", nf_is_function, NULL);
    
    // Math functions
    cs_register_native(vm, "abs",   nf_abs,   NULL);
    cs_register_native(vm, "min",   nf_min,   NULL);
    cs_register_native(vm, "max",   nf_max,   NULL);
    cs_register_native(vm, "clamp", nf_clamp, NULL);
    cs_register_native(vm, "floor", nf_floor, NULL);
    cs_register_native(vm, "ceil",  nf_ceil,  NULL);
    cs_register_native(vm, "round", nf_round, NULL);
    cs_register_native(vm, "trunc", nf_trunc, NULL);
    cs_register_native(vm, "sqrt",  nf_sqrt,  NULL);
    cs_register_native(vm, "pow",   nf_pow,   NULL);
    cs_register_native(vm, "exp",   nf_exp,   NULL);
    cs_register_native(vm, "log",   nf_log,   NULL);
    cs_register_native(vm, "log10", nf_log10, NULL);
    cs_register_native(vm, "log2",  nf_log2,  NULL);
    cs_register_native(vm, "sin",   nf_sin,   NULL);
    cs_register_native(vm, "cos",   nf_cos,   NULL);
    cs_register_native(vm, "tan",   nf_tan,   NULL);
    cs_register_native(vm, "asin",  nf_asin,  NULL);
    cs_register_native(vm, "acos",  nf_acos,  NULL);
    cs_register_native(vm, "atan",  nf_atan,  NULL);
    cs_register_native(vm, "atan2", nf_atan2, NULL);
    cs_register_native(vm, "random", nf_random, NULL);
    cs_register_native(vm, "random_int", nf_random_int, NULL);
    cs_register_native(vm, "random_choice", nf_random_choice, NULL);
    cs_register_native(vm, "shuffle", nf_shuffle, NULL);

    // Math constants
    cs_register_global(vm, "PI", cs_float(3.14159265358979323846));
    cs_register_global(vm, "E",  cs_float(2.71828182845904523536));
    
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
    cs_register_native(vm, "csv_parse",     nf_csv_parse,     NULL);
    cs_register_native(vm, "csv_stringify",  nf_csv_stringify,  NULL);
    cs_register_native(vm, "yaml_parse",    nf_yaml_parse,    NULL);
    cs_register_native(vm, "yaml_parse_all", nf_yaml_parse_all, NULL);
    cs_register_native(vm, "yaml_stringify", nf_yaml_stringify, NULL);
    cs_register_native(vm, "xml_parse",     nf_xml_parse,     NULL);
    cs_register_native(vm, "xml_stringify",  nf_xml_stringify,  NULL);
    cs_register_native(vm, "read_file",  nf_read_file,  NULL);
    cs_register_native(vm, "read_file_bytes",  nf_read_file_bytes,  NULL);
    cs_register_native(vm, "write_file", nf_write_file, NULL);
    cs_register_native(vm, "write_file_bytes", nf_write_file_bytes, NULL);
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

#if defined(__linux__)
    // Advanced file handling
    cs_register_native(vm, "glob",              nf_glob,              NULL);
    cs_register_native(vm, "watch_file",        nf_watch_file,        NULL);
    cs_register_native(vm, "watch_dir",         nf_watch_dir,         NULL);
    cs_register_native(vm, "unwatch",           nf_unwatch,           NULL);
    cs_register_native(vm, "process_file_watches", nf_process_file_watches, NULL);
    cs_register_native(vm, "temp_file",         nf_temp_file,         NULL);
    cs_register_native(vm, "temp_dir",          nf_temp_dir,          NULL);
    cs_register_native(vm, "gzip_compress",     nf_gzip_compress,     NULL);
    cs_register_native(vm, "gzip_decompress",   nf_gzip_decompress,   NULL);
    cs_register_native(vm, "tar_create",        nf_tar_create,        NULL);
    cs_register_native(vm, "tar_list",          nf_tar_list,          NULL);
    cs_register_native(vm, "tar_extract",       nf_tar_extract,       NULL);
#endif
    
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

#if defined(__linux__)
    cs_register_native(vm, "event_loop_start",   nf_event_loop_start,   NULL);
    cs_register_native(vm, "event_loop_stop",    nf_event_loop_stop,    NULL);
    cs_register_native(vm, "event_loop_running", nf_event_loop_running, NULL);
#endif
    
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
    cs_register_native(vm, "set_add",    nf_set_add,    NULL);
    cs_register_native(vm, "set_has",    nf_set_has,    NULL);
    cs_register_native(vm, "set_del",    nf_set_del,    NULL);
    cs_register_native(vm, "set_values", nf_set_values, NULL);
    
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
        cs_value tmp;
        tmp = cs_str(vm, "INVALID_ARG");    cs_map_set(err_map, "INVALID_ARG", tmp);    cs_value_release(tmp);
        tmp = cs_str(vm, "TYPE_ERROR");     cs_map_set(err_map, "TYPE_ERROR", tmp);     cs_value_release(tmp);
        tmp = cs_str(vm, "DIV_ZERO");       cs_map_set(err_map, "DIV_ZERO", tmp);       cs_value_release(tmp);
        tmp = cs_str(vm, "OUT_OF_BOUNDS");  cs_map_set(err_map, "OUT_OF_BOUNDS", tmp);  cs_value_release(tmp);
        tmp = cs_str(vm, "NOT_FOUND");      cs_map_set(err_map, "NOT_FOUND", tmp);      cs_value_release(tmp);
        tmp = cs_str(vm, "ASSERTION");      cs_map_set(err_map, "ASSERTION", tmp);      cs_value_release(tmp);
        tmp = cs_str(vm, "ERROR");          cs_map_set(err_map, "GENERIC", tmp);        cs_value_release(tmp);

        // Network error codes
        tmp = cs_str(vm, "NET_RESOLVE");    cs_map_set(err_map, "NET_RESOLVE", tmp);    cs_value_release(tmp);
        tmp = cs_str(vm, "NET_CONNECT");    cs_map_set(err_map, "NET_CONNECT", tmp);    cs_value_release(tmp);
        tmp = cs_str(vm, "NET_TIMEOUT");    cs_map_set(err_map, "NET_TIMEOUT", tmp);    cs_value_release(tmp);
        tmp = cs_str(vm, "NET_CLOSED");     cs_map_set(err_map, "NET_CLOSED", tmp);     cs_value_release(tmp);
        tmp = cs_str(vm, "NET_SEND");       cs_map_set(err_map, "NET_SEND", tmp);       cs_value_release(tmp);
        tmp = cs_str(vm, "NET_RECV");       cs_map_set(err_map, "NET_RECV", tmp);       cs_value_release(tmp);
        tmp = cs_str(vm, "HTTP_PARSE");     cs_map_set(err_map, "HTTP_PARSE", tmp);     cs_value_release(tmp);
        tmp = cs_str(vm, "HTTP_REDIRECT");  cs_map_set(err_map, "HTTP_REDIRECT", tmp);  cs_value_release(tmp);
        tmp = cs_str(vm, "HTTP_NO_TLS");    cs_map_set(err_map, "HTTP_NO_TLS", tmp);    cs_value_release(tmp);
        tmp = cs_str(vm, "TLS_INIT");       cs_map_set(err_map, "TLS_INIT", tmp);       cs_value_release(tmp);
        tmp = cs_str(vm, "TLS_HANDSHAKE");  cs_map_set(err_map, "TLS_HANDSHAKE", tmp);  cs_value_release(tmp);
        tmp = cs_str(vm, "TLS_CERT");       cs_map_set(err_map, "TLS_CERT", tmp);       cs_value_release(tmp);
        tmp = cs_str(vm, "TLS_READ");       cs_map_set(err_map, "TLS_READ", tmp);       cs_value_release(tmp);
        tmp = cs_str(vm, "TLS_WRITE");      cs_map_set(err_map, "TLS_WRITE", tmp);      cs_value_release(tmp);
        
        // Register as global ERR constant
        cs_register_global(vm, "ERR", err_map);
        cs_value_release(err_map);
    }

    cs_register_net_stdlib(vm);
    cs_register_tls_stdlib(vm);
    cs_register_http_stdlib(vm);
}
