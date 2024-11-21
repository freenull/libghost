#ifndef GHOST_FLEXIBLELIST_H
#define GHOST_FLEXIBLELIST_H

#define GH_FLEXIBLELIST_NOMAXCAPACITY 0
#define GH_FLEXIBLELIST(array) ((gh_flexiblelist) { \
            .alloc = (array)->alloc, \
            .buffer = (void**)&(array)->buffer, \
            .capacity_bytes = &(array)->capacity_bytes, \
            .size_bytes = &(array)->size_bytes, \
        })
#define GH_FLEXIBLELIST_WITHALLOC(array, alloc_) ((gh_flexiblelist) { \
            .alloc = (alloc_), \
            .buffer = (void**)&(array)->buffer, \
            .capacity_bytes = &(array)->capacity_bytes, \
            .size_bytes = &(array)->size_bytes, \
        })

#define _GNU_SOURCE
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ghost/result.h>
#include <ghost/alloc.h>

typedef struct {
    gh_alloc * alloc;
    void ** buffer;
    size_t * capacity_bytes;
    size_t * size_bytes;
} gh_flexiblelist;

typedef gh_result gh_flexiblelist_dtorelement_func(void * elem, void * userdata);

typedef enum {
    GH_FLEXIBLELISTOPTIONS_32BITSIGNEDSIZE,
    GH_FLEXIBLELISTOPTIONS_32BITUNSIGNEDSIZE,
    GH_FLEXIBLELISTOPTIONS_64BITSIGNEDSIZE,
    GH_FLEXIBLELISTOPTIONS_64BITUNSIGNEDSIZE,
    GH_FLEXIBLELISTOPTIONS_SIZETSIZE,
} gh_flexiblelistoptions_sizefieldtype;


typedef uint64_t gh_vasize;

typedef struct {
    size_t initial_capacity_bytes;
    size_t max_capacity_bytes;
    size_t element_header_size;

    gh_flexiblelistoptions_sizefieldtype element_va_size_field_type;
    off_t element_va_size_field_offset;
    gh_vasize element_va_size_suffix;

    bool has_delete_flag;
    off_t delete_flag_field_offset;

    gh_flexiblelist_dtorelement_func * dtorelement_func;
    void * userdata;
} gh_flexiblelistoptions;

gh_result gh_flexiblelist_ctor(gh_flexiblelist vl, const gh_flexiblelistoptions * options);
gh_result gh_flexiblelist_dtorelements(gh_flexiblelist vl, const gh_flexiblelistoptions * options);
gh_result gh_flexiblelist_dtor(gh_flexiblelist vl, const gh_flexiblelistoptions * options);
gh_result gh_flexiblelist_expandtofit(gh_flexiblelist vl, const gh_flexiblelistoptions * options, size_t amount);
gh_result gh_flexiblelist_next(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * element_ptr, void ** out_next_ptr);
gh_result gh_flexiblelist_nextwithdeleted(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * element_ptr, void ** out_next_ptr);
gh_result gh_flexiblelist_append(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * ptr);
gh_result gh_flexiblelist_appendalloc(gh_flexiblelist vl, const gh_flexiblelistoptions * options, gh_vasize va_size, void ** out_ptr);
gh_result gh_flexiblelist_appendmany(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * ptr, size_t count);
gh_result gh_flexiblelist_remove(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * ptr);

#endif
