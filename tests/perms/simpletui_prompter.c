#define _GNU_SOURCE
#include <string.h>
#include <ghost/perms/perms.h>
#include <ghost/perms/prompt.h>

int main(void) {
    int pipefd[2];
    assert(pipe(pipefd) >= 0);

    gh_permprompter p = gh_permprompter_simpletui(pipefd[0]);

    gh_permrequest req = {0};
    strcpy(req.source, "test");
    strcpy(req.group, "filesystem");
    strcpy(req.resource, "node");
    strcpy(req.fields[0].key, "path");
    req.fields[0].value.buffer = "/foo/bar/baz";
    req.fields[0].value.size = strlen(req.fields[0].value.buffer);

    strcpy(req.fields[1].key, "mode_self");
    req.fields[1].value.buffer = "read,createdir";
    req.fields[1].value.size = strlen(req.fields[1].value.buffer);

    strcpy(req.fields[2].key, "mode_children");
    req.fields[2].value.buffer = "read,write";
    req.fields[2].value.size = strlen(req.fields[2].value.buffer);

    assert(write(pipefd[1], "a\n", 2) == 2);
    gh_permresponse resp = {0};
    ghr_assert(gh_permprompter_request(&p, &req, &resp));
    fprintf(stderr, "response: %d\n", resp);
    assert(resp == GH_PERMRESPONSE_ACCEPTREMEMBER);

    assert(close(pipefd[0]) == 0);
    assert(close(pipefd[1]) == 0);
    return 0;
}
