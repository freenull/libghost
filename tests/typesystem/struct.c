#define _GNU_SOURCE
#include <stddef.h>
#include <string.h>
#include <ghost/result.h>
#include <ghost/typesystem.h>

static gh_result retrieveaddr_fromlocalmemory(uintptr_t addr, size_t size, void * buffer, void * userdata) {
    (void)userdata;
    void * ptr = (void*)addr;
    memcpy(buffer, ptr, size);
    return GHR_OK;
}

typedef struct {
    float x;
    float y;
} vector2;

int main(void) {
    gh_alloc alloc = gh_alloc_default();
    gh_ts ts;
    ghr_assert(gh_ts_ctor(&ts, &alloc));

    ghr_assert(gh_ts_append(&ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "float",
        .t_primitive = {
            .size = sizeof(float)
        }
    }));

    ghr_assert(gh_ts_append(&ts, (gh_tstype) {
        .kind = GH_TSTYPE_STRUCT,
        .name = "vector2",
        .t_struct = {
            .size = sizeof(vector2),
            .field_count = 2,
            .fields = {
                { .name = "x", .type = "float", .offset = offsetof(vector2, x) },
                { .name = "y", .type = "float", .offset = offsetof(vector2, y) },
            }
        }
    }));


    size_t size = 0;
    ghr_assert(gh_ts_sizec(&ts, "vector2", &size));
    assert(size == sizeof(vector2));

    // attempt to copy contents of the primitive type from one memory location
    // to another using gh_ts_retrieve
    // (this will use a very primitive callback function that works inside local
    //  memory - the goal is to use process_vm_readv in the callback on a child
    //  process)

    gh_bytebuffer bytebuffer;
    ghr_assert(gh_bytebuffer_ctor(&bytebuffer, &alloc));

    vector2 value = { 12.34f, 56.78f };
    vector2 * retrieved_value;
    ghr_assert(gh_ts_retrievec(&ts, "vector2", &bytebuffer, retrieveaddr_fromlocalmemory, NULL, (uintptr_t)&value, (void**)&retrieved_value));

    printf("EXPECTED: %f,%f, RETRIEVED: %f,%f\n", (double)value.x, (double)value.y, (double)retrieved_value->x, (double)retrieved_value->y);

    ghr_assert(gh_bytebuffer_dtor(&bytebuffer));

    ghr_assert(gh_ts_dtor(&ts));
}
