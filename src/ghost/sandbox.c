#define _GNU_SOURCE
#include <sys/poll.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <poll.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <ghost/ipc.h>
#include <ghost/result.h>
#include <ghost/sandbox.h>
#include <ghost/embedded_jail.h>

static gh_result gh_sandbox_ctor_parent(gh_sandbox * sandbox, gh_sandboxoptions options, pid_t pid) {
    memcpy(&sandbox->options, &options, sizeof(gh_sandboxoptions));
    sandbox->pid = pid;

    gh_ipcmsg_hello hello_msg;
    memset(&hello_msg, 0, sizeof(gh_ipcmsg_hello));
    hello_msg.type = GH_IPCMSG_HELLO;
    hello_msg.pid = getpid();
    return gh_ipc_send(&sandbox->ipc, (gh_ipcmsg*)&hello_msg, sizeof(gh_ipcmsg_hello));
}

static gh_result gh_sandbox_ctor_child(gh_sandbox * sandbox, gh_sandboxoptions options) {
    (void)sandbox;

    int fd = memfd_create("gh-sandbox-options", 0);
    if (fd < 0) return ghr_errno(GHR_JAIL_OPTIONSMEMFAIL);

    ssize_t write_res = write(fd, (void*)&options, sizeof(gh_sandboxoptions));
    if (write_res < 0) return ghr_errno(GHR_JAIL_OPTIONSWRITEFAIL);
    if (write_res != sizeof(gh_sandboxoptions)) return ghr_errno(GHR_JAIL_OPTIONSSWRITETRUNC);

    if (lseek(fd, 0, SEEK_SET) == ((off_t)-1)) {
        return ghr_errno(GHR_JAIL_OPTIONSSEEKFAIL);
    }

    gh_result res = gh_embeddedjail_exec(options.name, fd);
    if (ghr_iserr(res)) return res;

    __builtin_unreachable();
}

gh_result gh_sandbox_ctor(gh_sandbox * sandbox, gh_sandboxoptions options) {
    if (!gh_embeddedjail_available()) return GHR_EMBEDDEDJAIL_UNAVAILABLE;
    gh_result res = GHR_OK;

    int child_sockfd;
    res = gh_ipc_ctor(&sandbox->ipc, &child_sockfd);
    if (ghr_iserr(res)) return res;

    options.jail_ipc_sockfd = child_sockfd;

    pid_t child_pid = fork();
    if (child_pid == 0) {
        // Will close parent's fd
        ghr_assert(gh_ipc_dtor(&sandbox->ipc));

        res = gh_sandbox_ctor_child(sandbox, options);

        // Can't let the child live if it's not going to be replaced with
        // the jail executable
        if (ghr_iserr(res)) {
            ghr_fputs(stderr, res);
            fprintf(stderr, "Sandbox child process cannot live outside jail. Shutting down with error code 1.");
            exit(1);
        }

        __builtin_unreachable();
    } else {
        if (close(child_sockfd) < 0) {
            if (kill(child_pid, SIGKILL) < 0) return ghr_errno(GHR_SANDBOX_KILLCHILDFAIL);
            if (ghr_iserr(gh_ipc_dtor(&sandbox->ipc))) {
                return GHR_SANDBOX_CLEANIPCFAIL;
            }

            (void)waitpid(sandbox->pid, NULL, 0);

            return ghr_errno(GHR_SANDBOX_CLOSESOCKFAIL);
        }

        return gh_sandbox_ctor_parent(sandbox, options, child_pid);
    }
}

gh_result gh_sandboxoptions_readfrom(int fd, gh_sandboxoptions * out_options) {
    ssize_t read_res = read(fd, out_options, sizeof(gh_sandboxoptions));
    if (read_res < 0) return ghr_errno(GHR_SANDBOX_OPTIONSREADFAIL);
    if (read_res != sizeof(gh_sandboxoptions)) return GHR_SANDBOX_OPTIONSREADFAIL;
    return GHR_OK;
}


static gh_result sandbox_wait(gh_sandbox * sandbox, int timeout_ms) {
    int pidfd = (int)syscall(SYS_pidfd_open, sandbox->pid, 0);
    if (pidfd < 0) return ghr_errno(GHR_SANDBOX_PIDFD);

    struct pollfd pollfd = {
        .fd = pidfd,
        .events = POLLHUP | POLLIN
    };

    int pollres = poll(&pollfd, 1, timeout_ms);
    if (pollres < 0) return ghr_errno(GHR_SANDBOX_PIDFDPOLL);

    bool force_kill = false;
    if (pollres == 0) {
        if (syscall(SYS_pidfd_send_signal, pidfd, SIGKILL, NULL, 0) < 0) return ghr_errno(GHR_SANDBOX_PIDFDKILL);
        force_kill = true;
    }

    siginfo_t siginfo = {0};
    if (waitid(P_PIDFD, (id_t)pidfd, &siginfo, WEXITED) < 0) return ghr_errno(GHR_SANDBOX_PIDFDWAIT);

    if (force_kill) return GHR_SANDBOX_FORCEKILL;

    if (siginfo.si_code == CLD_EXITED) {
        int exit_code = siginfo.si_status;
        if (exit_code == 0) return GHR_OK;
        return ghr_errnoval(GHR_JAIL_NONZEROEXIT, exit_code);
        // "errno" is a misnomer here, this specific gh_error contains
        // exit code in the errno field
    } else if (siginfo.si_code == CLD_KILLED || siginfo.si_code == CLD_DUMPED) {
        // "errno" is a misnomer here, this specific gh_error contains
        // the signalno in the errno field
        return ghr_errnoval(GHR_JAIL_KILLEDSIG, siginfo.si_status);
    }

    __builtin_unreachable();
}

static gh_result sandbox_requestquit(gh_sandbox * sandbox) {
    gh_ipcmsg_quit quit_msg;
    memset(&quit_msg, 0, sizeof(gh_ipcmsg_quit));
    quit_msg.type = GH_IPCMSG_QUIT;
    gh_result res = gh_ipc_send(&sandbox->ipc, (gh_ipcmsg*)&quit_msg, sizeof(gh_ipcmsg_quit));
    if (ghr_iserr(res)) return res;

    res = sandbox_wait(sandbox, GH_SANDBOX_TIMETOQUITMS);
    if (ghr_iserr(res)) return res;

    return GHR_OK;
}

gh_result gh_sandbox_dtor(gh_sandbox * sandbox, gh_result * out_jailresult) {
    gh_result quit_res = sandbox_requestquit(sandbox);
    if (out_jailresult != NULL) *out_jailresult = quit_res;

    gh_result res = gh_ipc_dtor(&sandbox->ipc);
    if (ghr_iserr(res)) return res;

    return GHR_OK;
}
