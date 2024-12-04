/** @defgroup procfd procfd
 *
 * @brief Interface to the Linux procfs filesystem.
 *
 * @{
 */

#ifndef GHOST_PERMS_PROCFD_H
#define GHOST_PERMS_PROCFD_H

#include <fcntl.h>
#include <ghost/alloc.h>
#include <ghost/result.h>
#include <ghost/perms/pathfd.h>

#ifdef __cplusplus
extern "C" {
#endif

// no ./, no ../, always single slash separating each element, starts with /, no trailing /
typedef struct { const char * ptr; size_t len; } gh_abscanonicalpath;

typedef struct {
    int fd;
    gh_alloc * alloc;
} gh_procfd;

gh_result gh_procfd_ctor(gh_procfd * procfd, gh_alloc * alloc);
gh_result gh_procfd_fdpathctor(gh_procfd * procfd, gh_pathfd fd, gh_abscanonicalpath * out_path);
gh_result gh_procfd_fdpathdtor(gh_procfd * procfd, gh_abscanonicalpath * path);
gh_result gh_procfd_reopen(gh_procfd * procfd, gh_pathfd fd, int flags, mode_t create_mode, int * out_fd);
gh_result gh_procfd_dtor(gh_procfd * procfd);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
