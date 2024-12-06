#include "ghost/perms/pathfd.h"
#include "ghost/perms/procfd.h"
#define _GNU_SOURCE
#include "ghost/perms/permexec.h"
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

int main(void) {
    gh_sandbox sandbox;
    gh_sandboxoptions options = (gh_sandboxoptions) {
        .name = "my sandbox",
        .memory_limit_bytes = GH_SANDBOX_NOLIMIT,
        .functioncall_frame_limit_bytes = GH_SANDBOX_NOLIMIT,
    };
    ghr_assert(gh_sandbox_ctor(&sandbox, options));

    gh_alloc alloc = gh_alloc_default();

    gh_permprompter prompter = gh_permprompter_simpletui(STDIN_FILENO);

    gh_rpc rpc;
    ghr_assert(gh_rpc_ctor(&rpc, &alloc));
    gh_std_registerinrpc(&rpc);

    gh_thread thread;
    ghr_assert(gh_thread_ctor(&thread, (gh_threadoptions) {
        .sandbox = &sandbox,
        .rpc = &rpc,
        .prompter = prompter,
        .name = "thread",
        .safe_id = "my thread",
        .default_timeout_ms = GH_IPC_NOTIMEOUT
    }));

    int perms_file = open("std.ghperm", O_RDONLY);
    assert(perms_file >= 0 || errno == ENOENT);

    if (perms_file >= 0) {
        gh_permparser_error err = {0};
        gh_result res = gh_perms_readfd(&thread.perms, perms_file, &err);
        if (ghr_iserr(res)) {
            fprintf(stderr, "Failed parsing security policy: ");
            ghr_fputs(stderr, res);
            fprintf(stderr, "[%zu:%zu] %s\n", err.loc.row, err.loc.column, err.detail);
            ghr_fail(res);
        }
        assert(close(perms_file) >= 0);
    }

    int script = open("std.lua", O_RDONLY);
    assert(script >= 0);
    gh_threadnotif_script script_result;
    ghr_assert(gh_thread_runfilesync(&thread, script, &script_result));
    assert(close(script) >= 0);

    fprintf(stderr, "Script result: ");
    ghr_fputs(stderr, script_result.result);

    fprintf(stderr, "Script error: %s\n", script_result.error_msg);


    gh_thread_callframe frame;
    ghr_assert(gh_thread_callframe_ctor(&frame));
    ghr_assert(gh_thread_callframe_string(&frame, "Hello, world!"));

    ghr_assert(gh_thread_call(&thread, "luaprint", &frame, NULL));

    const char * s;
    assert(gh_thread_callframe_getstring(&frame, &s) == true);
    printf("RETURN: %s\n", s);

    ghr_assert(gh_thread_callframe_dtor(&frame));

    fprintf(stderr, "Script result: ");
    ghr_fputs(stderr, script_result.result);

    fprintf(stderr, "Script error: %s\n", script_result.error_msg);

    perms_file = open("std.ghperm", O_WRONLY | O_TRUNC | O_CREAT, 0644);
    assert(perms_file >= 0);
    ghr_assert(gh_perms_write(&thread.perms, perms_file));

    gh_result thread_res;
    ghr_assert(gh_thread_dtor(&thread, &thread_res));

    fprintf(stderr, "Thread result: ");
    ghr_fputs(stderr, thread_res);

    gh_result sandbox_res;
    ghr_assert(gh_sandbox_dtor(&sandbox, &sandbox_res));

    fprintf(stderr, "Sandbox result: ");
    ghr_fputs(stderr, sandbox_res);

    ghr_assert(gh_rpc_dtor(&rpc));
    return 0;
}
