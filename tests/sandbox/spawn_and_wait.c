#include <assert.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <ghost/sandbox.h>

int main(void) {
    gh_sandbox sandbox;
    gh_result res = GHR_OK;

    res = gh_sandbox_ctor(&sandbox, (gh_sandboxoptions) {
        .name = "ghost-test-sandbox",
        .memory_limit = GH_SANDBOX_NOMEMLIMIT
    });
    ghr_assert(res);

    printf("send %d\n", sandbox.ipc.sockfd);

    gh_ipcmsg_hello hello_msg;
    memset(&hello_msg, 0, sizeof(gh_ipcmsg_hello));
    hello_msg.type = GH_IPCMSG_HELLO;
    hello_msg.pid = getpid();
    ghr_assert(gh_ipc_send(&sandbox.ipc, (gh_ipcmsg*)&hello_msg, sizeof(gh_ipcmsg_hello)));

    gh_ipc direct_ipc;
    int direct_peerfd;
    ghr_assert(gh_ipc_ctor(&direct_ipc, &direct_peerfd));

    gh_ipcmsg_newsubjail newsubjail_msg;
    memset(&newsubjail_msg, 0, sizeof(gh_ipcmsg_newsubjail));
    newsubjail_msg.type = GH_IPCMSG_NEWSUBJAIL;
    newsubjail_msg.sockfd = direct_peerfd;
    fprintf(stderr, "sending fd: %d\n", newsubjail_msg.sockfd);
    ghr_assert(gh_ipc_send(&sandbox.ipc, (gh_ipcmsg*)&newsubjail_msg, sizeof(gh_ipcmsg_newsubjail)));

    char recvmsg_buf[GH_IPCMSG_MAXSIZE];
    gh_ipcmsg * recvmsg = (gh_ipcmsg *)recvmsg_buf;
    ghr_assert(gh_ipc_recv(&sandbox.ipc, recvmsg, 1000));
    assert(recvmsg->type == GH_IPCMSG_SUBJAILALIVE);
    fprintf(stderr, "new subjail is ready: %d\n", ((gh_ipcmsg_subjailalive *)recvmsg)->index);

    assert(close(direct_peerfd) >= 0);

    ghr_assert(gh_ipc_send(&direct_ipc, (gh_ipcmsg*)&hello_msg, sizeof(gh_ipcmsg_hello)));

    gh_ipc direct2_ipc;
    int direct2_peerfd;
    ghr_assert(gh_ipc_ctor(&direct2_ipc, &direct2_peerfd));

    memset(&newsubjail_msg, 0, sizeof(gh_ipcmsg_newsubjail));
    newsubjail_msg.type = GH_IPCMSG_NEWSUBJAIL;
    newsubjail_msg.sockfd = direct2_peerfd;
    fprintf(stderr, "sending fd: %d\n", newsubjail_msg.sockfd);
    ghr_assert(gh_ipc_send(&sandbox.ipc, (gh_ipcmsg*)&newsubjail_msg, sizeof(gh_ipcmsg_newsubjail)));

    ghr_assert(gh_ipc_recv(&sandbox.ipc, recvmsg, 1000));
    assert(recvmsg->type == GH_IPCMSG_SUBJAILALIVE);
    fprintf(stderr, "new subjail is ready: %d\n", ((gh_ipcmsg_subjailalive *)recvmsg)->index);

    assert(close(direct2_peerfd) >= 0);

    ghr_assert(gh_ipc_send(&direct2_ipc, (gh_ipcmsg*)&hello_msg, sizeof(gh_ipcmsg_hello)));

    gh_ipcmsg_quit quit_msg;
    memset(&quit_msg, 0, sizeof(gh_ipcmsg_quit));
    quit_msg.type = GH_IPCMSG_QUIT;
    ghr_assert(gh_ipc_send(&sandbox.ipc, (gh_ipcmsg*)&quit_msg, sizeof(gh_ipcmsg_quit)));

    ghr_assert(gh_ipc_send(&direct_ipc, (gh_ipcmsg*)&quit_msg, sizeof(gh_ipcmsg_quit)));
    ghr_assert(gh_ipc_send(&direct2_ipc, (gh_ipcmsg*)&quit_msg, sizeof(gh_ipcmsg_quit)));
    
    ghr_assert(gh_sandbox_wait(&sandbox));

    return 0;
}
