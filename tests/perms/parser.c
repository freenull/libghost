#define _GNU_SOURCE
#include <sys/mman.h>
#include <ghost/rpc.h>
#include <ghost/perms/prompt.h>
#include <ghost/result.h>
#include <ghost/alloc.h>
#include <ghost/perms/perms.h>
#include <ghost/strings.h>
#include <ghost/sandbox.h>
#include <ghost/thread.h>

int main(void) {
    gh_sandbox sandbox;
    gh_sandboxoptions options = (gh_sandboxoptions) {
        .name = "my sandbox",
        .memory_limit_bytes = GH_SANDBOX_NOLIMIT,
        .functioncall_frame_limit_bytes = GH_SANDBOX_NOLIMIT,
    };
    ghr_assert(gh_sandbox_ctor(&sandbox, options));

    gh_alloc alloc = gh_alloc_default();

    // the pipe will emulate STDIN for the simpletui prompter
    int pipefd[2];
    assert(pipe(pipefd) >= 0);
    gh_permprompter prompter = gh_permprompter_simpletui(pipefd[0]);

    gh_rpc rpc;
    ghr_assert(gh_rpc_ctor(&rpc, &alloc));

    gh_thread thread;
    ghr_assert(gh_thread_ctor(&thread, (gh_threadoptions) {
        .sandbox = &sandbox,
        .rpc = &rpc,
        .prompter = prompter,
        .name = "thread",
        .safe_id = "my thread",
        .default_timeout_ms = GH_IPC_NOTIMEOUT
    }));

    gh_perms * perms = &thread.perms;

    int permfd = open("parser_input.txt", O_RDONLY);
    assert(permfd >= 0);

    gh_permparser_error error;
    gh_result res = gh_perms_readfd(perms, permfd, &error);
    if (ghr_iserr(res)) {
        fprintf(stderr, "Parser error detail: [%zu:%zu] %s\n", error.loc.row, error.loc.column, error.detail);
        ghr_fail(res);
    }

    // TESTING PERMISSIONS ON /tmp
    gh_pathfd fd;
    ghr_assert(gh_pathfd_open(AT_FDCWD, "/tmp", &fd, GH_PATHFD_ALLOWMISSING));

    // both READ and CREATEDIR are 'self accept' in /tmp entry
    ghr_assert(gh_perms_gatefile(&thread.perms, thread.safe_id, fd, GH_PERMFS_CREATEDIR | GH_PERMFS_READ, NULL));

    // WRITE is 'self reject'
    ghr_asserterr(GHR_PERMS_REJECTEDPOLICY, gh_perms_gatefile(&thread.perms, thread.safe_id, fd, GH_PERMFS_CREATEDIR | GH_PERMFS_READ | GH_PERMFS_WRITE, NULL));

    assert(write(pipefd[1], "n\n", 2) == 2); // force next simpletui request to REJECT
    // CREATEFILE is explicit 'self prompt', so the prompt will appear and exit with REJECT (simpletui input n/N)
    ghr_asserterr(GHR_PERMS_REJECTEDUSER, gh_perms_gatefile(&thread.perms, thread.safe_id, fd, GH_PERMFS_CREATEDIR | GH_PERMFS_READ | GH_PERMFS_CREATEFILE, NULL));
    ghr_assert(gh_pathfd_close(fd));


    // TESTING PERMISISON ON /tmp/foobar.txt (pathfd handles non existing files correctly)
    ghr_assert(gh_pathfd_open(AT_FDCWD, "/tmp/foobar.txt", &fd, GH_PATHFD_ALLOWMISSING));


    // READ in /tmp is 'children accept'
    ghr_assert(gh_perms_gatefile(&thread.perms, thread.safe_id, fd, GH_PERMFS_READ, NULL));
    assert(write(pipefd[1], "x\n", 2) == 2); // force next simpletui request to REJECT AND REMEMBER

    // CREATEFILE is not specified under 'children' in /tmp, so default action is used (prompt), prompt will return REJECT AND REMEMBER
    ghr_asserterr(GHR_PERMS_REJECTEDUSER, gh_perms_gatefile(&thread.perms, thread.safe_id, fd, GH_PERMFS_CREATEFILE, NULL));

    // because previous prompt was REJECTED AND REMEMBER, next attempt returns rejected by policy instead of rejected by user
    ghr_asserterr(GHR_PERMS_REJECTEDPOLICY, gh_perms_gatefile(&thread.perms, thread.safe_id, fd, GH_PERMFS_CREATEFILE, NULL));

    // WRITE is explicitly 'children reject' under /tmp
    ghr_asserterr(GHR_PERMS_REJECTEDPOLICY, gh_perms_gatefile(&thread.perms, thread.safe_id, fd, GH_PERMFS_WRITE, NULL));
    ghr_assert(gh_perms_gatefile(&thread.perms, thread.safe_id, fd, GH_PERMFS_READ, NULL));
    ghr_assert(gh_pathfd_close(fd));

    // TESTING PERMISSIONS ON /foobar.txt
    ghr_assert(gh_pathfd_open(AT_FDCWD, "/foobar.txt", &fd, GH_PATHFD_ALLOWMISSING));

    // READ on /foobar.txt is 'self accept'
    ghr_assert(gh_perms_gatefile(&thread.perms, thread.safe_id, fd, GH_PERMFS_READ, NULL));

    assert(write(pipefd[1], "n\n", 2) == 2); // force next simpletui request to REJECT

    // no mode other than READ is specified on /foobar.txt, so default action is used (prompt), prompt will return REJECT
    ghr_asserterr(GHR_PERMS_REJECTEDUSER, gh_perms_gatefile(&thread.perms, thread.safe_id, fd, GH_PERMFS_CREATEFILE, NULL));

    assert(close(permfd) >= 0);

    ghr_assert(gh_pathfd_close(fd));

    // generate a .ghperm output file, and check if it matches the expected output
    // (all of the rules should be the same, with the exception of a new /tmp/foobar.txt entry
    //  with self reject on CREATEFILE)
    int outputfd = memfd_create("output", 0);
    ghr_assert(gh_perms_write(perms, outputfd));
    assert(lseek(outputfd, 0, SEEK_SET) >= 0);
    char output_buf[4096] = {0};
    assert(read(outputfd, output_buf, sizeof(output_buf)) >= 0);
    char output_check_buf[4096] = {0};
    int outputcheckfd = open("parser_output.txt", O_RDONLY);
    assert(outputcheckfd >= 0);
    assert(read(outputcheckfd, output_check_buf, sizeof(output_check_buf)) >= 0);
    assert(strcmp(output_buf, output_check_buf) == 0);
    assert(close(outputfd) >= 0);
    assert(close(outputcheckfd) >= 0);

    ghr_assert(gh_thread_dtor(&thread, NULL));

    ghr_assert(gh_sandbox_dtor(&sandbox, NULL));

    ghr_assert(gh_rpc_dtor(&rpc));

    assert(close(pipefd[0]) >= 0);
    assert(close(pipefd[1]) >= 0);
}
