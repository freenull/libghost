#define _GNU_SOURCE
#include <sys/wait.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>
#include <ghost/perms/perms.h>
#include <ghost/perms/prompt.h>
#include <ghost/sandbox.h>
#include <ghost/thread.h>
#include <ghost/stdlib.h>

#define assert(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "Assertion failed: %s\n", # cond); \
        } \
    } while (0)

typedef struct {
    int dirfd;
    const char * path;
} extprompter_loc;

static gh_result ext_prompter(const gh_permrequest * req, void * userdata, gh_permresponse * out_response) {
    extprompter_loc * loc = (extprompter_loc *)userdata;

#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"
#else
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#endif
    char * argv[] = {
        loc->path,

        req->source,
        req->group,
        req->resource,

        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,

        NULL,
    };
#pragma GCC diagnostic pop

    for (size_t i = 0; i < GH_PERMREQUEST_MAXFIELDS; i++) {
        const gh_permrequest_field * field = req->fields + i;
        if (field->key[0] == '\0') continue;

        size_t key_len = strlen(field->key);
        size_t arg_len = key_len + 1 + field->value.size;
        char * arg = malloc(sizeof(char) * (arg_len + 1));

        strncpy(arg, field->key, key_len);
        arg[key_len] = '=';
        strncpy(arg + key_len + 1, field->value.buffer, field->value.size);
        arg[key_len + 1 + field->value.size] = '\0';

        argv[i + 4] = arg;
    }

    pid_t child_pid;
    if ((child_pid = fork()) == 0) {
        if (execveat(loc->dirfd, loc->path, argv, environ, 0) < 0) {
            fprintf(stderr, "extprompt exec failed: %s\n", strerror(errno));
            exit(10);
        }
    }

    for (char ** arg = argv + 4; *arg != NULL; arg++) {
        free(*arg);
    }

    int status;
    assert(waitpid(child_pid, &status, 0) >= 0);

    int exit_code = -1;
    if (WIFEXITED(status)) exit_code = WEXITSTATUS(status);
    else abort();

    gh_result res = GHR_OK;

    if      (exit_code == 0) *out_response = GH_PERMRESPONSE_ACCEPTREMEMBER;
    else if (exit_code == 1) *out_response = GH_PERMRESPONSE_ACCEPT;
    else if (exit_code == 2) *out_response = GH_PERMRESPONSE_REJECTREMEMBER;
    else if (exit_code == 3) *out_response = GH_PERMRESPONSE_REJECT;
    else res = GHR_UNKNOWN;

    return res;
}

int main(int argc, const char * const * argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s file [files...]\n", argv[0]);
        fprintf(stderr, "measures total size of all passed in files\n");
        return 1;
    }

    const char * program_name = argv[0];
    size_t program_name_len = strlen(program_name);
    if (program_name_len > PATH_MAX - 1) {
        fprintf(stderr, "argv[0] is too long\n");
        return 1;
    }

    char self_dirname_buf[PATH_MAX];
    strcpy(self_dirname_buf, program_name);

    char * self_dirname = dirname(self_dirname_buf);
    int self_dirfd = open(self_dirname, O_PATH | O_DIRECTORY, 0);
    assert(self_dirfd >= 0);

    gh_sandbox sandbox;
    gh_sandboxoptions options = (gh_sandboxoptions) {
        .name = "my sandbox",
        .memory_limit_bytes = GH_SANDBOX_NOLIMIT,
        .functioncall_frame_limit_bytes = GH_SANDBOX_NOLIMIT,
    };
    ghr_assert(gh_sandbox_ctor(&sandbox, options));

    gh_alloc alloc = gh_alloc_default();

    extprompter_loc loc = {
        .dirfd = self_dirfd,
        .path = "prompter.py"
    };
    gh_permprompter prompter = gh_permprompter_new(ext_prompter, &loc);

    gh_rpc rpc;
    ghr_assert(gh_rpc_ctor(&rpc, &alloc));
    ghr_assert(gh_std_registerinrpc(&rpc));

    gh_thread thread;
    ghr_assert(gh_thread_ctor(&thread, (gh_threadoptions) {
        .sandbox = &sandbox,
        .rpc = &rpc,
        .prompter = prompter,
        .name = "thread",
        .safe_id = "my thread",
        .default_timeout_ms = GH_IPC_NOTIMEOUT
    }));

    int permsfd = open("./filesize.ghperm", O_RDONLY);
    assert(permsfd >= 0 || errno == ENOENT);
    if (permsfd >= 0) {
        ghr_assert(gh_perms_readfd(&thread.perms, permsfd, NULL));
        assert(close(permsfd) >= 0);
    }

    int in_fd = openat(self_dirfd, "filesize.lua", O_RDONLY);
    assert(in_fd >= 0);

    ghr_assert(gh_thread_setstringtable(&thread, "argv", argv, argc));

    gh_threadnotif_script script_result;
    ghr_assert(gh_thread_runfilesync(&thread, in_fd, &script_result));

    bool lua_fail = false;
    if (ghr_iserr(script_result.result)) {
        fprintf(stderr, "Lua error: ");
        ghr_fputs(stderr, script_result.result);
        fprintf(stderr, "Details: %s\n", script_result.error_msg);
        lua_fail = true;
    }

    assert(close(in_fd) == 0);

    permsfd = open("./filesize.ghperm", O_WRONLY | O_CREAT, 0600);
    assert(permsfd >= 0);
    ghr_assert(gh_perms_write(&thread.perms, permsfd));
    assert(close(permsfd) >= 0);

    ghr_assert(gh_thread_dtor(&thread, NULL));
    ghr_assert(gh_sandbox_dtor(&sandbox, NULL));

    ghr_assert(gh_rpc_dtor(&rpc));

    assert(close(self_dirfd) >= 0);
    return lua_fail ? 1 : 0;
}
