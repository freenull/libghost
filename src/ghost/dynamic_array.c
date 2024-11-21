#include <string.h>
#include <unistd.h>
#include <ghost/result.h>
#include <ghost/alloc.h>
#include <ghost/dynamic_array.h>

gh_result gh_dynamicarray_ctor(gh_dynamicarray da, const gh_dynamicarrayoptions * options) {
    *da.buffer = NULL;

    gh_result res = gh_alloc_new(da.alloc, da.buffer, options->element_size * options->initial_capacity);
    if (ghr_iserr(res)) return res;

    *da.capacity = options->initial_capacity;
    *da.size = 0;
    
    return GHR_OK;
}

gh_result gh_dynamicarray_dtorelements(gh_dynamicarray da, const gh_dynamicarrayoptions * options) {
    if (options->dtorelement_func == NULL) return GHR_OK;
    for (size_t i = 0; i < *da.size; i++) {
        gh_result res = options->dtorelement_func(
            da,
            (void*)(((char*)*da.buffer) + (i * options->element_size)),
            options->userdata
        );
        if (ghr_iserr(res)) return res;
    }
    return GHR_OK;
}

gh_result gh_dynamicarray_dtor(gh_dynamicarray da, const gh_dynamicarrayoptions * options) {
    gh_result res = gh_dynamicarray_dtorelements(da, options);
    if (ghr_iserr(res)) return res;

    res = gh_alloc_delete(da.alloc, da.buffer, *da.capacity * options->element_size);
    if (ghr_iserr(res)) return res;

    *da.buffer = NULL;
    *da.capacity = 0;
    *da.size = 0;
    return GHR_OK;
}

gh_result gh_dynamicarray_expandtofit(gh_dynamicarray da, const gh_dynamicarrayoptions * options, size_t amount) {
    if (*da.size + amount <= *da.capacity) return GHR_OK;

    size_t capacity = (*da.capacity);
    size_t target_capacity = capacity + amount;

    while (capacity < target_capacity) {
        capacity *= 2;
        if (options->max_capacity != GH_DYNAMICARRAY_NOMAXCAPACITY && capacity > options->max_capacity) return GHR_DYNAMICARRAY_MAXCAPACITY;
    }

    gh_result res = gh_alloc_resize(da.alloc, da.buffer, options->element_size * (*da.capacity), options->element_size * capacity);
    if (ghr_iserr(res)) return res;

    *da.capacity = capacity;

    return GHR_OK;
}

gh_result gh_dynamicarray_append(gh_dynamicarray da, const gh_dynamicarrayoptions * options, void * ptr) {
    gh_result res = gh_dynamicarray_expandtofit(da, options, 1);
    if (ghr_iserr(res)) return res;

    memcpy((char*)(*da.buffer) + (*da.size * options->element_size), ptr, options->element_size);
    *da.size += 1;

    return GHR_OK;
}

gh_result gh_dynamicarray_appendmany(gh_dynamicarray da, const gh_dynamicarrayoptions * options, void * ptr, size_t count) {
    gh_result res = gh_dynamicarray_expandtofit(da, options, count);
    if (ghr_iserr(res)) return res;

    memcpy((char*)(*da.buffer) + (*da.size * options->element_size), ptr, options->element_size * count);
    *da.size += count;

    return GHR_OK;
}

gh_result gh_dynamicarray_removeat(gh_dynamicarray da, const gh_dynamicarrayoptions * options, size_t index) {
    if (index >= *da.size) return GHR_DYNAMICARRAY_OUTOFBOUNDS;

    void * entry = ((char*)*da.buffer + options->element_size * index);
    if (options->dtorelement_func != NULL) {
        gh_result res = options->dtorelement_func(da, entry, options->userdata);
        if (ghr_iserr(res)) return res;
    }

    for (size_t i = index; i < *da.size - 1; i++) {
        void * cur_ptr = (char*)*da.buffer + options->element_size * i;
        void * next_ptr = (char*)cur_ptr + options->element_size;

        memcpy(cur_ptr, next_ptr, options->element_size);
    }

    *da.size -= 1;

    return GHR_OK;
}

gh_result gh_dynamicarray_getat(gh_dynamicarray da, const gh_dynamicarrayoptions * options, size_t index, void ** out_ptr) {
    if (index >= *da.size) return GHR_DYNAMICARRAY_OUTOFBOUNDS;

    void * entry = ((char*)*da.buffer + options->element_size * index);
    if (out_ptr != NULL) *out_ptr = entry;

    return GHR_OK;
}
