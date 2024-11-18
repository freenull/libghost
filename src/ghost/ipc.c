#include <signal.h>
#include <poll.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <ghost/result.h>
#include <ghost/ipc.h>

gh_result gh_ipc_ctor(gh_ipc * ipc, int * out_peerfd) {
    int fds[2];
    int socketpair_res = socketpair(AF_UNIX, SOCK_DGRAM, 0, fds);
    if (socketpair_res < 0) {
        return ghr_errno(GHR_IPC_SOCKCREATEFAIL);
    }

    ipc->sockfd = fds[0];
    *out_peerfd = fds[1];

    ipc->mode = GH_IPCMODE_CONTROLLER;

    return GHR_OK;
}

gh_result gh_ipc_ctorconnect(gh_ipc * ipc, int sockfd) {
    ipc->sockfd = sockfd;
    ipc->mode = GH_IPCMODE_CHILD;
    return GHR_OK;
}

gh_result gh_ipc_dtor(gh_ipc * ipc) {
    if (close(ipc->sockfd) < 0) return ghr_errno(GHR_IPC_CLOSEFDFAIL);
    return GHR_OK;
}

static gh_result prepare_cmsg(gh_ipc * ipc, gh_ipcmsg * msg, struct msghdr * msgh, char * cmsg_buf) {
    bool is_send = cmsg_buf != NULL;

    int * fd = NULL;
    bool required = false;

    if (msg->type == GH_IPCMSG_NEWSUBJAIL) {
        fd = &((gh_ipcmsg_newsubjail *)msg)->sockfd;
        required = true;
    } else if (msg->type == GH_IPCMSG_FUNCTIONRETURN) {
        fd = &((gh_ipcmsg_functionreturn *)msg)->fd;
        required = false;
    }

    if (fd != NULL && (!is_send || *fd >= 0)) {
        if (is_send && ipc->mode != GH_IPCMODE_CONTROLLER) {
            return GHR_IPC_NOCONTROLMSG;
        }

        if (is_send) {
            msgh->msg_control = cmsg_buf;
            msgh->msg_controllen = CMSG_SPACE(GH_IPCMSG_CDATAMAXSIZE);
        }

        struct cmsghdr * cmsg_header;
        cmsg_header = CMSG_FIRSTHDR(msgh);

        if (cmsg_header == NULL) {
            if (required || is_send) return GHR_IPC_NOCONTROLDATA;
            return GHR_OK;
        }

        if (is_send) {
            cmsg_header->cmsg_level = SOL_SOCKET;
            cmsg_header->cmsg_type = SCM_RIGHTS;
            cmsg_header->cmsg_len = CMSG_LEN(sizeof(int));
            memcpy(CMSG_DATA(cmsg_header), fd, sizeof(int));
        } else {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
            // RATIONALE: The data was written into bytes from a struct.
            // We are exactly the same machine when receiving as when sending,
            // so the alignment will be the same. This cast is okay.
 
            *fd = *((int*)CMSG_DATA(cmsg_header));
#pragma GCC diagnostic pop
        }
    }

    return GHR_OK;
}

gh_result gh_ipc_send(gh_ipc * ipc, gh_ipcmsg * msg, size_t msg_size) {
    struct iovec iov;
    struct msghdr msgh;

    // Socket is already connected, name is unnecessary
    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;

    iov.iov_base = msg;
    iov.iov_len = msg_size;

    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
 
    msgh.msg_flags = 0;

    msgh.msg_control = NULL;
    msgh.msg_controllen = 0;

    char cmsg_buf[CMSG_SPACE(GH_IPCMSG_CDATAMAXSIZE)] = {0};

    gh_result res = prepare_cmsg(ipc, msg, &msgh, cmsg_buf);
    if (ghr_iserr(res)) return res;

    ssize_t sendmsg_res = sendmsg(ipc->sockfd, &msgh, 0);
    if (sendmsg_res < 0) {
        return ghr_errno(GHR_IPC_SENDMSGFAIL);
    }

    return GHR_OK;
}

static void basic_sanitize_msg(gh_ipcmsg * msg) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
    // RATIONALE: Not all messages have to be sanitized at the receive stage.
    switch(msg->type) {
    case GH_IPCMSG_LUASTRING:
        ((gh_ipcmsg_luastring * )msg)->content[GH_IPCMSG_LUASTRING_MAXSIZE - 1] = '\0';
        break;
    case GH_IPCMSG_LUARESULT:
        ((gh_ipcmsg_luaresult * )msg)->error_msg[GH_IPCMSG_LUARESULT_ERRORMSGMAX - 1] = '\0';
        break;
    case GH_IPCMSG_FUNCTIONCALL: {
        gh_ipcmsg_functioncall * fc_msg = ((gh_ipcmsg_functioncall * )msg);
        fc_msg->name[GH_IPCMSG_FUNCTIONCALL_MAXNAME - 1] = '\0';
        if (fc_msg->arg_count > GH_IPCMSG_FUNCTIONCALL_MAXARGS) {
            fc_msg->arg_count = GH_IPCMSG_FUNCTIONCALL_MAXARGS;
        }
        break;
    }

    default: break;
#pragma GCC diagnostic pop
    }
}

gh_result gh_ipc_recv(gh_ipc * ipc, gh_ipcmsg * msg, int timeout_ms) {
    struct iovec iov;
    struct msghdr msgh;

    union {
        char buf[CMSG_SPACE(GH_IPCMSG_CDATAMAXSIZE)];
        struct cmsghdr align;
    } control_msg;

    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;

    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;

    iov.iov_base = msg;
    iov.iov_len = GH_IPCMSG_MAXSIZE;

    msgh.msg_flags = 0;

    msgh.msg_control = &control_msg;
    msgh.msg_controllen = sizeof(control_msg);

    if (timeout_ms != GH_IPC_NOTIMEOUT) {
        struct pollfd fd = {
            .fd = ipc->sockfd,
            .events = POLLIN | POLLPRI,
            .revents = 0
        };
        int poll_res = poll(&fd, 1, timeout_ms);
        if (poll_res < 0) return ghr_errno(GHR_IPC_POLLMSGFAIL);

        if (poll_res == 0) return GHR_IPC_RECVMSGTIMEOUT;
    }

    ssize_t recv_size = recvmsg(ipc->sockfd, &msgh, 0);
    if (recv_size < 0) return ghr_errno(GHR_IPC_RECVMSGFAIL);

    bool msg_trunc = (msgh.msg_flags & MSG_TRUNC) != 0;
    if (msg_trunc) return GHR_IPC_RECVMSGTRUNC;

    if ((unsigned long)recv_size < sizeof(gh_ipcmsg)) return GHR_IPC_RECVTOOSMALL;

    basic_sanitize_msg(msg);

    gh_result res = prepare_cmsg(ipc, msg, &msgh, NULL);
    if (ghr_iserr(res)) return res;

    return GHR_OK;
}

gh_result gh_ipc_call(gh_ipc * ipc, const char * name, size_t argc, gh_ipcmsg_functioncall_arg * args, void * return_arg, size_t return_arg_size) {
    gh_ipcmsg_functioncall funccall = {0};
    funccall.type = GH_IPCMSG_FUNCTIONCALL;
    strncpy(funccall.name, name, GH_IPCMSG_FUNCTIONCALL_MAXNAME - 1);
    funccall.name[GH_IPCMSG_FUNCTIONCALL_MAXNAME - 1] = '\0';
    funccall.arg_count = argc;
    if (funccall.arg_count > GH_IPCMSG_FUNCTIONCALL_MAXARGS) {
        funccall.arg_count = GH_IPCMSG_FUNCTIONCALL_MAXARGS;
    }

    if (funccall.arg_count > 0) {
        memcpy(&funccall.args, args, sizeof(gh_ipcmsg_functioncall_arg) * funccall.arg_count);
    }

    funccall.return_arg.addr = (uintptr_t)return_arg;
    funccall.return_arg.size = return_arg_size;

    gh_result res = gh_ipc_send(ipc, (gh_ipcmsg*)&funccall, sizeof(gh_ipcmsg_functioncall));
    if (ghr_iserr(res)) return res;

    GH_IPCMSG_BUFFER(msg_buf);
    res = gh_ipc_recv(ipc, (gh_ipcmsg*)msg_buf, GH_IPC_NOTIMEOUT);
    if (ghr_iserr(res)) return res;

    gh_ipcmsg * msg = (gh_ipcmsg *)msg_buf;
    if (msg->type != GH_IPCMSG_FUNCTIONRETURN) {
        return GHR_JAIL_NORETURN;
    }

    gh_ipcmsg_functionreturn * return_msg = (gh_ipcmsg_functionreturn *)msg;
    if (return_msg->fd >= 0 && funccall.return_arg.size >= sizeof(int) && funccall.return_arg.addr != (uintptr_t)NULL) {
        memcpy((void*)funccall.return_arg.addr, &return_msg->fd, sizeof(int));
    }

    return return_msg->result;
}
