#include <assert.h>
#include <ghost/alloc.h>
#include <ghost/dynamic_array.h>

int main(void) {
    gh_alloc alloc = gh_alloc_default();

    gh_dynamicarrayoptions opts = {
        .initial_capacity = 64,
        .max_capacity = GH_DYNAMICARRAY_NOMAXCAPACITY,
        .element_size = sizeof(int),

        .dtorelement_func = NULL,
        .userdata = NULL
    };

    int * buffer;
    size_t size;
    size_t capacity;

    gh_dynamicarray ary = {
        .alloc = &alloc,
        .buffer = (void**)&buffer,
        .size = &size,
        .capacity = &capacity
    };

    ghr_assert(gh_dynamicarray_ctor(ary, &opts));

    for (int i = 0; i < 192; i++) {
        int a = 192 - i;
        ghr_assert(gh_dynamicarray_append(ary, &opts, (void*)&a));
    }

    for (size_t i = 0; i < size; i++) {
        printf("%zu: %d\n", i, buffer[i]);
        assert(buffer[i] == (int)(192 - i));
    }

    assert(capacity == 256);

    ghr_assert(gh_dynamicarray_dtor(ary, &opts));

    assert(capacity == 0);
    assert(size == 0);
    assert(buffer == NULL);
}
