#include <sys/types.h>
#include <ghost/ipc.h>
#include <jail/jail.h>
#include <jail/subjail.h>

int gh_global_subjail_idx = -1;

static bool message_recv(gh_ipc * ipc, gh_ipcmsg * msg) {
    (void)ipc;

    switch(msg->type) {
    case GH_IPCMSG_HELLO: ghr_fail(GHR_JAIL_MULTIHELLO); break;

    case GH_IPCMSG_QUIT:
        fprintf(stderr, "subjail %d: received request to exit\n", gh_global_subjail_idx);
        return true;

    case GH_IPCMSG_SUBJAILALIVE: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);
    case GH_IPCMSG_NEWSUBJAIL: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);

    default:
        fprintf(stderr, "subjail %d: received unknown message of type %d\n", gh_global_subjail_idx, (int)msg->type);
        ghr_fail(GHR_JAIL_UNKNOWNMESSAGE);
        break;
    }

    return false;
}

void gh_subjail_spawn(int sockfd, int parent_pid, gh_ipc * parent_ipc) {
    gh_global_subjail_idx += 1;

    pid_t pid = fork();

    if (pid == 0) {
        gh_ipc ipc;
        gh_ipc_ctorconnect(&ipc, sockfd);
        _exit(gh_subjail_main(&ipc, parent_pid, parent_ipc));
    }
}

int gh_subjail_main(gh_ipc * ipc, int parent_pid, gh_ipc * parent_ipc) {
    // Before destroying the parent's IPC and disconnecting from the pool socket,
    // we send a notification confirming that the subjail is ready
    gh_ipcmsg_subjailalive subjailalive_msg;
    subjailalive_msg.type = GH_IPCMSG_SUBJAILALIVE;
    subjailalive_msg.index = gh_global_subjail_idx;
    ghr_assert(gh_ipc_send(parent_ipc, (gh_ipcmsg *)&subjailalive_msg, sizeof(gh_ipcmsg_subjailalive)));
    ghr_assert(gh_ipc_dtor(parent_ipc));

    gh_result res;

    fprintf(stderr, "subjail %d: started by jail pid %d\n", gh_global_subjail_idx, parent_pid);
    fprintf(stderr, "subjail %d: waiting for hello\n", gh_global_subjail_idx);

    char msg_buf[GH_IPCMSG_MAXSIZE];
    gh_ipcmsg * msg = (gh_ipcmsg *)msg_buf;
    res = gh_ipc_recv(ipc, msg, GH_JAIL_HELLOTIMEOUTMS);

    if (res == GHR_IPC_RECVMSGTIMEOUT) {
        fprintf(stderr, "subjail %d: timed out while waiting for hello message, bailing\n", gh_global_subjail_idx);
        return 1;
    }
    ghr_assert(res);

    if (msg->type != GH_IPCMSG_HELLO) {
        fprintf(stderr, "subjail %d: received hello message\n", gh_global_subjail_idx);
        return 1;
    }

    fprintf(stderr, "subjail %d: received hello message\n", gh_global_subjail_idx);

    fprintf(stderr, "subjail %d: entering main message loop\n", gh_global_subjail_idx);

    while (true) {
        ghr_assert(gh_ipc_recv(ipc, msg, 0));

        if (message_recv(ipc, msg)) break;
    }

    fprintf(stderr, "subjail %d: quitting normally\n", gh_global_subjail_idx);
    return 0;
}
