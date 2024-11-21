#ifndef GHOST_DYNAMICARRAY_H
#define GHOST_DYNAMICARRAY_H

#define GH_DYNAMICARRAY_NOMAXCAPACITY 0
#define GH_DYNAMICARRAY(array) ((gh_dynamicarray) { \
            .alloc = (array)->alloc, \
            .buffer = (void**)&(array)->buffer, \
            .capacity = &(array)->capacity, \
            .size = &(array)->size, \
        })
#define GH_DYNAMICARRAY_WITHALLOC(array, alloc_) ((gh_dynamicarray) { \
            .alloc = (alloc_), \
            .buffer = (void**)&(array)->buffer, \
            .capacity = &(array)->capacity, \
            .size = &(array)->size, \
        })

#include <stdlib.h>
#include <ghost/result.h>
#include <ghost/alloc.h>

typedef struct {
    gh_alloc * alloc;
    void ** buffer;
    size_t * capacity;
    size_t * size;
} gh_dynamicarray;

typedef gh_result gh_dynamicarray_dtorelement_func(gh_dynamicarray da, void * elem, void * userdata);

typedef struct {
    size_t initial_capacity;
    size_t max_capacity;
    size_t element_size;

    gh_dynamicarray_dtorelement_func * dtorelement_func;
    void * userdata;
} gh_dynamicarrayoptions;

gh_result gh_dynamicarray_ctor(gh_dynamicarray da, const gh_dynamicarrayoptions * options);
gh_result gh_dynamicarray_dtorelements(gh_dynamicarray da, const gh_dynamicarrayoptions * options);
gh_result gh_dynamicarray_dtor(gh_dynamicarray da, const gh_dynamicarrayoptions * options);
gh_result gh_dynamicarray_expandtofit(gh_dynamicarray da, const gh_dynamicarrayoptions * options, size_t amount);
gh_result gh_dynamicarray_append(gh_dynamicarray da, const gh_dynamicarrayoptions * options, void * ptr);
gh_result gh_dynamicarray_appendmany(gh_dynamicarray da, const gh_dynamicarrayoptions * options, void * ptr, size_t count);
gh_result gh_dynamicarray_removeat(gh_dynamicarray da, const gh_dynamicarrayoptions * options, size_t index);
gh_result gh_dynamicarray_getat(gh_dynamicarray da, const gh_dynamicarrayoptions * options, size_t index, void ** out_ptr);

#endif
