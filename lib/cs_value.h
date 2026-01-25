#ifndef CS_VALUE_H
#define CS_VALUE_H

#include "cupidscript.h"
#include <stddef.h>

typedef struct cs_string {
    int ref;
    size_t len;
    size_t cap; // capacity in bytes excluding trailing NUL
    char* data;
} cs_string;

typedef struct cs_func cs_func;

typedef struct cs_native {
    int ref;
    cs_native_fn fn;
    void* userdata;
} cs_native;

typedef struct cs_list_obj {
    int ref;
    size_t len;
    size_t cap;
    cs_value* items;
} cs_list_obj;

typedef struct cs_map_entry {
    cs_string* key;
    cs_value val;
} cs_map_entry;

typedef struct cs_map_obj {
    int ref;
    size_t len;
    size_t cap;
    cs_map_entry* entries;
} cs_map_obj;

typedef struct cs_strbuf_obj {
    int ref;
    size_t len;
    size_t cap;
    char* data;
} cs_strbuf_obj;

// refcounted heap objects
cs_string* cs_str_new(const char* s);
cs_string* cs_str_new_take(char* owned, size_t len);
void       cs_str_incref(cs_string* s);
void       cs_str_decref(cs_string* s);

const char* cs_type_name_impl(cs_type t);

#endif
