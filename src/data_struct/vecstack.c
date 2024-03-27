#include "vecstack.h"

VecStack VecStack_empty() {
    VecStack r = {Vector_new(0)};
    return r;
}

void VecStack_push(VecStack *v, void *el) {
    Vector_add(&v->v, 1);
    v->v.el[Vector_len(v->v)] = el;
    Vector_set_len(&v->v, Vector_len(v->v) + 1);
}
void *VecStack_pop(VecStack *v) {
    if (Vector_len(v->v) < 1)
        return nullptr;
    void *r = v->v.el[Vector_len(v->v) - 1];
    Vector_set_len_no_free(&v->v, Vector_len(v->v) - 1);
    return r;
}

void *VecStack_peek(VecStack *v) {
    if (Vector_len(v->v) < 1)
        return nullptr;
    return v->v.el[Vector_len(v->v) - 1];
}

void VecStack_bye(VecStack *v) {
    Vector_bye(&v->v);
}
