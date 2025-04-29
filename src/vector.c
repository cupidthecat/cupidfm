// File: src/vector.c
// -----------------------
#include <stddef.h> // for size_t
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
    Vector v = {
        malloc((cap + 1) * sizeof(void *)),
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
    for (size_t i = 0; v->el[i]; i++)
        free(v->el[i]);
    free(v->el);
}
/**
 * Function to add an element to the Vector
 *
 * @param v the Vector to add to
 * @param add the number of elements to add
 */
void Vector_add(Vector *v, size_t add) {
    if (v->IMPL.len + add > v->IMPL.cap) {
        v->IMPL.cap = MAX(v->IMPL.cap * 2, v->IMPL.len + add);
        v->el = realloc(v->el, (v->IMPL.cap + 1) * sizeof(void *));
    }
}
/**
 * Function to set the length of the Vector
 *
 * @param v the Vector to set the length of
 * @param len the new length of the Vector
 */
void Vector_set_len_no_free(Vector *v, size_t len) {
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
    if (len < v->IMPL.len)
        for (size_t i = len; v->el[i]; i++)
            free(v->el[i]);

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
    v->IMPL.cap = v->IMPL.len;
    v->el = realloc(v->el, (v->IMPL.cap + 1) * sizeof(void *));
}
/**
 * Function to get the element at a specific index in the Vector
 *
 * @param v the Vector to get the element from
 * @param idx the index of the element to get
 * @return the element at the specified index
 */
void Vector_sane_cap(Vector *v) {
    v->IMPL.cap = v->IMPL.len * 2;
    v->el = realloc(v->el, (v->IMPL.cap + 1) * sizeof(void *));
}
/**
 * Function to reserve memory for a Vector
 *
 * @param v the Vector to reserve memory for
 * @param want the amount of memory to reserve
 */
void Vector_reserve(Vector *v, size_t want)
{
    if (v->IMPL.cap < want) {
        v->IMPL.cap = want;
        v->el = realloc(v->el, (v->IMPL.cap + 1) * sizeof(void *));
    }
}