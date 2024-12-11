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

/** @brief Persistent reference to /proc/self/fd (ProcFD). */
typedef struct {
    /** @brief File descriptor of directory /proc/self/fd. */
    int fd;

    /** @brief Allocator used to allocate memory for paths. */
    gh_alloc * alloc;
} gh_procfd;

/** @brief Construct a new ProcFD.
 *
 * @param procfd   Pointer to unconstructed memory that will hold the new instance.
 * @param alloc    Allocator used to allocate memory for paths.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_procfd_ctor(gh_procfd * procfd, gh_alloc * alloc);

/** @brief Destroy a ProcFD.
 *
 * @param procfd Pointer to a constructed centralized permission system instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_procfd_dtor(gh_procfd * procfd);

/** @brief Retrieve an absolute canonical path from a PathFD.
 *
 * @warning The state of the filesystem may change after this call in such a way,
 *          that the retrieved path no longer points to the same resource as the
 *          PathFD. Make sure to only use the retrieved path in situations where
 *          this is not a problem.
 *
 * @warning The resource written to @p out_path must be destroyed using the
 *          @ref gh_procfd_fdpathdtor function.
 *
 * @param procfd Pointer to a constructed centralized permission system instance.
 * @param fd     PathFD to a filesystem node.
 * @param[out] out_path Will contain the absolute canonical path that represents
 *                      or had represented the resource indicated by the PathFD.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_procfd_fdpathctor(gh_procfd * procfd, gh_pathfd fd, gh_abscanonicalpath * out_path);

/** @brief Destroy an absolute canonical path retrieved with @ref gh_procfd_fdpathctor.
 *
 * @param procfd Pointer to a constructed centralized permission system instance.
 * @param path   Absolute canonical path retrieved by @ref gh_procfd_fdpathctor.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_procfd_fdpathdtor(gh_procfd * procfd, gh_abscanonicalpath * path);

/** @brief Safely reopen a PathFD as a file descriptor.
 *
 * @note This function does not involve the use of file paths. Instead, the file
 *       descriptor is reopened through procfs, ensuring safety from TOCTOU.
 *
 * @warning Despite effectively being an `open(2)` wrapper, errors are handled
 *          by the function. A negative file descriptor will never be written
 *          to @p out_fd. Check the result code to detect errors.
 *
 * @param procfd Pointer to a constructed centralized permission system instance.
 * @param fd     PathFD to reopen.
 * @param flags  Flags, as in `open(2)`.
 * @param create_mode  Mode, as in `open(2)`.
 * @param[out] out_fd  Will hold the newly opened file descriptor.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_procfd_reopen(gh_procfd * procfd, gh_pathfd fd, int flags, mode_t create_mode, int * out_fd);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
