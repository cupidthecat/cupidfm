#include "vector.h"

typedef struct {
    Vector v;
} VecStack;

VecStack VecStack_empty();
void VecStack_push(VecStack *v, void *el);
void *VecStack_pop(VecStack *v);
void *VecStack_peek(VecStack *v);
void VecStack_bye(VecStack *v);
