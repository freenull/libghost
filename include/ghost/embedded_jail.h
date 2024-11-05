#ifndef GHOST_EMBEDDED_JAIL_H
#define GHOST_EMBEDDED_JAIL_H

#include <stdbool.h>
#include <ghost/result.h>

/** @brief Data of the embedded jail executable. */
extern unsigned char gh_embeddedjail_exe_data[];

/** @brief Length of the embedded jail executable data.
 * @par May be 0 if the jail executable has not been embedded in this build.
 */
extern unsigned int gh_embeddedjail_exe_data_len;

/** @brief Checks if the jail executable has been embedded into the shared library.
 *
 * @par The jail executable is not included in the initial build of the shared library due to a cyclical dependency.
 *      The jail is built later and linked against the initial shared library, then the library is rebuilt with the executable embedded.
 *
 * @return True if the jail executable is embedded. False if not.
 */
bool gh_embeddedjail_available(void);

/** @brief Creates a memory file containing the jail executable.
 *
 * @param out_fd Output pointer for the file descriptor of the new memory file.
 *
 * @return Result code.
 */
gh_result gh_embeddedjail_createfd(int * out_fd);

/** @brief Replaces current address space with the embedded jail executable if available.
 *
 * @param name Name of the process passed as argv[0].
 * @param options_fd File descriptor containing sandbox options to be passed as argv[1].
 *
 * @return On success, the function doesn't return. On failure, a result code is returned.
 */
gh_result gh_embeddedjail_exec(const char * name, int options_fd);

#endif
