/** @defgroup pathfd pathfd
 *
 * @brief Safe reference to files and directories.
 *
 * @{
 */

#ifndef GHOST_PERMS_PATHFD_H
#define GHOST_PERMS_PATHFD_H

#include <sys/stat.h>
#include <stdbool.h>
#include <ghost/result.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GH_PATHFD_TRAILINGNAMEMAX 4096

typedef struct {
    int fd;
    char trailing_name[GH_PATHFD_TRAILINGNAMEMAX];
} gh_pathfd;

typedef enum {
    GH_PATHFD_NONE = 0,
    GH_PATHFD_ALLOWMISSING = 1 << 0,
    GH_PATHFD_RESOLVELINKS = 1 << 1,
} gh_pathfd_mode;

gh_result gh_pathfd_open(int dirfd, const char * path, gh_pathfd * out_pathfd, gh_pathfd_mode mode);
gh_result gh_pathfd_opentrailing(int dirfd, const char * path, gh_pathfd * out_pathfd);
gh_result gh_pathfd_stat(gh_pathfd pathfd, struct stat * out_statbuf);
bool gh_pathfd_guaranteedtoexist(gh_pathfd pathfd);
gh_result gh_pathfd_close(gh_pathfd pathfd);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
