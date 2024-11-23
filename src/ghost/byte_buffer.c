#include <string.h>
#include <ghost/result.h>
#include <ghost/alloc.h>
#include <ghost/dynamic_array.h>
#include <ghost/byte_buffer.h>
#include <valgrind/memcheck.h>
#include <valgrind/valgrind.h>

static const gh_dynamicarrayoptions bb_daopts = {
    .initial_capacity = GH_BYTEBUFFER_INITIALCAPACITY,
    .max_capacity = GH_BYTEBUFFER_MAXCAPACITY,
    .element_size = sizeof(char),
   
    .dtorelement_func = NULL,
    .userdata = NULL
};

gh_result gh_bytebuffer_ctor(gh_bytebuffer * buffer, gh_alloc * alloc) {
    buffer->alloc = alloc;
    gh_result res = gh_dynamicarray_ctor(GH_DYNAMICARRAY(buffer), &bb_daopts);

#if !defined(GH_TRADEOFF_UNINITBYTEBUFFER)
    if (ghr_isok(res)) memset(buffer->buffer, 0, buffer->capacity);
#endif

    return res;
}

gh_result gh_bytebuffer_append(gh_bytebuffer * buffer, char * bytes, size_t size) {
    char * target = NULL;
    gh_result res = gh_bytebuffer_expand(buffer, size, &target);
    if (ghr_iserr(res)) return res;

    memcpy(target, bytes, size);
    return GHR_OK;
}

gh_result gh_bytebuffer_expand(gh_bytebuffer * buffer, size_t size, char ** out_ptr) {
    size_t prev_capacity = buffer->capacity;
    gh_result res = gh_dynamicarray_expandtofit(GH_DYNAMICARRAY(buffer), &bb_daopts, size);
    if (ghr_iserr(res)) return res;

    *out_ptr = buffer->buffer + buffer->size * bb_daopts.element_size;
    buffer->size += size;

#if !defined(GH_TRADEOFF_UNINITBYTEBUFFER)
    if (buffer->capacity > prev_capacity) {
        memset(buffer->buffer + bb_daopts.element_size * prev_capacity, 0, buffer->capacity - prev_capacity);
    }
#endif

    return GHR_OK;
}

gh_result gh_bytebuffer_clear(gh_bytebuffer * buffer) {
#if !defined(GH_TRADEOFF_UNINITBYTEBUFFER)
    memset(buffer->buffer, 0, buffer->size);
#endif

    buffer->size = 0;
    return GHR_OK;
}

gh_result gh_bytebuffer_dtor(gh_bytebuffer * buffer) {
    return gh_dynamicarray_dtor(GH_DYNAMICARRAY(buffer), &bb_daopts);
}
