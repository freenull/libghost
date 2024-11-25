#ifndef GHOST_STDLIB_H
#define GHOST_STDLIB_H

#include <fcntl.h>
#include <ghost/result.h>
#include <ghost/thread.h>
#include <ghost/rpc.h>

gh_result gh_std_openat(gh_thread * thread, int dirfd, const char * path, int flags, mode_t create_mode, int * out_fd);
gh_result gh_std_unlinkat(gh_thread * thread, int dirfd, const char * path);

#define GH_STD_TEMPFILE_PATHMAX 4096
struct gh_std_tempfile {
    int fd;
    char path[GH_STD_TEMPFILE_PATHMAX];
};
gh_result gh_std_opentemp(gh_thread * thread, const char * prefix, int flags, struct gh_std_tempfile * out_tempfile);

gh_result gh_std_registerinrpc(gh_rpc * rpc);

#endif
