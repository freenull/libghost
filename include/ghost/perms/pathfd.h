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

/* @brief Structure representing a safe reference to a filesystem node,
 *        unaffected by changes in the state of the filesystem hierarchy.
 *
 * This structure may represent two types of entries.
 *
 * @par Existing file
 *
 * @ref gh_pathfd.trailing_name is empty. In this case, the structure always
 * represents this existing file, and moving/deleting it does not invalidate
 * the structure.
 *
 * @par New/missing file
 *
 * @ref gh_pathfd.trailing_name contains a filename. In this case, the structure
 * always represents a single *name* inside a directory. @ref gh_pathfd.fd 
 * refers to a directory opened with mode O_PATH | O_DIRECTORY. Moving or deleting the
 * directory does not invalidate the structure and neither does creating the file,
 * however until an operation is executed, no guarantee is made about what can
 * be done with the path (for example, a file may already exist at that path).
 * In other words, in this mode the *parent directory* is the reference, and
 * the @ref gh_pathfd.trailing_name is an additional piece of information for
 * some functions that deal with creating new files or moving files.
 *
 */
typedef struct {
    /* @brief Referenced file descriptor in O_PATH mode. May be a directory. */
    int fd;

    /* @brief Filename, if the @ref gh_pathfd refers to a new/missing file. */
    char trailing_name[GH_PATHFD_TRAILINGNAMEMAX];
} gh_pathfd;


/* @brief Flags for @ref gh_pathfd_open. */
typedef enum {
    /* @brief Empty flags. Missing files return an error and if the last element
     *        of the path is a symlink, it will not be resolved. */
    GH_PATHFD_NONE = 0,

    /* @brief If the file is missing at the first attempt to open an O_PATH
     *        descriptor to it, will open a @ref gh_pathfd in new/missing mode.
     */
    GH_PATHFD_ALLOWMISSING = 1 << 0,

    /* @brief If the last element of the path (e.g. 'c' in '/a/b/c') is a symlink,
     *        it will be resolved. Works with @ref GH_PATHFD_ALLOWMISSING, in
     *        which case either an existing symlink will be resolved, or a @ref gh_pathfd
     *        in new/missing mode will be returned referencing the provided path.
     */
    GH_PATHFD_RESOLVELINKS = 1 << 1,
} gh_pathfd_mode;

/* @brief Open a @ref gh_pathfd referencing a @p path inside the directory @p dirfd.
 *        
 * @param dirfd     File descriptor of the parent directory. An @ref AT_FDCWD
 *                  @p dirfd means the current directory.
 *
 * @param path      Path inside the parent directory. If absolute, @p dirfd is
 *                  ignored.
 *
 * @param[out] out_pathfd Will hold the newly created @ref gh_pathfd.
 *
 * @param mode      Open mode, see @ref gh_pathfd_mode.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_pathfd_open(int dirfd, const char * path, gh_pathfd * out_pathfd, gh_pathfd_mode mode);

/* @brief Open a @ref gh_pathfd referencing a @p path in new/missing mode.
 *        
 * @param dirfd     File descriptor of the parent directory. An @ref AT_FDCWD
 *                  @p dirfd means the current directory.
 *
 * @param path      Path inside the parent directory. If absolute, @p dirfd is
 *                  ignored.
 *
 * @param[out] out_pathfd Will hold the newly created @ref gh_pathfd.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_pathfd_opentrailing(int dirfd, const char * path, gh_pathfd * out_pathfd);

/* @brief Perform a `stat(2)` operation safely on a @ref gh_pathfd.
 *        
 * @param pathfd    @ref gh_pathfd structure referencing a filesystem node.
 *
 * @param[out] out_statbuf Will hold the stat buffer, as per `stat(2)`.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_pathfd_stat(gh_pathfd pathfd, struct stat * out_statbuf);

/* @brief Check if the file referenced by a @ref gh_pathfd is guaranteed to
 *        always exist.
 *
 * This operation is equivalent to checking if the @ref gh_pathfd has been
 * opened in existing file mode.
 *        
 * @param pathfd    @ref gh_pathfd structure referencing a filesystem node.
 *
 * @return True if the @ref gh_pathfd is guaranteed to always exist, false
 *         otherwise.
 */
bool gh_pathfd_guaranteedtoexist(gh_pathfd pathfd);

/* @brief Close a @ref gh_pathfd.
 *
 * @param pathfd    @ref gh_pathfd structure referencing a filesystem node.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_pathfd_close(gh_pathfd pathfd);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
