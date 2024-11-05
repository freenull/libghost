#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <ghost/sandbox.h>

int main(void) {
    gh_sandbox sandbox;
    gh_result res = GHR_OK;

    res = gh_sandbox_ctor(&sandbox, (gh_sandboxoptions) {
        .name = "test-sandbox",
        .memory_limit = GH_SANDBOX_NOMEMLIMIT
    });
    ghr_assert(res);
    
    ghr_assert(gh_sandbox_wait(&sandbox));

    return 0;
}
