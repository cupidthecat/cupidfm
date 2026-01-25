#include "cs_value.h"
#include <stdlib.h>
#include <string.h>

static char* cs_strdup(const char* s) {
    size_t n = strlen(s);
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

cs_string* cs_str_new(const char* s) {
    cs_string* st = (cs_string*)calloc(1, sizeof(cs_string));
    if (!st) return NULL;
    st->ref = 1;
    st->data = cs_strdup(s ? s : "");
    st->len = st->data ? strlen(st->data) : 0;
    st->cap = st->len;
    return st;
}

cs_string* cs_str_new_take(char* owned, size_t len) {
    cs_string* st = (cs_string*)calloc(1, sizeof(cs_string));
    if (!st) { free(owned); return NULL; }
    st->ref = 1;
    if (!owned) {
        st->data = cs_strdup("");
        st->len = st->data ? strlen(st->data) : 0;
        st->cap = st->len;
        return st;
    }
    st->data = owned;
    st->len = (len == (size_t)-1) ? strlen(owned) : len;
    st->cap = st->len;
    return st;
}

void cs_str_incref(cs_string* s) {
    if (s) s->ref++;
}

void cs_str_decref(cs_string* s) {
    if (!s) return;
    s->ref--;
    if (s->ref <= 0) {
        free(s->data);
        free(s);
    }
}

const char* cs_type_name_impl(cs_type t) {
    switch (t) {
        case CS_T_NIL:    return "nil";
        case CS_T_BOOL:   return "bool";
        case CS_T_INT:    return "int";
        case CS_T_FLOAT:  return "float";
        case CS_T_STR:    return "string";
        case CS_T_LIST:   return "list";
        case CS_T_MAP:    return "map";
        case CS_T_STRBUF: return "strbuf";
        case CS_T_RANGE:  return "range";
        case CS_T_FUNC:   return "function";
        case CS_T_NATIVE: return "native";
        default:          return "unknown";
    }
}
