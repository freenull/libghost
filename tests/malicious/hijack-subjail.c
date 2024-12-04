/** @file Subjail hijack test
 *
 * This file constructs a new sandbox, a new thread, executes hijack-subjail.lua
 * in the new thread, then prints any errors.
 *
 * The hijack-subjail.lua script does the following:
 * - It uses LuaJIT FFI to define some libghost structures and types.
 * - It uses the Lua debug table to "steal" the gh_ipc pointer used by the subjail.
 *   The pointer is normally inaccessible, but this is not for security reasons (as
 *   demonstrated, it is trivial to access it).
 * - It uses this gh_ipc pointer to send a fake LUARESULT message with:
 *   - A fake error result ("Cannot allocate memory")
 *   - A full error_msg buffer without a terminating null byte
 *   - Script ID 0, which should match the script ID allocated by
 *     gh_thread_runfiesync
 *
 * The host process does not die (despite the generous use of ghr_assert).
 * The script result is clearly separated from the "trusted" host result, and the
 * error message is null terminated by the time the host receives it.
 *
 * The point of the test (as with other tests in malicious/) is to demonstrate that
 * the right thing is done without much intervention on part of the library's user,
 * even when the remote processes are explicitly malicious and are trying to cause
 * undefined or undesired behavior in the host.
 */

#include "ghost/result.h"
#include <ghost/alloc.h>
#include <ghost/perms/prompt.h>
#include <ghost/rpc.h>
#include <ghost/sandbox.h>
#include <ghost/thread.h>

int main(void) {
    gh_sandbox sbox;
    ghr_assert(gh_sandbox_ctor(&sbox, (gh_sandboxoptions) {
        .name = "my sandbox",
        .memory_limit_bytes = GH_SANDBOX_NOLIMIT,
        .functioncall_frame_limit_bytes = GH_SANDBOX_NOLIMIT
    }));

    gh_alloc alloc = gh_alloc_default();

    gh_rpc rpc;
    ghr_assert(gh_rpc_ctor(&rpc, &alloc));

    gh_thread thread;
    ghr_assert(gh_thread_ctor(&thread, (gh_threadoptions) {
        .sandbox = &sbox,
        .rpc = &rpc,
        .prompter = gh_permprompter_simpletui(STDIN_FILENO),
        .name = "thread",
        .safe_id = "thread script",
        .default_timeout_ms = GH_IPC_NOTIMEOUT,
    }));

    int fd = open("hijack-subjail.lua", O_RDONLY);
    assert(fd >= 0);
    gh_threadnotif_script script_res;
    ghr_assert(gh_thread_runfilesync(&thread, fd, &script_res));
    assert(close(fd) >= 0);

    gh_result thread_res;
    ghr_assert(gh_thread_dtor(&thread, &thread_res));

    ghr_assert(gh_rpc_dtor(&rpc));

    gh_result sandbox_res;
    ghr_assert(gh_sandbox_dtor(&sbox, &sandbox_res));

    if (ghr_iserr(thread_res)) {
        fprintf(stderr, "Thread error: ");
        ghr_fputs(stderr, thread_res);

        assert(ghr_isok(thread_res));
    }

    if (ghr_iserr(sandbox_res)) {
        fprintf(stderr, "Sandbox error: ");
        ghr_fputs(stderr, sandbox_res);

        assert(ghr_isok(sandbox_res));
    }

    if (ghr_iserr(script_res.result)) {
        fprintf(stderr, "Script error: ");
        ghr_fputs(stderr, script_res.result);
        fprintf(stderr, "%s\n", script_res.error_msg);

        ghr_asserterr(GHR_ALLOC_ALLOCFAIL, script_res.result);
        assert(ghr_frag_errno(script_res.result) == ENOMEM);
        assert(strlen(script_res.error_msg) == sizeof(script_res.error_msg) - 1);
    }

    return 0;
}
