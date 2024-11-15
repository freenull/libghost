#define _GNU_SOURCE
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <ghost/result.h>
#include <ghost/typesystem.h>

static gh_result retrieveaddr_fromlocalmemory(uintptr_t addr, size_t size, void * buffer, void * userdata) {
    (void)userdata;
    void * ptr = (void*)addr;
    memcpy(buffer, ptr, size);
    return GHR_OK;
}

typedef struct {
    gh_ts_arraypointer owner;
    gh_ts_arraypointer apples;
    gh_ts_arraypointer bananas;
} basket;

static void print_basket(basket * basket) {
    printf("[[ Basket %p ]]\n", (void*)basket);
    printf("Owner (%p): %.*s\n", (void*)basket->owner.ptr, (int)basket->owner.count, (char*)basket->owner.ptr);

    printf("Apples (%p):", (void*)basket->apples.ptr);
    int * apples = basket->apples.ptr;
    for (size_t i = 0; i < basket->apples.count; i++) {
        printf(" %d", apples[i]);
    }
    printf("\n");

    printf("Bananas (%p):", (void*)basket->bananas.ptr);
    float * bananas = basket->bananas.ptr;
    for (size_t i = 0; i < basket->bananas.count; i++) {
        printf(" %f", (double)bananas[i]);
    }
    printf("\n");
}

static bool eq_baskets(basket * lhs, basket * rhs) {
    if (lhs->owner.count != rhs->owner.count) return false;
    if (strncmp(lhs->owner.ptr, rhs->owner.ptr, lhs->owner.count) != 0) return false;

    if (lhs->apples.count != rhs->apples.count) return false;
    int * lhs_apples = lhs->apples.ptr;
    int * rhs_apples = lhs->apples.ptr;
    for (size_t i = 0; i < lhs->apples.count; i++) {
        if (lhs_apples[i] != rhs_apples[i]) return false;
    }

    if (lhs->bananas.count != rhs->bananas.count) return false;
    float * lhs_bananas = lhs->bananas.ptr;
    float * rhs_bananas = lhs->bananas.ptr;
    for (size_t i = 0; i < lhs->bananas.count; i++) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
        // RATIONALE: In this case, we do literally want a bit-for-bit comparison of floats.
        if (lhs_bananas[i] != rhs_bananas[i]) return false;
#pragma GCC diagnostic pop
    }

    return true;
}

int main(void) {
    gh_alloc alloc = gh_alloc_default();
    gh_ts ts;
    ghr_assert(gh_ts_ctor(&ts, &alloc));

    ghr_assert(gh_ts_loadbuiltin(&ts));

    ghr_assert(gh_ts_append(&ts, (gh_tstype) {
        .kind = GH_TSTYPE_STRUCT,
        .name = "basket",
        .t_struct = {
            .size = sizeof(basket),
            .field_count = 3,
            .fields = {
                { .name = "owner", .type = "char[]", .offset = offsetof(basket, owner) },
                { .name = "apples", .type = "int[]", .offset = offsetof(basket, apples) },
                { .name = "bananas", .type = "float[]", .offset = offsetof(basket, bananas) },
            }
        }
    }));

    ghr_assert(gh_ts_makederivatives(&ts));

    printf("Types in typesystem:\n");
    for (size_t i = 0; i < ts.size; i++) {
        gh_tstype * type = ts.buffer + i;
        printf("- %s\n", type->name);
    }


    size_t size = 0;
    ghr_assert(gh_ts_sizec(&ts, "basket", &size));
    assert(size == sizeof(basket));

    // attempt to copy contents of the primitive type from one memory location
    // to another using gh_ts_retrieve
    // (this will use a very primitive callback function that works inside local
    //  memory - the goal is to use process_vm_readv in the callback on a child
    //  process)

    gh_bytebuffer bytebuffer;
    ghr_assert(gh_bytebuffer_ctor(&bytebuffer, &alloc));

    int apples[5] = { 1, 2, 3, 4, 5 };
    float bananas[5] = { 11.1f, 22.2f, 33.3f, 44.4f, 55.5f };

    char owner[] = "freenull";

    basket my_basket = {
        .owner = { .ptr = owner, .count = sizeof(owner) },
        .apples = { .ptr = apples, .count = 5 },
        .bananas = { .ptr = bananas, .count = 5 }
    };

    printf("Original struct:\n");
    print_basket(&my_basket);

    basket * retrieved_basket;
    ghr_assert(gh_ts_retrievec(&ts, "basket", &bytebuffer, retrieveaddr_fromlocalmemory, NULL, (uintptr_t)&my_basket, (void**)&retrieved_basket));

    printf("Reinterpreted struct:\n");
    print_basket(retrieved_basket);

    assert(retrieved_basket->owner.ptr != my_basket.owner.ptr);
    assert(retrieved_basket->apples.ptr != my_basket.apples.ptr);
    assert(retrieved_basket->bananas.ptr != my_basket.bananas.ptr);
    assert(eq_baskets(&my_basket, retrieved_basket));

    ghr_assert(gh_bytebuffer_dtor(&bytebuffer));

    ghr_assert(gh_ts_dtor(&ts));
}
