#ifndef VECTOR_H
#define VECTOR_H

#include <stddef.h>

struct VectorImpl {
    size_t cap;
    size_t len;
};

typedef struct {
    void **el;
    struct VectorImpl _PRIVATE_dont_access_PRIVATE_;
} Vector;


Vector Vector_new(size_t cap);
void Vector_bye(Vector *v);
void Vector_add(Vector *v, size_t add);
void Vector_set_len_no_free(Vector *v, size_t len);
void Vector_set_len(Vector *v, size_t len);
size_t Vector_len(Vector v);
void Vector_min_cap(Vector *v);
void Vector_sane_cap(Vector *v);


#endif

