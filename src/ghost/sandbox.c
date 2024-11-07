#define _GNU_SOURCE
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <ghost/result.h>
#include <ghost/sandbox.h>
#include <ghost/embedded_jail.h>

static gh_result gh_sandbox_ctor_parent(gh_sandbox * sandbox, gh_sandboxoptions options, pid_t pid) {
    memcpy(&sandbox->options, &options, sizeof(gh_sandboxoptions));
    sandbox->pid = pid;
    return GHR_OK;
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

            return ghr_errno(GHR_SANDBOX_CLOSESOCKFAIL);
        }

        return gh_sandbox_ctor_parent(sandbox, options, child_pid);
    }
}

gh_result gh_sandbox_wait(gh_sandbox * sandbox) {
    int status;
    pid_t wait_res = waitpid(sandbox->pid, &status, 0);
    if (wait_res < 0) return ghr_errno(GHR_JAIL_WAITFAIL);

    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) return GHR_OK;
        return ghr_errnoval(GHR_JAIL_NONZEROEXIT, exit_code);
        // "errno" is a misnomer here, this specific gh_error contains
        // exit code in the errno field
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        return ghr_errnoval(GHR_JAIL_KILLEDSIG, sig);
    }

    __builtin_unreachable();
}

gh_result gh_sandboxoptions_readfrom(int fd, gh_sandboxoptions * out_options) {
    ssize_t read_res = read(fd, out_options, sizeof(gh_sandboxoptions));
    if (read_res < 0) return ghr_errno(GHR_SANDBOX_OPTIONSREADFAIL);
    if (read_res != sizeof(gh_sandboxoptions)) return GHR_SANDBOX_OPTIONSREADFAIL;
    return GHR_OK;
}

gh_result gh_sandbox_dtor(gh_sandbox * sandbox) {
    gh_result res = gh_ipc_dtor(&sandbox->ipc);
    if (ghr_iserr(res)) return res;

    return GHR_OK;
}
