#ifndef GHOST_ALLOC_H
#define GHOST_ALLOC_H

#include <stdlib.h>
#include <ghost/result.h>

typedef gh_result gh_alloc_func(void ** ptr, size_t old_size, size_t new_size, void * userdata);

typedef struct {
    gh_alloc_func * func;
    void * userdata;
} gh_alloc;

__attribute__((always_inline))
static inline void gh_alloc_ctor(gh_alloc * alloc, gh_alloc_func func, void * userdata) {
    alloc->func = func;
    alloc->userdata = userdata;
}

gh_result gh_alloc_dtor(gh_alloc * alloc);

gh_alloc gh_alloc_default(void);
gh_result gh_alloc_new(gh_alloc * alloc, void ** out_ptr, size_t size);
gh_result gh_alloc_delete(gh_alloc * alloc, void ** ptr, size_t old_size);
gh_result gh_alloc_resize(gh_alloc * alloc, void ** out_ptr, size_t old_size, size_t new_size);

#endif
