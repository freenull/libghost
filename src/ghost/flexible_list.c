#define _GNU_SOURCE
#include <string.h>
#include <ghost/flexible_list.h>

gh_result gh_flexiblelist_ctor(gh_flexiblelist vl, const gh_flexiblelistoptions * options) {
    *vl.buffer = NULL;

    gh_result res = gh_alloc_new(vl.alloc, vl.buffer, options->initial_capacity_bytes);
    if (ghr_iserr(res)) return res;

    *vl.capacity_bytes = options->initial_capacity_bytes;
    *vl.size_bytes = 0;
    
    return GHR_OK;
}

gh_result gh_flexiblelist_dtorelements(gh_flexiblelist vl, const gh_flexiblelistoptions * options) {
    if (options->dtorelement_func == NULL) return GHR_OK;

    void * elem_ptr = vl.buffer;
    if (vl.size_bytes == 0) return GHR_OK;

    gh_result res = GHR_OK;
    while (ghr_isok(res)) {
        gh_result dtor_res = options->dtorelement_func(elem_ptr, options->userdata);

        if (ghr_iserr(dtor_res)) return dtor_res;
        
        void * next_elem_ptr = NULL;
        res = gh_flexiblelist_next(vl, options, elem_ptr, &next_elem_ptr);
        elem_ptr = next_elem_ptr;
    }

    return GHR_OK;
}

gh_result gh_flexiblelist_dtor(gh_flexiblelist vl, const gh_flexiblelistoptions * options) {
    gh_result res = gh_flexiblelist_dtorelements(vl, options);
    if (ghr_iserr(res)) return res;

    res = gh_alloc_delete(vl.alloc, vl.buffer, *vl.capacity_bytes);
    if (ghr_iserr(res)) return res;

    *vl.buffer = NULL;
    *vl.capacity_bytes = 0;
    *vl.size_bytes = 0;
    return GHR_OK;
}

static gh_result flexiblelist_elementgetsize(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * element_ptr, gh_vasize * out_size) {
    (void)vl;

    void * va_size_ptr = ((char*)element_ptr + options->element_va_size_field_offset);

    gh_vasize va_size = 0;
    switch(options->element_va_size_field_type) {
    case GH_FLEXIBLELISTOPTIONS_32BITSIGNEDSIZE:
        va_size = (gh_vasize)*(int32_t *)va_size_ptr;
        break;
    case GH_FLEXIBLELISTOPTIONS_32BITUNSIGNEDSIZE:
        va_size = (gh_vasize)*(uint32_t *)va_size_ptr;
        break;
    case GH_FLEXIBLELISTOPTIONS_64BITSIGNEDSIZE:
        va_size = (gh_vasize)*(int64_t *)va_size_ptr;
        break;
    case GH_FLEXIBLELISTOPTIONS_64BITUNSIGNEDSIZE:
        va_size = (gh_vasize)*(uint64_t *)va_size_ptr;
        break;
    case GH_FLEXIBLELISTOPTIONS_SIZETSIZE:
        va_size = (gh_vasize)*(size_t *)va_size_ptr;
        break;
    default: return GHR_FLEXIBLELIST_INVALIDSIZETYPE;
    }

    if (out_size != NULL) {
        *out_size = options->element_header_size + va_size + options->element_va_size_suffix;
    }

    return GHR_OK;
}

static gh_result flexiblelist_elementsetsize(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * element_ptr, gh_vasize size) {
    (void)vl;

    void * va_size_ptr = ((char*)element_ptr + options->element_va_size_field_offset);

    switch(options->element_va_size_field_type) {
    case GH_FLEXIBLELISTOPTIONS_32BITSIGNEDSIZE:
        *(int32_t *)va_size_ptr = (int32_t)(size - options->element_va_size_suffix);
        break;
    case GH_FLEXIBLELISTOPTIONS_32BITUNSIGNEDSIZE:
        *(uint32_t *)va_size_ptr = (uint32_t)(size - options->element_va_size_suffix);
        break;
    case GH_FLEXIBLELISTOPTIONS_64BITSIGNEDSIZE:
        *(int64_t *)va_size_ptr = (int64_t)(size - options->element_va_size_suffix);
        break;
    case GH_FLEXIBLELISTOPTIONS_64BITUNSIGNEDSIZE:
        *(uint64_t *)va_size_ptr = (uint64_t)(size - options->element_va_size_suffix);
        break;
    case GH_FLEXIBLELISTOPTIONS_SIZETSIZE:
        *(size_t *)va_size_ptr = (size_t)(size - options->element_va_size_suffix);
        break;
    default: return GHR_FLEXIBLELIST_INVALIDSIZETYPE;
    }
    
    return GHR_OK;
}

static gh_result flexiblelist_next(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * element_ptr, bool incl_deleted, void ** out_next_ptr) {
    bool marked_as_delete = false;

    if (element_ptr == NULL) {
        if (*vl.size_bytes == 0) return GHR_FLEXIBLELIST_NONEXT;
        if (out_next_ptr != NULL) *out_next_ptr = *vl.buffer;
        return GHR_OK;
    }

    void * next_element_ptr = element_ptr;
    do {
        size_t va_size = 0;
        gh_result res = flexiblelist_elementgetsize(vl, options, next_element_ptr, &va_size);
        if (ghr_iserr(res)) return res;
        next_element_ptr = (char*)next_element_ptr + va_size;
        if ((uintptr_t)next_element_ptr - (uintptr_t)(*vl.buffer) >= *vl.size_bytes) {
            if (out_next_ptr != NULL) *out_next_ptr = NULL;
            return GHR_FLEXIBLELIST_NONEXT;
        }

        if (!incl_deleted && options->has_delete_flag) {
            marked_as_delete = *(bool *)((char*)next_element_ptr + options->delete_flag_field_offset);
        }
    } while (marked_as_delete);

    if (out_next_ptr != NULL) *out_next_ptr = next_element_ptr;

    return GHR_OK;
}

gh_result gh_flexiblelist_next(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * element_ptr, void ** out_next_ptr) {
    return flexiblelist_next(vl, options, element_ptr, false, out_next_ptr);
}

gh_result gh_flexiblelist_nextwithdeleted(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * element_ptr, void ** out_next_ptr) {
    return flexiblelist_next(vl, options, element_ptr, true, out_next_ptr);
}

gh_result gh_flexiblelist_expandtofit(gh_flexiblelist vl, const gh_flexiblelistoptions * options, size_t amount_bytes) {
    if (*vl.size_bytes + amount_bytes <= *vl.capacity_bytes) return GHR_OK;

    size_t capacity_bytes = (*vl.capacity_bytes);
    size_t target_capacity_bytes = capacity_bytes + amount_bytes;

    while (capacity_bytes < target_capacity_bytes) {
        capacity_bytes *= 2;
        if (options->max_capacity_bytes != GH_FLEXIBLELIST_NOMAXCAPACITY && capacity_bytes > options->max_capacity_bytes) return GHR_FLEXIBLELIST_MAXCAPACITY;
    }

    gh_result res = gh_alloc_resize(vl.alloc, vl.buffer, *vl.capacity_bytes, capacity_bytes);
    if (ghr_iserr(res)) return res;

    *vl.capacity_bytes = capacity_bytes;

    return GHR_OK;
}

gh_result gh_flexiblelist_append(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * ptr) {
    size_t element_size_bytes = 0;
    gh_result res = flexiblelist_elementgetsize(vl, options, ptr, &element_size_bytes);
    if (ghr_iserr(res)) return res;
    size_t prev_size_bytes = *vl.size_bytes;
    res = gh_flexiblelist_expandtofit(vl, options, element_size_bytes);
    if (ghr_iserr(res)) return res;

    void * new_ptr = (char*)(*vl.buffer) + prev_size_bytes;
    memcpy(new_ptr, ptr, element_size_bytes);

    *vl.size_bytes += element_size_bytes;
    return GHR_OK;
}

gh_result gh_flexiblelist_appendalloc(gh_flexiblelist vl, const gh_flexiblelistoptions * options, size_t va_size, void ** out_ptr) {
    size_t element_size_bytes = options->element_header_size + va_size;
    size_t prev_size_bytes = *vl.size_bytes;
    gh_result res = gh_flexiblelist_expandtofit(vl, options, element_size_bytes);
    if (ghr_iserr(res)) return res;

    void * new_ptr = (char*)(*vl.buffer) + prev_size_bytes;
    res = flexiblelist_elementsetsize(vl, options, new_ptr, va_size);

    size_t foo;
    flexiblelist_elementgetsize(vl, options, new_ptr, &foo);

    if(ghr_iserr(res)) {
        *vl.size_bytes = prev_size_bytes;
        return res;
    }

    if (options->has_delete_flag) {
        bool * delete_flag = (bool *)((char*)vl.buffer + options->delete_flag_field_offset);
        *delete_flag = false;
    }

    if (out_ptr != NULL) *out_ptr = new_ptr;
    *vl.size_bytes += element_size_bytes;
    
    return GHR_OK;
}

gh_result gh_flexiblelist_appendmany(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * ptr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        gh_result res = gh_flexiblelist_append(vl, options, ptr);
        if (ghr_iserr(res)) return res;
    }

    return GHR_OK;
}

gh_result gh_flexiblelist_remove(gh_flexiblelist vl, const gh_flexiblelistoptions * options, void * ptr) {
    (void)vl;
    if (!options->has_delete_flag) return GHR_FLEXIBLELIST_NODELETEFLAG;

    if (options->dtorelement_func != NULL) {
        gh_result res = options->dtorelement_func(ptr, options->userdata);
        if (ghr_iserr(res)) return res;
    }

    bool * delete_flag_ptr = (bool *)((char*)ptr + options->delete_flag_field_offset);
    if (*delete_flag_ptr) return GHR_FLEXIBLELIST_ALREADYDELETED;
    *delete_flag_ptr = true;

    return GHR_OK;
}
