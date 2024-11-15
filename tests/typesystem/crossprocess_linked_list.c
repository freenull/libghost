#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <ghost/result.h>
#include <ghost/typesystem.h>

static gh_result retrieveaddr_remote(uintptr_t addr, size_t size, void * buffer, void * userdata) {
    pid_t pid = (pid_t)(uintptr_t)userdata;

    struct iovec local_iov = { .iov_base = buffer, .iov_len = size };
    struct iovec remote_iov = { .iov_base = (void*)addr, .iov_len = size };

    ssize_t readv_res = process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0);
    if (readv_res < 0) {
        return ghr_errno(GHR_TEST_GENERIC);
    }

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
    int sockets[2];
    assert(socketpair(PF_LOCAL, SOCK_DGRAM, 0, sockets) == 0);

    pid_t child_pid = fork();

    if (child_pid == 0) { 
        // child

        int sockfd = sockets[0];
        assert(close(sockets[1]) == 0);

        node ll3 = {0};
        ll3.value = 3;
        ll3.next = NULL;

        node ll2 = {0};
        ll2.value = 2;
        ll2.next = &ll3;

        node ll1 = {0};
        ll1.value = 1;
        ll1.next = &ll2;

        printf(" child: sending parent entry node to the following linked list:\n");
        print_ll(&ll1);
        assert(send(sockfd, &ll1, sizeof(node), 0) == sizeof(node));

        printf(" child: sending entry node pointer for remote read: %p\n", (void*)&ll1);
        node * ll1_ptr = &ll1;
        assert(send(sockfd, &ll1_ptr, sizeof(node *), 0) == sizeof(node *));

        printf(" child: sending parent ll2 and ll3 nodes to compare pointers\n");
        assert(send(sockfd, &ll2, sizeof(node), 0) == sizeof(node));
        assert(send(sockfd, &ll3, sizeof(node), 0) == sizeof(node));

        printf(" child: will now stay idle until parent sends a message\n");
        char b;
        assert(recv(sockfd, &b, 1, 0) == 1);

        printf(" child: done\n");
    } else {
        // parent
        
        int sockfd = sockets[1];
        assert(close(sockets[0]) == 0);

        gh_alloc alloc = gh_alloc_default();
        gh_ts ts;
        ghr_assert(gh_ts_ctor(&ts, &alloc));
        ghr_assert(gh_ts_loadbuiltin(&ts));

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

        ghr_assert(gh_ts_makederivatives(&ts));

        size_t size = 0;
        ghr_assert(gh_ts_sizec(&ts, "node", &size));
        assert(size == sizeof(node));

        gh_bytebuffer bytebuffer;
        ghr_assert(gh_bytebuffer_ctor(&bytebuffer, &alloc));

        printf("parent: waiting to receive entry linked list node and remote pointer from child\n");
        node ll1;
        assert(recv(sockfd, &ll1, sizeof(node), 0) == sizeof(node));

        node * ll1_remote_ptr;
        assert(recv(sockfd, &ll1_remote_ptr, sizeof(node *), 0) == sizeof(node *));

        printf("parent: received node with value %d, remote pointer %p, next pointer %p\n", ll1.value, (void*)ll1_remote_ptr, (void*)ll1.next);

        node * retrieved_ll1;
        ghr_assert(gh_ts_retrievec(&ts, "node", &bytebuffer, retrieveaddr_remote, (void*)(uintptr_t)child_pid, (uintptr_t)ll1_remote_ptr, (void**)&retrieved_ll1));

        printf("Reinterpreted linked list:\n");
        print_ll(retrieved_ll1);

        assert(retrieved_ll1->value == ll1.value);
        assert(retrieved_ll1->next != NULL);
        assert(retrieved_ll1->next != ll1.next);

        printf("parent: waiting to receive node ll2 from child for comparison checks\n");
        node ll2;
        assert(recv(sockfd, &ll2, sizeof(node), 0) == sizeof(node));

        printf("parent: waiting to receive node ll3 from child for comparison checks\n");
        node ll3;
        assert(recv(sockfd, &ll3, sizeof(node), 0) == sizeof(node));

        assert(retrieved_ll1->next->value == ll2.value);
        assert(retrieved_ll1->next->next != NULL);
        assert(retrieved_ll1->next->next != ll2.next);

        assert(retrieved_ll1->next->next->value == ll3.value);
        assert(retrieved_ll1->next->next->next == NULL);

        ghr_assert(gh_bytebuffer_dtor(&bytebuffer));

        ghr_assert(gh_ts_dtor(&ts));

        printf("parent: notifying child that it can now close\n");
        assert(send(sockfd, "1", 1, 0) == 1);

        int status = 0;
        waitpid(child_pid, &status, 0);

        printf("parent: done\n");
    }
}
