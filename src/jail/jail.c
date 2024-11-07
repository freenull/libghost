#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/bpf_common.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <ghost/result.h>
#include <ghost/sandbox.h>
#include <ghost/ipc.h>
#include <ghost/alloc.h>

#include <jail/jail.h>
#include <jail/subjail.h>

gh_sandboxoptions gh_global_sandboxoptions;

static bool message_recv(gh_ipc * ipc, gh_ipcmsg * msg) {
    (void)ipc;

    switch(msg->type) {
    case GH_IPCMSG_HELLO: ghr_fail(GHR_JAIL_MULTIHELLO); break;

    case GH_IPCMSG_QUIT:
        fprintf(stderr, "jail: received request to exit\n");
        return true;

    case GH_IPCMSG_SUBJAILALIVE: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);
    case GH_IPCMSG_NEWSUBJAIL:
        fprintf(stderr, "jail: creating new subjail\n");
        int sockfd = ((gh_ipcmsg_newsubjail *)msg)->sockfd;
        gh_subjail_spawn(sockfd, getpid(), ipc);
        if (close(sockfd) < 0) ghr_fail(GHR_JAIL_CLOSEFDFAIL);
        break;

    default:
        fprintf(stderr, "jail: received unknown message of type %d\n", (int)msg->type);
        ghr_fail(GHR_JAIL_UNKNOWNMESSAGE);
        break;
    }

    return false;
}

int main(int argc, char ** argv) {
    errno = EINVAL;

    if (argc < 2) {
        ghr_fail(GHR_JAIL_NOOPTIONSFD);
        return 1;
    }

    fprintf(stderr, "jail: started with pid %d\n", getpid());
    fprintf(stderr, "jail: sandbox options fd is %s\n", argv[1]);

    intmax_t strtoimax_res = strtoimax(argv[1], NULL, 10);
    if ((strtoimax_res == INTMAX_MAX && errno == ERANGE) || strtoimax_res < 0 || strtoimax_res > INT_MAX) {
        ghr_fail(GHR_JAIL_OPTIONSFDPARSEFAIL);
        return 1;
    }

    int sandbox_options_fd = (int)strtoimax_res;
    gh_result res = gh_sandboxoptions_readfrom(sandbox_options_fd, &gh_global_sandboxoptions);
    ghr_assert(res);

    fprintf(stderr, "jail: sandbox options loaded\n");
    fprintf(stderr, "jail: responsible for sandbox '%s'\n", gh_global_sandboxoptions.name);
    fprintf(stderr, "jail: ipc socket fd is %d\n", gh_global_sandboxoptions.jail_ipc_sockfd);


    gh_ipc ipc;
    ghr_assert(gh_ipc_ctorconnect(&ipc, gh_global_sandboxoptions.jail_ipc_sockfd));
    fprintf(stderr, "jail: connected to ipc\n");

    fprintf(stderr, "jail: locking down\n");

    ghr_assert(gh_jail_lockdown(&gh_global_sandboxoptions));

    fprintf(stderr, "jail: security policy in effect\n");

    fprintf(stderr, "jail: waiting for hello\n");

    char msg_buf[GH_IPCMSG_MAXSIZE];
    gh_ipcmsg * msg = (gh_ipcmsg *)msg_buf;
    res = gh_ipc_recv(&ipc, msg, GH_JAIL_HELLOTIMEOUTMS);

    if (res == GHR_IPC_RECVMSGTIMEOUT) {
        fprintf(stderr, "jail: timed out while waiting for hello message, bailing\n");
        return 1;
    }
    ghr_assert(res);

    if (msg->type != GH_IPCMSG_HELLO) {
        fprintf(stderr, "jail: received non-hello message, bailing\n");
        return 1;
    }

    fprintf(stderr, "jail: hello received from pid %d\n", ((gh_ipcmsg_hello*)msg)->pid);

    fprintf(stderr, "jail: entering main message loop\n");

    while (true) {
        ghr_assert(gh_ipc_recv(&ipc, msg, 0));

        if (message_recv(&ipc, msg)) break;
    }

    fprintf(stderr, "jail: stopping normally\n");

    return 0;
}

gh_result gh_jail_lockdown(gh_sandboxoptions * options) {
    static const struct sock_filter filter[] = {
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, arch))),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL_PROCESS),


        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, nr))),
        /* BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_mmap, 0, 1), */
        /* BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, ar))), */
        
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_recvmsg, 23, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_sendmsg, 22, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_close, 21, 0),
        // fork
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_set_robust_list, 20, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_clone, 19, 0),

        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_poll, 18, 0),

        // luajit
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_fstat, 17, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_msync, 16, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_munmap, 15, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_mprotect, 14, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_mmap, 13, 0),
        /* BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_futex, 15, 0), */
        /* BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_rt_sigprocmask, 14, 0), */
        /* BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_tgkill, 13, 0), */
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_gettid, 12, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_getpid, 11, 0),

        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_recvfrom, 10, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_recvmsg, 9, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_sendto, 8, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_clock_nanosleep, 7, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_getrandom, 6, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_brk, 5, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_write, 4, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_read, 3, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_exit, 2, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_exit_group, 1, 0),

        /* BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ERRNO | (SECCOMP_RET_DATA & GH_BOGUS_ERRNO)), */
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL_PROCESS),

        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW)
    };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    // RATIONALE: Field .filter will never be modified. This struct is only passed to seccomp.
    static const struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = (struct sock_filter*)filter
    };
#pragma GCC diagnostic pop

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0, 0) != 0)
    {
        return ghr_errno(GHR_JAIL_NONEWPRIVSFAIL);
    }

    if (options->memory_limit != GH_SANDBOX_NOMEMLIMIT) {
        struct rlimit mem_limit = (struct rlimit){
            .rlim_cur = options->memory_limit,
            .rlim_max = options->memory_limit
        };
        if (setrlimit(RLIMIT_DATA, &mem_limit) < 0)
        {
            return ghr_errno(GHR_JAIL_MEMRESTRICTFAIL);
        }
    }

    if (prctl(PR_SET_SECCOMP, 2, &prog) != 0)
    {
        return ghr_errno(GHR_JAIL_SECCOMPFAIL);
    }
    
    return GHR_OK;
}
