// File: src/vector.c
// -----------------------
#include <stddef.h> // for size_t
#include <stdint.h>
#include <errno.h>
#include <stdlib.h> // for malloc, free, realloc
#include <utils.h>  // for MAX

#include "vector.h" // for Vector

#define IMPL _PRIVATE_dont_access_PRIVATE_
/**
 * Function to create a new Vector
 *
 * @param cap the initial capacity of the Vector
 * @return a new Vector
 */
Vector Vector_new(size_t cap) {
    if (cap > SIZE_MAX / sizeof(void *) - 1) {
        errno = EOVERFLOW;
        return (Vector){0};
    }
    void **el = malloc((cap + 1) * sizeof(void *));
    if (!el) {
        Vector v = {NULL, {.cap = 0, .len = 0}};
        return v;
    }
    Vector v = {
        el,
        {
            .cap = cap,
            .len = 0,
        }
    };
    v.el[0] = NULL;
    return v;
}
/**
 * Function to free the memory allocated for a Vector
 *
 * @param v the Vector to free
 */
void Vector_bye(Vector *v) {
    for (size_t i = 0; i < v->IMPL.len; i++) {
        if (v->el[i]) {
            free(v->el[i]);
        }
    }
    free(v->el);
}
/**
 * Function to add an element to the Vector
 *
 * @param v the Vector to add to
 * @param add the number of elements to add
 */
bool Vector_add(Vector *v, size_t add) {
    const size_t maximum = SIZE_MAX / sizeof(void *) - 1;
    if (!v || v->IMPL.len > maximum || add > maximum - v->IMPL.len) {
        errno = EOVERFLOW;
        return false;
    }
    size_t required = v->IMPL.len + add;
    if (v->el && required <= v->IMPL.cap) return true;
    size_t capacity = v->IMPL.cap > maximum / 2 ? maximum : v->IMPL.cap * 2;
    if (capacity < required) capacity = required;
    void **elements = realloc(v->el, (capacity + 1) * sizeof(void *));
    if (!elements) return false;
    v->el = elements;
    v->IMPL.cap = capacity;
    v->el[v->IMPL.len] = NULL;
    return true;
}

/**
 * Function to set the length of the Vector
 *
 * @param v the Vector to set the length of
 * @param len the new length of the Vector
 */
void Vector_set_len_no_free(Vector *v, size_t len) {
    if (!v || !v->el || len > v->IMPL.cap) return;
    v->IMPL.len = len;
    v->el[len] = NULL;
}
/**
 * Function to set the length of the Vector and free the memory of the removed elements
 *
 * @param v the Vector to set the length of
 * @param len the new length of the Vector
 */
void Vector_set_len(Vector *v, size_t len) {
    if (len < v->IMPL.len) {
        for (size_t i = len; i < v->IMPL.len; i++) {
            if (v->el[i]) {
                free(v->el[i]);
            }
        }
    }

    Vector_set_len_no_free(v, len);
}
/**
 * Function to get the length of the Vector
 *
 * @param v the Vector to get the length of
 * @return the length of the Vector
 */
size_t Vector_len(Vector v) {
    return v.IMPL.len;
}
/**
 * Function to get the element at a specific index in the Vector
 *
 * @param v the Vector to get the element from
 * @param idx the index of the element to get
 * @return the element at the specified index
 */
void Vector_min_cap(Vector *v) {
    size_t new_cap = v->IMPL.len;
    void **new_el = realloc(v->el, (new_cap + 1) * sizeof(void *));
    if (!new_el) {
        return;
    }
    v->el = new_el;
    v->IMPL.cap = new_cap;
}
/**
 * Function to get the element at a specific index in the Vector
 *
 * @param v the Vector to get the element from
 * @param idx the index of the element to get
 * @return the element at the specified index
 */
void Vector_sane_cap(Vector *v) {
    const size_t maximum = SIZE_MAX / sizeof(void *) - 1;
    size_t new_cap = v->IMPL.len > maximum / 2 ? maximum : v->IMPL.len * 2;
    void **new_el = realloc(v->el, (new_cap + 1) * sizeof(void *));
    if (!new_el) {
        return;
    }
    v->el = new_el;
    v->IMPL.cap = new_cap;
}
