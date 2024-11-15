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

typedef struct node node;
struct node {
    int value;
    node * next;
};

static void print_ll(node * node) {
    while (node != NULL) {
        printf("- %d (%p)\n", node->value, (void*)node);
        node = node->next;
    }
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

    ghr_assert(gh_ts_append(&ts, (gh_tstype) {
        .kind = GH_TSTYPE_POINTER,
        .name = "node *",
        .t_pointer = {
            .element_type = "node"
        }
    }));

    ghr_assert(gh_ts_append(&ts, (gh_tstype) {
        .kind = GH_TSTYPE_STRUCT,
        .name = "node",
        .t_struct = {
            .size = sizeof(node),
            .field_count = 2,
            .fields = {
                { .name = "value", .type = "int", .offset = offsetof(node, value) },
                { .name = "next", .type = "node *", .offset = offsetof(node, next) },
            }
        }
    }));


    size_t size = 0;
    ghr_assert(gh_ts_sizec(&ts, "node", &size));
    assert(size == sizeof(node));

    // attempt to copy contents of the primitive type from one memory location
    // to another using gh_ts_retrieve
    // (this will use a very primitive callback function that works inside local
    //  memory - the goal is to use process_vm_readv in the callback on a child
    //  process)

    gh_bytebuffer bytebuffer;
    ghr_assert(gh_bytebuffer_ctor(&bytebuffer, &alloc));

    node ll3 = { .value = 3, .next = NULL };
    node ll2 = { .value = 2, .next = &ll3 };
    node ll1 = { .value = 1, .next = &ll2 };

    printf("Linked list:\n");
    print_ll(&ll1);

    node * retrieved_ll1;
    ghr_assert(gh_ts_retrievec(&ts, "node", &bytebuffer, retrieveaddr_fromlocalmemory, NULL, (uintptr_t)&ll1, (void**)&retrieved_ll1));

    printf("Reinterpreted linked list:\n");
    print_ll(retrieved_ll1);

    assert(retrieved_ll1->value == ll1.value);
    assert(retrieved_ll1->next != NULL);
    assert(retrieved_ll1->next != ll1.next);

    assert(retrieved_ll1->next->value == ll2.value);
    assert(retrieved_ll1->next->next != NULL);
    assert(retrieved_ll1->next->next != ll2.next);

    assert(retrieved_ll1->next->next->value == ll3.value);
    assert(retrieved_ll1->next->next->next == NULL);

    ghr_assert(gh_bytebuffer_dtor(&bytebuffer));

    ghr_assert(gh_ts_dtor(&ts));
}
