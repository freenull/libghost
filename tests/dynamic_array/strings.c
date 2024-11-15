#include <assert.h>
#include <string.h>
#include <ghost/result.h>
#include <ghost/alloc.h>
#include <ghost/dynamic_array.h>

static gh_result free_str(void * obj, void * userdata) {
    (void)userdata;

    char ** str = (char**)obj;
    gh_alloc alloc = gh_alloc_default();

    return gh_alloc_delete(&alloc, (void**)str, strlen(*str) + 1);
}

static gh_result new_str(const char * s, char ** out_str) {
    size_t len = strlen(s);
    gh_alloc alloc = gh_alloc_default();

    gh_result res = gh_alloc_new(&alloc, (void**)out_str, len + 1);
    if (ghr_iserr(res)) return res;

    memcpy(*out_str, s, len + 1);
    return GHR_OK;
}

int main(void) {
    gh_alloc alloc = gh_alloc_default();

    gh_dynamicarrayoptions opts = {
        .initial_capacity = 4,
        .max_capacity = GH_DYNAMICARRAY_NOMAXCAPACITY,
        .element_size = sizeof(char *),

        .dtorelement_func = free_str,
        .userdata = NULL
    };

    char ** buffer;
    size_t size;
    size_t capacity;

    gh_dynamicarray ary = {
        .alloc = &alloc,
        .buffer = (void**)&buffer,
        .size = &size,
        .capacity = &capacity
    };

    ghr_assert(gh_dynamicarray_ctor(ary, &opts));

    for (int i = 0; i < 16; i++) {
        char * s = NULL;
        if (i % 2 == 0) ghr_assert(new_str("even", &s));
        else ghr_assert(new_str("odd", &s));
        ghr_assert(gh_dynamicarray_append(ary, &opts, (void*)&s));
    }

    for (size_t i = 0; i < size; i++) {
        printf("%zu: %s\n", i, buffer[i]);
        if (i % 2 == 0) assert(strcmp(buffer[i], "even") == 0);
        else assert(strcmp(buffer[i], "odd") == 0);
    }

    assert(size == 16);
    assert(capacity == 16);

    for (size_t i = 0; i < 16; i++) {
        size_t j = 16 - i - 1;

        if (j % 2 == 1) {
            ghr_assert(gh_dynamicarray_removeat(ary, &opts, j));
        }
    }

    for (size_t i = 0; i < size; i++) {
        printf("%zu: %s\n", i, buffer[i]);
        assert(strcmp(buffer[i], "even") == 0);
    }

    assert(size == 8);
    assert(capacity == 16);

    ghr_assert(gh_dynamicarray_dtor(ary, &opts));

    assert(capacity == 0);
    assert(size == 0);
    assert(buffer == NULL);
}
