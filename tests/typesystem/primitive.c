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

int main(void) {
    gh_alloc alloc = gh_alloc_default();
    gh_ts ts;
    ghr_assert(gh_ts_ctor(&ts, &alloc));

    ghr_assert(gh_ts_append(&ts, (gh_tstype) {
        .kind = GH_TSTYPE_PRIMITIVE,
        .name = "int",
        .t_primitive = {
            .size = sizeof(int)
        }
    }));

    const gh_tstype * type;
    ghr_assert(gh_ts_findbynamec(&ts, "int", &type));

    assert(strcmp(type->name, "int") == 0);
    assert(type->kind == GH_TSTYPE_PRIMITIVE);
    assert(type->t_primitive.size == sizeof(int));

    size_t size = 0;
    ghr_assert(gh_ts_sizec(&ts, "int", &size));
    assert(size == sizeof(int));

    // attempt to copy contents of the primitive type from one memory location
    // to another using gh_ts_retrieve
    // (this will use a very primitive callback function that works inside local
    //  memory - the goal is to use process_vm_readv in the callback on a child
    //  process)

    gh_bytebuffer bytebuffer;
    ghr_assert(gh_bytebuffer_ctor(&bytebuffer, &alloc));

    int value = 42;
    int * retrieved_value;
    ghr_assert(gh_ts_retrievec(&ts, "int", &bytebuffer, retrieveaddr_fromlocalmemory, NULL, (uintptr_t)&value, (void**)&retrieved_value));

    printf("EXPECTED: %d, RETRIEVED: %d\n", value, *retrieved_value);

    ghr_assert(gh_bytebuffer_dtor(&bytebuffer));


    ghr_assert(gh_ts_dtor(&ts));
}
