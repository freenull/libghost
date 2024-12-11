/** @defgroup sandbox Sandbox
 *
 * @brief Library representation of a live jail process with connected IPC.
 *
 * @{
 */

#ifndef GHOST_SANDBOX_H
#define GHOST_SANDBOX_H

#include <stdlib.h>
#include <ghost/result.h>
#include <ghost/ipc.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum size (with null terminator) of sandbox name. */
#define GHOST_SANDBOXOPTIONS_NAME_MAX 256

/** @brief Value representing no limit when used as value of @ref gh_sandboxoptions and
 *         @ref gh_threadoptions fields related to limits.
 */
#define GH_SANDBOX_NOLIMIT 0
#define GH_SANDBOX_MAXWAITSUBJAILMS 5000

#define GH_SANDBOX_TIMETOQUITMS 4000

/** @brief Sandbox options. */
typedef struct {
    /** Name. */
    char name[GHOST_SANDBOXOPTIONS_NAME_MAX];

    /** Limit of memory usage by sandbox in bytes. */
    size_t memory_limit_bytes;
    /** Limit of RPC frame size inside sandbox in bytes. */
    size_t functioncall_frame_limit_bytes;

    /** @brief File descriptor of jail IPC socket. Not intended to be set by user, will be reset during sandbox spawn. */
    int jail_ipc_sockfd;
} gh_sandboxoptions;

/** @brief Sandbox. */
typedef struct {
    /** @brief PID of jail process. */
    pid_t pid;
    /** @brief Options. */
    gh_sandboxoptions options;
    /** @brief IPC instance. */
    gh_ipc ipc;
} gh_sandbox;

/** @brief Construct a sandbox object.
 *
 * @note This function spawns a new jail process.
 *
 * @param sandbox Pointer to the unconstructed sandbox object.
 * @param options Structure containing configuration of the jail process and its sandbox.
 *
 * @return Result code.
 */
gh_result gh_sandbox_ctor(gh_sandbox * sandbox, gh_sandboxoptions options);

/** @brief Read sandbox options from a file pointed to by a file descriptor.
 *
 * @param fd          File descriptor pointing to the file containing the sandbox options structure.
 * @param out_options Pointer to the uninitialized sandbox options structure.
 */
gh_result gh_sandboxoptions_readfrom(int fd, gh_sandboxoptions * out_options);

/** @brief Destroy a sandbox object.
 *
 * @param sandbox Pointer to the sandbox object.
 *
 * @return Result code.
 */
gh_result gh_sandbox_dtor(gh_sandbox * sandbox, gh_result * out_jailresult);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
