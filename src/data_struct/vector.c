#include <stddef.h>
#include <stdlib.h>

#include "../utils.h"

#include "vector.h"

#define IMPL _PRIVATE_dont_access_PRIVATE_


Vector Vector_new(size_t cap) {
    Vector v = {
        malloc((cap + 1) * sizeof(void *)),
        {
            .cap = cap,
            .len = 0,
        }
    };
    v.el[0] = nullptr;
    return v;
}

void Vector_bye(Vector *v) {
    for (size_t i = 0; v->el[i]; i++)
        free(v->el[i]);
    free(v->el);
}

void Vector_add(Vector *v, size_t add) {
    if (v->IMPL.len + add > v->IMPL.cap) {
        v->IMPL.cap = MAX(v->IMPL.cap * 2, v->IMPL.len + add);
        v->el = realloc(v->el, (v->IMPL.cap + 1) * sizeof(void *));
    }
}

void Vector_set_len_no_free(Vector *v, size_t len) {
    v->IMPL.len = len;
    v->el[len] = nullptr;
}

void Vector_set_len(Vector *v, size_t len) {
    if (len < v->IMPL.len)
        for (size_t i = len; v->el[i]; i++)
            free(v->el[i]);

    Vector_set_len_no_free(v, len);
}

size_t Vector_len(Vector v) {
    return v.IMPL.len;
}

void Vector_min_cap(Vector *v) {
    v->IMPL.cap = v->IMPL.len;
    v->el = realloc(v->el, (v->IMPL.cap + 1) * sizeof(void *));
}

void Vector_sane_cap(Vector *v) {
    v->IMPL.cap = v->IMPL.len * 2;
    v->el = realloc(v->el, (v->IMPL.cap + 1) * sizeof(void *));
}
