/** @file Simple permission test
 *
 * This test does the following in order:
 *   - Spawns a sandbox.
 *   - Spawns a single thread.
 *   - Registers an 'open' function with the RPC that uses gh_perms to request
 *     permission to the passed in file path.
 *     If granted, the function opens and returns a new file descriptor.
 *   - Executes a Lua script that asks for permission to a file twice -
 *     first through pcall, then in the root (so that it can fail)
 *   - Sends a single input of 'x\n' to the simpletui prompter, then closes the
 *     input file descriptor (so that if simpletui tries to read it again,
 *     the test will crash)
 *   - Ensures that the script finally throws an error regarding the security policy.
 *   - Cleans up all memory.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ghost/rpc.h>
#include <ghost/sandbox.h>
#include <ghost/thread.h>
#include <ghost/alloc.h>
#include <ghost/result.h>
#include <ghost/perms/prompt.h>
#include <ghost/perms/perms.h>
#include <ghost/stdlib.h>

static void func_open(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * path_buf;
    size_t path_size;
    if (!gh_rpcframe_argbuf(frame, 0, &path_buf, &path_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }

    if (path_size > INT_MAX) gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EFBIG));
    path_buf[path_size - 1] = '\0';

    double * flags_arg;
    if (!gh_rpcframe_arg(frame, 1, &flags_arg)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG1, EINVAL));
    }

    printf("open call: path = %s, flags = %d\n", path_buf, (int)*flags_arg);

    int fd;
    gh_result res = gh_std_openat(frame->thread, AT_FDCWD, path_buf, (int)*flags_arg, 0, &fd);
    if (ghr_iserr(res)) {
        gh_rpcframe_failhere(frame, res);
    }

    gh_rpcframe_returnfdhere(frame, fd);
}

int main(void) {
    gh_sandbox sandbox;
    gh_sandboxoptions options = (gh_sandboxoptions) {
        .name = "my sandbox",
        .memory_limit_bytes = GH_SANDBOX_NOLIMIT,
        .functioncall_frame_limit_bytes = GH_SANDBOX_NOLIMIT,
    };
    ghr_assert(gh_sandbox_ctor(&sandbox, options));

    gh_alloc alloc = gh_alloc_default();

    int pipefd[2];
    assert(pipe(pipefd) >= 0);

    gh_permprompter prompter = gh_permprompter_simpletui(pipefd[0]);
    /* gh_permprompter prompter = gh_permprompter_simpletui(STDIN_FILENO); */

    gh_rpc rpc;
    ghr_assert(gh_rpc_ctor(&rpc, &alloc));
    ghr_assert(gh_rpc_register(&rpc, "open", func_open, GH_RPCFUNCTION_THREADSAFE));

    gh_thread thread;
    ghr_assert(gh_thread_ctor(&thread, (gh_threadoptions) {
        .sandbox = &sandbox,
        .rpc = &rpc,
        .prompter = prompter,
        .name = "thread",
        .safe_id = "my thread",
        .default_timeout_ms = GH_IPC_NOTIMEOUT
    }));

    assert(write(pipefd[1], "x\n", 2) == 2);

    assert(close(pipefd[1]) == 0);

    char tempfile[] = "/tmp/gh-perms-test-XXXXXX";
    int fd;
    assert((fd = mkstemp(tempfile)) >= 0);
    assert(write(fd, "foo", strlen("foo")) == strlen("foo"));
    assert(close(fd) == 0);

    gh_bytebuffer buf;
    ghr_assert(gh_bytebuffer_ctor(&buf, &alloc));
    gh_bytebuffer_append(&buf, "tmp_path = '", sizeof("tmp_path = '") - 1);
    gh_bytebuffer_append(&buf, tempfile, strlen(tempfile));
    gh_bytebuffer_append(&buf, "'", sizeof("'") - 1);
    gh_bytebuffer_append(&buf, "\0", 1);
    ghr_assert(gh_thread_runstringsync(&thread, buf.buffer, buf.size - 1, NULL));

    int in_fd = open("./simple.lua", O_RDONLY);
    assert(in_fd >= 0);
    gh_threadnotif_script script_result;
    ghr_asserterr(GHR_THREAD_LUAFAIL, gh_thread_runfilesync(&thread, in_fd, &script_result));

    ghr_asserterr(GHR_LUA_RUNTIME, script_result.result);
    assert(strstr(script_result.error_msg, "[PERMS_REJECTEDPOLICY]") != NULL);
    assert(close(in_fd) == 0);

    assert(unlink(tempfile) == 0);

    ghr_assert(gh_bytebuffer_dtor(&buf));
    ghr_assert(gh_thread_dtor(&thread, NULL));
    ghr_assert(gh_sandbox_dtor(&sandbox, NULL));

    ghr_assert(gh_rpc_dtor(&rpc));
    assert(close(pipefd[0]) == 0);
    return 0;
}
