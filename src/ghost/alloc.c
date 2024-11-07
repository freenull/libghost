#include <stdlib.h>
#include <ghost/alloc.h>

static gh_result default_allocator(void ** ptr, size_t old_size, size_t new_size, void * userdata) {
    (void)old_size;
    (void)userdata;

    if (ptr == NULL) {
        void * new_ptr = malloc(new_size);
        if (new_ptr == NULL) return ghr_errno(GHR_ALLOC_ALLOCFAIL);

        *ptr = new_ptr;
        return GHR_OK;
    }

    if (new_size == 0) {
        free(*ptr);
        return GHR_OK;
    }

    void * new_ptr = realloc(*ptr, new_size);
    if (new_ptr == NULL) return ghr_errno(GHR_ALLOC_REALLOCFAIL);
    *ptr = new_ptr;
    return GHR_OK;
}

gh_alloc gh_alloc_default(void) {
    return (gh_alloc){
        .func = default_allocator,
        .userdata = NULL
    };
}

gh_result gh_alloc_new(gh_alloc * alloc, void ** out_ptr, size_t size) {
    if (out_ptr == NULL) return GHR_ALLOC_INVALIDOUTERPTR;
    if (*out_ptr != NULL) return GHR_ALLOC_INVALIDPTR;
    if (size == 0) return GHR_ALLOC_INVALIDSIZE;

    return alloc->func(out_ptr, 0, size, alloc->userdata);
}

gh_result gh_alloc_delete(gh_alloc * alloc, void ** ptr, size_t old_size) {
    if (ptr == NULL) return GHR_ALLOC_INVALIDOUTERPTR;
    if (*ptr == NULL) return GHR_ALLOC_INVALIDPTR;
    if (old_size == 0) return GHR_ALLOC_INVALIDSIZE;

    return alloc->func(ptr, old_size, 0, alloc->userdata);
}

gh_result gh_alloc_resize(gh_alloc * alloc, void ** out_ptr, size_t old_size, size_t new_size) {
    if (out_ptr == NULL) return GHR_ALLOC_INVALIDOUTERPTR;
    if (*out_ptr == NULL) return GHR_ALLOC_INVALIDPTR;
    if (old_size == 0) return GHR_ALLOC_INVALIDSIZE;
    if (new_size == 0) return GHR_ALLOC_INVALIDSIZE;

    return alloc->func(out_ptr, old_size, new_size, alloc->userdata);
}
