#define _GNU_SOURCE
#include <pty.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <ghost/stdlib.h>
#include <ghost/rpc.h>
#include <ghost/perms/perms.h>
#include <ghost/perms/pathfd.h>
#include <ghost/perms/procfd.h>
#include <ghost/result.h>

gh_result gh_std_openat(gh_thread * thread, int dirfd, const char * path, int flags, mode_t create_mode, int * out_fd) {
    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;
    gh_perms * perms = &thread->perms;

    gh_pathfd pathfd = {0};
    res = gh_pathfd_open(dirfd, path, &pathfd, (flags & O_CREAT) != 0);
    if (ghr_iserr(res)) return res;

    gh_permfs_mode mode;
    res = gh_permfs_fcntlflags2permfsmode(flags, create_mode, pathfd, &mode);
    if (ghr_iserr(res)) goto close_pathfd;

    res = gh_perms_gatefile(&thread->perms, thread->safe_id, pathfd, mode, NULL);
    if (ghr_iserr(res)) goto close_pathfd;

    int new_fd = -1;
    res = gh_procfd_reopen(&perms->procfd, pathfd, flags, create_mode, &new_fd);
    if (ghr_iserr(res)) goto close_pathfd;

    *out_fd = new_fd;

close_pathfd:
    inner_res = gh_pathfd_close(pathfd);
    if (ghr_iserr(inner_res)) res = inner_res;

    return res;
}

static void ghost_open(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * path_buf;
    size_t path_size;
    if (!gh_rpcframe_argbuf(frame, 0, &path_buf, &path_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }

    if (path_size > INT_MAX) gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EFBIG));
    path_buf[path_size - 1] = '\0';

    int * flags;
    if (!gh_rpcframe_arg(frame, 1, &flags)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG1, EINVAL));
    }

    int * access_mode;
    if (!gh_rpcframe_arg(frame, 2, &access_mode)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG2, EINVAL));
    }

    int fd;
    gh_result res = gh_std_openat(frame->thread, AT_FDCWD, path_buf, *flags, (mode_t)*access_mode, &fd);
    if (ghr_iserr(res)) {
        gh_rpcframe_failhere(frame, res);
    }

    gh_rpcframe_returnfdhere(frame, fd);
}

gh_result gh_std_unlinkat(gh_thread * thread, int dirfd, const char * path) {
    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    gh_pathfd pathfd = {0};
    res = gh_pathfd_opentrailing(dirfd, path, &pathfd);
    if (ghr_iserr(res)) goto close_pathfd;

    gh_permfs_mode mode = GH_PERMFS_UNLINK;

    res = gh_perms_gatefile(&thread->perms, thread->safe_id, pathfd, mode, NULL);
    if (ghr_iserr(res)) goto close_pathfd;

    if (unlinkat(pathfd.fd, pathfd.trailing_name, 0) < 0) {
        return ghr_errno(GHR_STD_UNLINK);
    }

close_pathfd:
    inner_res = gh_pathfd_close(pathfd);
    if (ghr_iserr(inner_res)) res = inner_res;

    return res;
}

static void ghost_remove(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * path_buf;
    size_t path_size;
    if (!gh_rpcframe_argbuf(frame, 0, &path_buf, &path_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }

    if (path_size > INT_MAX) gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EFBIG));
    path_buf[path_size - 1] = '\0';

    gh_result res = gh_std_unlinkat(frame->thread, AT_FDCWD, path_buf);
    if (ghr_iserr(res)) {
        gh_rpcframe_failhere(frame, res);
    }
}

#define LCGRAND_A 1103515245
#define LCGRAND_M ((uint32_t)1 << 31)
#define LCGRAND_C 12345
static uint32_t lcgrand(uint32_t input) {
    return (LCGRAND_A * input + LCGRAND_C) % LCGRAND_M;
}

static void random_str(char * buf, size_t size) {
    static const char charset[] = "0123456789"
                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "abcdefghijklmnopqrstuvwxyz";

    uint32_t seed = lcgrand((uint32_t)time(0));

    for (size_t i = 0; i < size; i++) {
        buf[i] = charset[(seed = lcgrand(seed)) % (sizeof(charset) - 1)];
    }
}

gh_result gh_std_opentemp(gh_thread * thread, const char * prefix, int flags, struct gh_std_tempfile * out_tempfile) {
    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    int tmp_fd = open("/tmp", O_PATH | O_DIRECTORY);
    if (tmp_fd < 0) return GHR_STD_OPENTMP;

    size_t prefix_len = strlen(prefix);
    char tmp_name[prefix_len + 16 + 1];
    strcpy(tmp_name, prefix);
    random_str(tmp_name + prefix_len, 16);
    tmp_name[sizeof(tmp_name) - 1] = '\0';

    int new_fd = -1;

    gh_pathfd pathfd = {0};
    res = gh_pathfd_open(tmp_fd, tmp_name, &pathfd, (flags & O_CREAT) != 0);
    if (ghr_iserr(res)) goto err_close_fd;

    gh_permfs_mode mode;
    res = gh_permfs_fcntlflags2permfsmode(flags, 0644, pathfd, &mode);
    if (ghr_iserr(res)) goto err_close_fd;

    res = gh_perms_gatefile(&thread->perms, thread->safe_id, pathfd, mode, "tmpfile");
    if (ghr_iserr(res)) goto err_close_fd;

    gh_abscanonicalpath path;
    res = gh_procfd_fdpathctor(&thread->perms.procfd, pathfd, &path);
    if (ghr_iserr(res)) goto err_close_fd;

    new_fd = openat(tmp_fd, tmp_name, O_RDWR | O_CREAT, 0);
    if (new_fd < 0) {
        res = ghr_errno(GHR_STD_OPENTMPFILE);
        goto err_dtor_fdpath;
    }

    out_tempfile->fd = new_fd;
    strncpy(out_tempfile->path, path.ptr, GH_STD_TEMPFILE_PATHMAX);
    out_tempfile->path[GH_STD_TEMPFILE_PATHMAX - 1] = '\0';

err_dtor_fdpath:
    inner_res = gh_procfd_fdpathdtor(&thread->perms.procfd, &path);
    if (ghr_iserr(inner_res)) res = inner_res;

err_close_fd:
    if (close(tmp_fd) < 0) {
        if (new_fd >= 0 && close(new_fd < 0) < 0) return ghr_errno(GHR_STD_CLOSETMPFILE);
        return ghr_errno(GHR_STD_CLOSETMP);
    }
    return res;
}

static void ghost_opentemp(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * prefix_buf;
    size_t prefix_size;
    if (!gh_rpcframe_argbuf(frame, 0, &prefix_buf, &prefix_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }

    if (prefix_size > INT_MAX) gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EFBIG));
    prefix_buf[prefix_size - 1] = '\0';

    int * flags;
    if (!gh_rpcframe_arg(frame, 1, &flags)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG1, EINVAL));
    }

    struct gh_std_tempfile tempfile;
    gh_result res = gh_std_opentemp(frame->thread, prefix_buf, *flags, &tempfile);
    if (ghr_iserr(res)) {
        gh_rpcframe_failhere(frame, res);
    }

    gh_rpcframe_setreturnfd(frame, tempfile.fd);
    gh_rpcframe_returnbuftypedhere(frame, tempfile.path, strlen(tempfile.path))
}

gh_result gh_std_fsrequest(gh_thread * thread, int dirfd, const char * path, gh_permfs_mode self_mode, gh_permfs_mode children_mode, bool * out_wouldprompt) {
    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    if (!gh_permfs_ismodevalid(self_mode)) return GHR_STD_INVALIDMODE;
    if (!gh_permfs_ismodevalid(children_mode)) return GHR_STD_INVALIDMODE;

    if (self_mode == children_mode && self_mode == GH_PERMFS_NONE) return GHR_STD_INVALIDMODE;

    gh_pathfd pathfd = {0};
    res = gh_pathfd_open(dirfd, path, &pathfd, true);
    if (ghr_iserr(res)) return res;

    res = gh_perms_fsrequest(&thread->perms, thread->safe_id, pathfd, self_mode, children_mode, "future", out_wouldprompt);
    if (ghr_iserr(res)) goto close_pathfd;

close_pathfd:
    inner_res = gh_pathfd_close(pathfd);
    if (ghr_iserr(inner_res)) res = inner_res;

    return res;
}

static void ghost_fsrequest(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * path_buf;
    size_t path_size;
    if (!gh_rpcframe_argbuf(frame, 0, &path_buf, &path_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }

    if (path_size > INT_MAX) gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EFBIG));
    path_buf[path_size - 1] = '\0';

    gh_permfs_mode * self_mode;
    if (!gh_rpcframe_arg(frame, 1, &self_mode)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG1, EINVAL));
    }

    gh_permfs_mode * children_mode;
    if (!gh_rpcframe_arg(frame, 2, &children_mode)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG2, EINVAL));
    }

    gh_result res = gh_std_fsrequest(frame->thread, AT_FDCWD, path_buf, *self_mode, *children_mode, NULL);

    if (ghr_iserr(res)) {
        gh_rpcframe_failhere(frame, res);
    }
}

static void ghost_fshas(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * path_buf;
    size_t path_size;
    if (!gh_rpcframe_argbuf(frame, 0, &path_buf, &path_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }

    if (path_size > INT_MAX) gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EFBIG));
    path_buf[path_size - 1] = '\0';

    gh_permfs_mode * self_mode;
    if (!gh_rpcframe_arg(frame, 1, &self_mode)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG1, EINVAL));
    }

    gh_permfs_mode * children_mode;
    if (!gh_rpcframe_arg(frame, 2, &children_mode)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG2, EINVAL));
    }

    bool would_prompt = false;
    gh_result res = gh_std_fsrequest(frame->thread, AT_FDCWD, path_buf, *self_mode, *children_mode, &would_prompt);

    if (ghr_iserr(res)) {
        gh_rpcframe_failhere(frame, res);
    }

    bool has_perms = !would_prompt;
    gh_rpcframe_returntypedhere(frame, &has_perms);
}

gh_result gh_std_execute(gh_thread * thread, int dirfd, const char * shellscript, char * const * envp, int * out_exitcode, int * out_ptyfd) {
    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    // C popen(3) is defined as running the argument as a parameter to /bin/sh.
    // Both Lua and LuaJIT popen behavior is based on popen(3).
    char * shell_program = "/bin/sh";

    gh_pathfd pathfd = {0};
    res = gh_pathfd_open(dirfd, shell_program, &pathfd, GH_PATHFD_RESOLVELINKS);
    if (ghr_iserr(res)) return res;

    char * const argv[] = {
        shell_program,
        "-c",
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
        // RATIONALE: The odd interface of (char * const *) for argv and envp is
        // caused by the signature of execve. The strings don't have to be mutable.

        (char *)shellscript,
#pragma GCC diagnostic pop
        NULL
    };

    res = gh_perms_gateexec(&thread->perms, thread->safe_id, pathfd, argv, envp);
    if (ghr_iserr(res)) goto close_pathfd;

    int pty_masterfd = -1;
    int pty_slavefd = -1;

    if (out_ptyfd != NULL) {
        if (openpty(&pty_masterfd, &pty_slavefd, NULL, NULL, NULL) < 0) {
            res = ghr_errno(GHR_STD_SPAWNPTY);
            goto close_pathfd;
        }
        *out_ptyfd = pty_masterfd;
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (out_ptyfd != NULL) {
            if (close(pty_masterfd) < 0) abort();
            if (dup2(pty_slavefd, STDIN_FILENO) < 0) abort();
            if (dup2(pty_slavefd, STDOUT_FILENO) < 0) abort();
            if (dup2(pty_slavefd, STDERR_FILENO) < 0) abort();
            if (close(pty_slavefd) < 0) abort();
        }
        int exec_res = execveat(pathfd.fd, "", argv, envp, AT_EMPTY_PATH);
        if (exec_res < 0) {
            abort();
        }
    }

    if (out_ptyfd != NULL && close(pty_slavefd) < 0) {
        res = ghr_errno(GHR_STD_SPAWNPTYCLOSE);
        goto close_pathfd;
    }

    if (out_exitcode != NULL) {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            res = ghr_errno(GHR_STD_SPAWNWAIT);
            goto close_pathfd;
        }

        if (WIFEXITED(status)) {
            *out_exitcode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            *out_exitcode = 128 + WTERMSIG(status);
        } else {
            *out_exitcode = 255;
        }
    }

close_pathfd:
    inner_res = gh_pathfd_close(pathfd);
    if (ghr_iserr(inner_res)) res = inner_res;

    return res;
}

static void ghost_execute(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * cmd_buf;
    size_t cmd_size;
    if (!gh_rpcframe_argbuf(frame, 0, &cmd_buf, &cmd_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }

    if (cmd_size > GH_PERMEXEC_CMDLINEMAX) gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EFBIG));

    if (cmd_size > 0) {
        cmd_buf[cmd_size] = '\0';
    }

    int exitcode = 255;
    gh_result res = gh_std_execute(frame->thread, AT_FDCWD, cmd_buf, environ, NULL, NULL);
    if (ghr_iserr(res)) {
        gh_rpcframe_failhere(frame, res);
    }

    gh_rpcframe_returntypedhere(frame, &exitcode);
}

static void ghost_popen(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * cmd_buf;
    size_t cmd_size;
    if (!gh_rpcframe_argbuf(frame, 0, &cmd_buf, &cmd_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }

    if (cmd_size > GH_PERMEXEC_CMDLINEMAX) gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EFBIG));

    if (cmd_size > 0) {
        cmd_buf[cmd_size - 1] = '\0';
    }

    int ptyfd = -1;
    gh_result res = gh_std_execute(frame->thread, AT_FDCWD, cmd_buf, environ, NULL, &ptyfd);
    if (ghr_iserr(res)) {
        gh_rpcframe_failhere(frame, res);
    }

    printf("RETURNING FD: %d\n", ptyfd);
    gh_rpcframe_returnfdhere(frame, ptyfd);
}

gh_result gh_std_registerinrpc(gh_rpc * rpc) {
    gh_result res;

    res = gh_rpc_register(rpc, "ghost.open", ghost_open, GH_RPCFUNCTION_THREADSAFE);
    if (ghr_iserr(res)) return res;

    res = gh_rpc_register(rpc, "ghost.opentemp", ghost_opentemp, GH_RPCFUNCTION_THREADSAFE);
    if (ghr_iserr(res)) return res;

    res = gh_rpc_register(rpc, "ghost.remove", ghost_remove, GH_RPCFUNCTION_THREADSAFE);
    if (ghr_iserr(res)) return res;

    res = gh_rpc_register(rpc, "ghost.perm.fsrequest", ghost_fsrequest, GH_RPCFUNCTION_THREADSAFE);
    if (ghr_iserr(res)) return res;

    res = gh_rpc_register(rpc, "ghost.perm.fshas", ghost_fshas, GH_RPCFUNCTION_THREADSAFE);
    if (ghr_iserr(res)) return res;

    res = gh_rpc_register(rpc, "ghost.execute", ghost_execute, GH_RPCFUNCTION_THREADSAFE);
    if (ghr_iserr(res)) return res;

    res = gh_rpc_register(rpc, "ghost.popen", ghost_popen, GH_RPCFUNCTION_THREADSAFE);
    if (ghr_iserr(res)) return res;

    return res;
}
