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

    gh_sandboxoptions options = {0};
    strcpy(options.name, "ghost-test-sandbox");
    options.memory_limit_bytes = GH_SANDBOX_NOLIMIT;
    options.functioncall_frame_limit_bytes = GH_SANDBOX_NOLIMIT;

    gh_alloc alloc = gh_alloc_default();

    ghr_assert(gh_sandbox_ctor(&sandbox, options));

    gh_rpc rpc;
    ghr_assert(gh_rpc_ctor(&rpc, &alloc, gh_permprompter_simpletui(STDIN_FILENO)));
    ghr_assert(gh_rpc_register(&rpc, "print", func_print, GH_RPCFUNCTION_THREADSAFE));

    gh_thread thread;
    ghr_assert(gh_sandbox_newthread(&sandbox, &rpc, "thread", "thread", GH_IPC_NOTIMEOUT, &thread));

    char s[] =
        "local ghost = require('ghost')\n"
        "local res = ghost.call('print', 'int', 'Hello, world!')\n"
        "print('result:', res)\n"
        ;

    ghr_assert(gh_thread_runstring(&thread, s, strlen(s), NULL));
    while (true) {
        gh_threadnotif notif;
        ghr_assert(gh_thread_process(&thread, &notif));

        if (notif.type == GH_THREADNOTIF_SCRIPTRESULT) {
            if (notif.script.result == GHR_LUA_RUNTIME) {
                fprintf(stderr, "ERROR EXECUTING LUA:\n%s\n", notif.script.error_msg);
            }
            ghr_assert(notif.script.result);
            break;
        }
    }
    ghr_assert(gh_thread_dtor(&thread, NULL));

    ghr_assert(gh_rpc_dtor(&rpc));

    ghr_assert(gh_sandbox_dtor(&sandbox, NULL));

    return 0;
}
