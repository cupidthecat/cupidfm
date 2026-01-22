// vecstack.c

#include "vecstack.h"
#include <stdlib.h> // For malloc, realloc, free
#include <stdio.h>  // For fprintf, stderr

#define INITIAL_CAPACITY 10

/**
 * Function to create an empty VecStack
 *
 * @return an empty VecStack
 */
VecStack VecStack_empty() {
    // Pre-allocate capacity for 10 elements to reduce reallocations
    VecStack r = {Vector_new(INITIAL_CAPACITY)};
    return r;
}

/**
 * Function to push an element onto the VecStack
 *
 * @param v the VecStack to push onto
 * @param el the element to push
 */
void VecStack_push(VecStack *v, void *el) {
    // Cache length to avoid multiple function calls
    size_t len = Vector_len(v->v);
    Vector_add(&v->v, 1);
    v->v.el[len] = el;
    // Use set_len_no_free since we're growing (no elements to free)
    Vector_set_len_no_free(&v->v, len + 1);
}

/**
 * Function to pop an element from the VecStack
 *
 * @param v the VecStack to pop from
 * @return the popped element
 */
void *VecStack_pop(VecStack *v) {
    // Cache length to avoid multiple function calls
    size_t len = Vector_len(v->v);
    if (len < 1)
        return NULL;
    void *r = v->v.el[len - 1];
    Vector_set_len_no_free(&v->v, len - 1);
    return r;
}

/**
 * Function to free the memory allocated for a VecStack
 *
 * @param v the VecStack to free
 */
void VecStack_bye(VecStack *v) {
    Vector_bye(&v->v);
}

/**
 * Function to peek at the top element of the VecStack
 *
 * @param v the VecStack to peek at
 * @return the top element
 */
void *VecStack_peek(VecStack *v) {
    // Cache length to avoid multiple function calls
    size_t len = Vector_len(v->v);
    if (len < 1)
        return NULL;
    return v->v.el[len - 1];
}