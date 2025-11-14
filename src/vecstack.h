#ifndef VECSTACK_H
#define VECSTACK_H

#include "vector.h"  // the dynamic array of void*

typedef struct {
    Vector v;
} VecStack;

// "Constructor" to create a new empty stack
VecStack VecStack_empty();

// Push an element
void     VecStack_push(VecStack *stack, void *el);

// Pop (removes last element, returns it, or NULL if stack empty)
void    *VecStack_pop(VecStack *stack);

// Peek the top element (no remove)
void    *VecStack_peek(VecStack *stack);

// Frees underlying Vector
void     VecStack_bye(VecStack *stack);

#endif // VECSTACK_H
