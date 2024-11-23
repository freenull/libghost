#ifndef GHOST_PERMS_PATHFD_H
#define GHOST_PERMS_PATHFD_H

#include <ghost/result.h>

#define GH_PATHFD_TRAILINGNAMEMAX 4096

typedef struct {
    int fd;
    char trailing_name[GH_PATHFD_TRAILINGNAMEMAX];
} gh_pathfd;

gh_result gh_pathfd_open(int dirfd, const char * path, gh_pathfd * out_pathfd);
gh_result gh_pathfd_close(gh_pathfd pathfd);

#endif
