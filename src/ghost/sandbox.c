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
    void * dest = memcpy(&sandbox->options, &options, sizeof(gh_sandboxoptions));
    sandbox->pid = pid;
    return GHR_OK;
}

static gh_result gh_sandbox_ctor_child(gh_sandbox * sandbox, gh_sandboxoptions options) {
    int fd = memfd_create("gh-sandbox-options", 0);
    if (fd < 0) return ghr_errno(GHR_JAIL_OPTIONSMEMFAIL);

    int write_res = write(fd, (void*)&options, sizeof(gh_sandboxoptions));
    if (write_res < 0) return ghr_errno(GHR_JAIL_OPTIONSWRITEFAIL);

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

    pid_t child_pid = fork();
    if (child_pid == 0) {
        gh_result res = gh_sandbox_ctor_child(sandbox, options);

        // Can't let the child live if it's not going to be replaced with
        // the jail executable
        if (ghr_iserr(res)) {
            ghr_fputs(stderr, res);
            fprintf(stderr, "Sandbox child process cannot live outside jail. Shutting down with error code 1.");
            exit(1);
        }

        __builtin_unreachable();
    } else {
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
        printf("sig: %d\n", sig);
        return ghr_errnoval(GHR_JAIL_KILLEDSIG, sig);
    }

    __builtin_unreachable();
}

gh_result gh_sandboxoptions_readfrom(int fd, gh_sandboxoptions * out_options) {
    int read_res = read(fd, out_options, sizeof(gh_sandboxoptions));
    if (read_res < 0) return ghr_errno(GHR_SANDBOX_OPTIONSREADFAIL);
    if (read_res != sizeof(gh_sandboxoptions)) return GHR_SANDBOX_OPTIONSREADFAIL;
    return GHR_OK;
}
