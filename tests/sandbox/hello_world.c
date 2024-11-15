#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <ghost/alloc.h>
#include <ghost/ipc.h>
#include <ghost/sandbox.h>
#include <ghost/thread.h>
#include <ghost/rpc.h>

static void func_print(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * str_buf;
    size_t str_size;
    if (!gh_rpcframe_argbuf(frame, 0, &str_buf, &str_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }

    if (str_size > INT_MAX) gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EFBIG));

    printf("PRINT: %.*s\n", (int)str_size, str_buf);

    int a = 42;
    gh_rpcframe_returntypedhere(frame, &a);
}

int main(void) {
    gh_sandbox sandbox;

    printf("max functioncall args: %zu\n", (size_t)GH_IPCMSG_FUNCTIONCALL_MAXARGS);

    gh_sandboxoptions options = {0};
    strcpy(options.name, "ghost-test-sandbox");
    options.memory_limit_bytes = GH_SANDBOX_NOLIMIT;
    options.functioncall_frame_limit_bytes = GH_SANDBOX_NOLIMIT;

    gh_alloc alloc = gh_alloc_default();

    ghr_assert(gh_sandbox_ctor(&sandbox, options));

    gh_thread thread;
    ghr_assert(gh_sandbox_newthread(&sandbox, &alloc, "thread", &thread));
    gh_rpc_register(gh_thread_rpc(&thread), "print", func_print);

    ghr_assert(gh_thread_runtestcase(&thread, 1));
    ghr_assert(gh_thread_process(&thread));
    ghr_assert(gh_thread_requestquit(&thread));
    ghr_assert(gh_thread_dtor(&thread));

    ghr_assert(gh_sandbox_requestquit(&sandbox));
    
    ghr_assert(gh_sandbox_wait(&sandbox));
    ghr_assert(gh_sandbox_dtor(&sandbox));

    return 0;
}
