#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <linux/bpf_common.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <stddef.h>
#include <unistd.h>

#include <ghost/result.h>
#include <ghost/sandbox.h>
#include <jail/jail.h>

gh_sandboxoptions gh_global_sandboxoptions;

int main(int argc, char ** argv) {
    errno = EINVAL;

    if (argc < 2) {
        ghr_fail(GHR_JAIL_NOOPTIONSFD);
        return 1;
    }

    fprintf(stderr, "jail: started with pid %d\n", getpid());
    fprintf(stderr, "jail: sandbox options fd = %s\n", argv[1]);

    intmax_t strtoimax_res = strtoimax(argv[1], NULL, 10);
    if ((strtoimax_res == INTMAX_MAX && errno == ERANGE) || strtoimax_res < 0 || strtoimax_res > INT_MAX) {
        ghr_fail(GHR_JAIL_OPTIONSFDPARSEFAIL);
        return 1;
    }

    int sandbox_options_fd = (int)strtoimax_res;
    gh_result res = gh_sandboxoptions_readfrom(sandbox_options_fd, &gh_global_sandboxoptions);
    ghr_assert(res);

    fprintf(stderr, "jail: responsible for sandbox '%s'\n", gh_global_sandboxoptions.name);

    ghr_assert(gh_jail_lockdown(&gh_global_sandboxoptions));

    fprintf(stderr, "jail: security policy in effect\n");
}


gh_result gh_jail_lockdown(gh_sandboxoptions * options) {

    static const struct sock_filter filter[] = {
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, arch))),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL_PROCESS),

        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, nr))),
        // luajit
        /* BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_mmap, 0, 1), */
        /* BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, ar))), */
        
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

    static const struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = (struct sock_filter*)filter
    };

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
