#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ghost/result.h>
#include <ghost/embedded_jail.h>

#ifndef GH_EMBEDDEDJAIL_PROVIDED
unsigned char gh_embeddedjail_exe_data[] = { '\0' };
unsigned int gh_embeddedjail_exe_data_len = 0;
#endif

bool gh_embeddedjail_available(void) {
    return gh_embeddedjail_exe_data_len > 0;
}

gh_result gh_embeddedjail_createfd(int * out_fd) {
    if (!gh_embeddedjail_available()) return GHR_EMBEDDEDJAIL_UNAVAILABLE;
    int fd = memfd_create("gh-embeddedjail", 0);
    if (fd < 0) return ghr_errno(GHR_EMBEDDEDJAIL_CREATEFAIL);

    ssize_t write_res = write(fd, gh_embeddedjail_exe_data, gh_embeddedjail_exe_data_len);
    if (write_res < 0) return ghr_errno(GHR_EMBEDDEDJAIL_WRITEFAIL);
    if (write_res != gh_embeddedjail_exe_data_len) return GHR_EMBEDDEDJAIL_WRITETRUNC;

    *out_fd = fd;

    return GHR_OK;
}

extern char ** environ;

gh_result gh_embeddedjail_exec(const char * name, int options_fd) {
    int fd = -1;
    gh_result res = gh_embeddedjail_createfd(&fd);
    if (ghr_iserr(res)) return res;

    int options_fd_str_len = snprintf(NULL, 0, "%d", options_fd);
    if (options_fd_str_len < 0) return ghr_errno(GHR_SANDBOX_OPTIONSFDCONVERTFAIL);

    char options_fd_str[options_fd_str_len + 1];
    int snprintf_res = snprintf(options_fd_str, (unsigned long)options_fd_str_len + 1, "%d", options_fd);
    if (snprintf_res < 0) return ghr_errno(GHR_SANDBOX_OPTIONSFDCONVERTFAIL);

    const char * const argv[] = {
        name,
        options_fd_str,
        NULL
    };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    // RATIONALE: The cast is okay, exec replaces the entire address space anyway.
    
    char * envp[] = {
        NULL,
        NULL
    };

    const char * sandbox_disabled = getenv("GH_SANDBOX_DISABLED");
    if (sandbox_disabled != NULL && strcmp(sandbox_disabled, "1") == 0) {
        envp[0] = "GH_SANDBOX_DISABLED=1";
    }

    int exec_res = fexecve(fd, (char * const *)argv, envp);
#pragma GCC diagnostic pop
    if (exec_res < 0) return ghr_errno(GHR_EMBEDDEDJAIL_EXECFAIL);

    __builtin_unreachable();
}
