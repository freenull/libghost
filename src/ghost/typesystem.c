#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include <ghost/result.h>
#include <ghost/typesystem.h>
#include <ghost/dynamic_array.h>
#include <ghost/strings.h>

static const gh_dynamicarrayoptions ts_daopts = {
    .initial_capacity = GH_TSLIST_INITIALCAPACITY,
    .max_capacity = GH_TSLIST_MAXCAPACITY,
    .element_size = sizeof(gh_tstype),
   
    .dtorelement_func = NULL,
    .userdata = NULL
};

gh_result gh_ts_ctor(gh_ts * ts, gh_alloc * alloc) {
    ts->alloc = alloc;
    return gh_dynamicarray_ctor(GH_DYNAMICARRAY(ts), &ts_daopts);
}

gh_result gh_ts_findbyname(gh_ts * ts, const char * name, size_t name_len, const gh_tstype ** const out_type) {
    if (name_len == 0) return GHR_TS_EMPTYNAME;

    for (size_t i = 0; i < ts->size; i++) {
        gh_tstype * type = ts->buffer + i;
        size_t type_name_len = strlen(type->name);
        if (type_name_len == name_len && strncmp(type->name, name, name_len) == 0) {
            if (out_type != NULL) *out_type = type;
            return GHR_OK;
        }
    }

    return GHR_TS_MISSINGTYPE;
}

gh_result gh_ts_findbynamec(gh_ts * ts, const char * name, const gh_tstype ** const out_type) {
    return gh_ts_findbyname(ts, name, strlen(name), out_type);
}

gh_result gh_ts_append(gh_ts * ts, gh_tstype type) {
    gh_result res = gh_ts_findbyname(ts, type.name, strlen(type.name), NULL);
    if (ghr_is(res, GHR_OK)) return GHR_TS_DUPLICATETYPE;
    if (ghr_isnot(res, GHR_TS_MISSINGTYPE)) return res;
    if (type.name[0] == '\0') return GHR_TS_EMPTYNAME;

    return gh_dynamicarray_append(GH_DYNAMICARRAY(ts), &ts_daopts, &type);
}

gh_result gh_ts_remove(gh_ts * ts, const char * name, size_t name_len) {
    for (size_t i = 0; i < ts->size; i++) {
        gh_tstype * type = ts->buffer + i;
        size_t type_name_len = strlen(type->name);
        if (type_name_len == name_len && strncmp(type->name, name, name_len) == 0) {
            gh_dynamicarray_removeat(GH_DYNAMICARRAY(ts), &ts_daopts, i);
            return GHR_OK;
        }
    }

    return GHR_TS_MISSINGTYPE;
}

gh_result gh_ts_sizet(gh_ts * ts, const gh_tstype * type, size_t * out_size) {
    switch(type->kind) {
    case GH_TSTYPE_INVALID: return GHR_TS_INVALIDTYPE;
    case GH_TSTYPE_PRIMITIVE:
        *out_size = type->t_primitive.size;
        break;
    case GH_TSTYPE_STATICARRAY: {
        size_t element_size = 0;
        gh_result res = gh_ts_sizec(ts, type->t_staticarray.element_type, &element_size);
        if (ghr_iserr(res)) return res;
        *out_size = type->t_staticarray.count * element_size;
        break;
    }
    case GH_TSTYPE_STRUCT:
        *out_size = type->t_struct.size;
        break;
    case GH_TSTYPE_TAGGEDUNION:
        *out_size = sizeof(gh_ts_taggedunion) + type->t_taggedunion.union_size;
        break;
    case GH_TSTYPE_POINTER:
        *out_size = sizeof(void*);
        break;
    case GH_TSTYPE_ARRAYPOINTER:
        *out_size = sizeof(void*);
        break;
    default: return GHR_TS_INVALIDTYPE;
    }

    return GHR_OK;
}

gh_result gh_ts_size(gh_ts * ts, const char * name, size_t name_len, size_t * out_size) {
    const gh_tstype * type;
    gh_result res = gh_ts_findbyname(ts, name, name_len, &type);
    if (ghr_iserr(res)) return res;

    return gh_ts_sizet(ts, type, out_size);
}

gh_result gh_ts_sizec(gh_ts * ts, const char * name, size_t * out_size) {
    return gh_ts_size(ts, name, strlen(name), out_size);
}

gh_result gh_ts_dtor(gh_ts * ts) {
    return gh_dynamicarray_dtor(GH_DYNAMICARRAY(ts), &ts_daopts);
}

static gh_result ts_retrieve(gh_ts * ts, const gh_tstype * type, gh_bytebuffer * buffer, void * target, size_t size, gh_ts_retrieveaddr_func * callback, void * userdata, uintptr_t addr) {
    gh_result res = GHR_OK;

    switch(type->kind) {
    case GH_TSTYPE_INVALID: return GHR_TS_INVALIDTYPE;
    case GH_TSTYPE_PRIMITIVE: {
        res = callback(addr, size, target, userdata);
        if (ghr_iserr(res)) return res;

        break;
    }

    case GH_TSTYPE_STATICARRAY:
        res = callback(addr, size, target, userdata);
        if (ghr_iserr(res)) return res;

        break;

    case GH_TSTYPE_STRUCT: {
#if defined(GH_TRADEOFF_WHOLESALESTRUCTCOPY)
        res = callback(addr, size, target, userdata);
        if (ghr_iserr(res)) return res;
#endif

        for (size_t i = 0; i < type->t_struct.field_count; i++) {
            const gh_tstype_struct_field * field = type->t_struct.fields + i;
            const gh_tstype * field_type = NULL;
            res = gh_ts_findbynamec(ts, field->type, &field_type);
            if (ghr_iserr(res)) return res;

            size_t field_size = 0;
            res = gh_ts_sizet(ts, field_type, &field_size);
            if (ghr_iserr(res)) return res;

#if defined(GH_TRADEOFF_WHOLESALESTRUCTCOPY)
            if (field_type->kind == GH_TSTYPE_POINTER || field_type->kind == GH_TSTYPE_ARRAYPOINTER) {
#endif
                res = ts_retrieve(ts, field_type, buffer, (char*)target + field->offset, field_size, callback, userdata, addr + (uintptr_t)field->offset);
                if (ghr_iserr(res)) return res;
#if defined(GH_TRADEOFF_WHOLESALESTRUCTCOPY)
            }
#endif
        }
        break;
    }

    case GH_TSTYPE_TAGGEDUNION:
        res = callback(addr, size, target, userdata);
        if (ghr_iserr(res)) return res;

        break;

    case GH_TSTYPE_POINTER: {
        uintptr_t element_addr;
        res = callback(addr, sizeof(uintptr_t), &element_addr, userdata);
        if (ghr_iserr(res)) return res;

        if (element_addr == (uintptr_t)NULL) {
            *(uintptr_t*)target = 0;
        } else {
            const gh_tstype * element_type;
            res = gh_ts_findbynamec(ts, type->t_pointer.element_type, &element_type);
            if (ghr_iserr(res)) return res;

            size_t element_size = 0;
            res = gh_ts_sizet(ts, element_type, &element_size);
            if (ghr_iserr(res)) return res;

            char * element_target = NULL;
            res = gh_bytebuffer_expand(buffer, element_size, &element_target);
            if (ghr_iserr(res)) return res;

            res = ts_retrieve(ts, element_type, buffer, element_target, element_size, callback, userdata, element_addr);
            if (ghr_iserr(res)) return res;

            *(uintptr_t*)target = (uintptr_t)element_target;
        }

        break;
    }

    case GH_TSTYPE_ARRAYPOINTER: {
        gh_ts_arraypointer element_slice;
        res = callback(addr, sizeof(gh_ts_arraypointer), &element_slice, userdata);
        if (ghr_iserr(res)) return res;

        size_t array_count = 0;
        char * array_target = NULL;

        if (element_slice.ptr != NULL && element_slice.count != 0) {
            array_count = element_slice.count;

            const gh_tstype * element_type;
            res = gh_ts_findbynamec(ts, type->t_pointer.element_type, &element_type);
            if (ghr_iserr(res)) return res;

            size_t element_size = 0;
            res = gh_ts_sizet(ts, element_type, &element_size);
            if (ghr_iserr(res)) return res;

            size_t array_size = element_size * element_slice.count;

            res = gh_bytebuffer_expand(buffer, array_size, &array_target);
            if (ghr_iserr(res)) return res;

            res = ts_retrieve(ts, element_type, buffer, array_target, array_size, callback, userdata, (uintptr_t)element_slice.ptr);
            if (ghr_iserr(res)) return res;
        }

        *(gh_ts_arraypointer *)target = (gh_ts_arraypointer) {
            .count = array_count,
            .ptr = (void*)array_target
        };

        break;
    }

    default: return GHR_TS_INVALIDTYPE;
    }

    return res;
}

gh_result gh_ts_retrievet(gh_ts * ts, const gh_tstype * type, gh_bytebuffer * buffer, gh_ts_retrieveaddr_func * callback, void * userdata, uintptr_t addr, void ** out_ptr) {
    gh_result res = GHR_OK;

    size_t size = 0;
    res = gh_ts_sizet(ts, type, &size);
    if (ghr_iserr(res)) return res;

    char * target = NULL;
    res = gh_bytebuffer_expand(buffer, size, &target);
    if (ghr_iserr(res)) return res;

    res = ts_retrieve(ts, type, buffer, target, size, callback, userdata, addr);
    if (ghr_iserr(res)) return res;

    if (out_ptr != NULL) *out_ptr = buffer->buffer;
    return GHR_OK;
}

gh_result gh_ts_retrieve(gh_ts * ts, const char * name, size_t name_len, gh_bytebuffer * buffer, gh_ts_retrieveaddr_func * callback, void * userdata, uintptr_t addr, void ** out_ptr) {
    const gh_tstype * type;
    gh_result res = gh_ts_findbyname(ts, name, name_len, &type);
    if (ghr_iserr(res)) return res;

    return gh_ts_retrievet(ts, type, buffer, callback, userdata, addr, out_ptr);
}

gh_result gh_ts_retrievec(gh_ts * ts, const char * name, gh_bytebuffer * buffer, gh_ts_retrieveaddr_func * callback, void * userdata, uintptr_t addr, void ** out_ptr) {
    return gh_ts_retrieve(ts, name, strlen(name), buffer, callback, userdata, addr, out_ptr);
}

static gh_result ts_makederivatives_fromtypename(gh_ts * ts, const char * name) {
    const gh_tstype * existing_type = NULL;
    gh_result res = gh_ts_findbynamec(ts, name, &existing_type);
    if (ghr_isok(res)) {
        return GHR_OK;
    }
    if (ghr_isnot(res, GHR_TS_MISSINGTYPE)) return res;

    size_t name_len = strlen(name);

    char buf[name_len + 1];
    strncpy(buf, name, name_len + 1);

    gh_str s = gh_str_fromc(buf, sizeof(buf));

    // if ends with '*', trim and make pointer
    if (gh_str_endswith(&s, "*", 1)) {
        gh_str_trimrightn(&s, 1);
        gh_str_trimrightchar(&s, ' ');
        gh_str_insertnull(&s);

        gh_tstype type = {0};
        type.kind = GH_TSTYPE_POINTER;
        strncpy(type.name, name, name_len + 1);
        strncpy(type.t_pointer.element_type, s.buffer, s.size);

        res = gh_ts_append(ts, type);
        if (ghr_iserr(res)) return res;
    } else if (gh_str_endswith(&s, "[]", 2)) {
        gh_str_trimrightn(&s, 2);
        gh_str_trimrightchar(&s, ' ');
        gh_str_insertnull(&s);

        gh_tstype type = {0};
        type.kind = GH_TSTYPE_ARRAYPOINTER;
        strncpy(type.name, name, name_len + 1);
        strncpy(type.t_arraypointer.element_type, s.buffer, s.size);

        res = gh_ts_append(ts, type);
        if (ghr_iserr(res)) return res;
    } else if (gh_str_endswith(&s, "]", 1)) {

        size_t lbrace_pos = 0;
        if (!gh_str_rpos(&s, '[', &lbrace_pos)) return GHR_TS_MISSINGTYPE;
        if (lbrace_pos + 1 >= s.size) return GHR_TS_MISSINGTYPE;

        gh_str_trimrightn(&s, 1);
        gh_str_insertnull(&s);
        const char * len_str = s.buffer + lbrace_pos + 1;
        size_t len_str_len = s.size - 1 - lbrace_pos - 1;

        uintmax_t len_uintmax = strtoumax(len_str, NULL, 10);
        if (len_uintmax == UINTMAX_MAX && errno == ERANGE) return GHR_TS_MISSINGTYPE;

        gh_str_trimrightn(&s, 1 + len_str_len + 1);
        gh_str_trimrightchar(&s, ' ');
        gh_str_insertnull(&s);

        if (len_uintmax > SIZE_MAX) return GHR_TS_ARRAYTOOBIG;
        size_t len = (size_t)len_uintmax;

        gh_tstype type = {0};
        type.kind = GH_TSTYPE_STATICARRAY;
        strncpy(type.name, name, name_len + 1);
        strncpy(type.t_staticarray.element_type, s.buffer, s.size);
        type.t_staticarray.count = len;

        res = gh_ts_append(ts, type);
        if (ghr_iserr(res)) return res;
    } else {
        return GHR_TS_MISSINGTYPE;
    }

    return ts_makederivatives_fromtypename(ts, s.buffer);
}

static gh_result ts_makederivatives(gh_ts * ts, gh_tstype * type) {
    gh_result res = GHR_OK;
    if (ghr_iserr(res)) return res;

    switch(type->kind) {
    case GH_TSTYPE_INVALID: return GHR_TS_INVALIDTYPE;
    case GH_TSTYPE_PRIMITIVE: break;
    case GH_TSTYPE_STATICARRAY:
        return ts_makederivatives_fromtypename(ts, type->t_staticarray.element_type);
    case GH_TSTYPE_STRUCT: {
        for (size_t i = 0; i < type->t_struct.field_count; i++) {
            gh_tstype_struct_field * field = type->t_struct.fields + i;
            res = ts_makederivatives_fromtypename(ts, field->type);
            if (ghr_iserr(res)) return res;
        }
        break;
    }
    case GH_TSTYPE_TAGGEDUNION: {
        for (size_t i = 0; i < type->t_taggedunion.field_count; i++) {
            gh_tstype_taggedunion_field * field = type->t_taggedunion.fields + i;
            res = ts_makederivatives_fromtypename(ts, field->type);
            if (ghr_iserr(res)) return res;
        }
        break;
    }
    case GH_TSTYPE_POINTER: 
        return ts_makederivatives_fromtypename(ts, type->t_pointer.element_type);
    case GH_TSTYPE_ARRAYPOINTER:
        return ts_makederivatives_fromtypename(ts, type->t_arraypointer.element_type);
        
    default: return GHR_TS_INVALIDTYPE;
    }

    return GHR_OK;
}

gh_result gh_ts_makederivatives(gh_ts * ts) {
    gh_result res = GHR_OK;

    for (size_t i = 0; i < ts->size; i++) {
        res = ts_makederivatives(ts, ts->buffer + i);
        if (ghr_iserr(res)) return res;
    }

    return GHR_OK;
}

gh_result gh_ts_loadbuiltin(gh_ts * ts) {
    gh_result res = GHR_OK;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "char",
        .t_primitive = { .size = sizeof(char) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "signed char",
        .t_primitive = { .size = sizeof(signed char) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "unsigned char",
        .t_primitive = { .size = sizeof(unsigned char) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "short",
        .t_primitive = { .size = sizeof(short) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "unsigned short",
        .t_primitive = { .size = sizeof(unsigned short) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "int",
        .t_primitive = { .size = sizeof(int) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "unsigned int",
        .t_primitive = { .size = sizeof(unsigned int) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "long",
        .t_primitive = { .size = sizeof(long) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "unsigned long",
        .t_primitive = { .size = sizeof(unsigned long) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "long long",
        .t_primitive = { .size = sizeof(long long) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "unsigned long long",
        .t_primitive = { .size = sizeof(unsigned long long) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "float",
        .t_primitive = { .size = sizeof(float) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "double",
        .t_primitive = { .size = sizeof(double) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "long double",
        .t_primitive = { .size = sizeof(long double) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "int8_t",
        .t_primitive = { .size = sizeof(int8_t) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "uint8_t",
        .t_primitive = { .size = sizeof(uint8_t) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "int16_t",
        .t_primitive = { .size = sizeof(int16_t) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "uint16_t",
        .t_primitive = { .size = sizeof(uint16_t) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "int32_t",
        .t_primitive = { .size = sizeof(int32_t) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "uint32_t",
        .t_primitive = { .size = sizeof(uint32_t) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "int64_t",
        .t_primitive = { .size = sizeof(int64_t) }
    });
    if (ghr_iserr(res)) return res;

    res = gh_ts_append(ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "uint64_t",
        .t_primitive = { .size = sizeof(uint64_t) }
    });
    if (ghr_iserr(res)) return res;

    return GHR_OK;
}
