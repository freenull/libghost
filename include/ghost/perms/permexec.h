/** @defgroup permfs permfs
 *
 * @brief External program execution permission handler.
 *
 * @{
 */

#ifndef GHOST_PERMS_PERMEXEC_H
#define GHOST_PERMS_PERMEXEC_H

#include <linux/limits.h>
#include <stdint.h>
#include <ghost/perms/pathfd.h>
#include <ghost/perms/procfd.h>
#include <ghost/perms/prompt.h>
#include <ghost/perms/parser.h>
#include <ghost/perms/writer.h>
#include <ghost/result.h>
#include <ghost/alloc.h>
#include <ghost/dynamic_array.h>
#include <ghost/sha256provider.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Mode of a PermExec security policy entry. */
typedef enum {
    /** @brief Accept request matching this entry. */
    GH_PERMEXEC_ACCEPT,

    /** @brief Reject request matching this entry. */
    GH_PERMEXEC_REJECT,

    /** @brief Prompt for permission to fulfill request matching this entry. */
    GH_PERMEXEC_PROMPT
} gh_permexec_mode;

/** @brief PermExec security policy entry. */
typedef struct {
    /** @brief Mode. */
    gh_permexec_mode mode;

    /** @brief Combined SHA256 hash of the executable and `argv`. */
    gh_sha256 combined_hash;
} gh_permexec_entry;

/** @brief Build a PermExec combined SHA256 hash.
 *
 * This hash consists of:
 * - SHA256 of the *binary contents* of the executable.
 * - SHA256 of @p argc entries of @p argv.
 *
 * These element hashes are concatenated (in raw byte form), then the combined
 * hash is created by hashing that concatenated buffer.
 *
 * @param alloc  Memory allocator to use when reading the executable's binary
 *               content.
 *
 * @param exe_fd File descriptor of the executable file. Must allow reading.
 * 
 * @param argc   Argument count.
 * @param argv   Argument vector, like in `main()`. The trailing NULL is
 *               unnecessary.
 *
 * @param[out] out_hash Will contain the combined hash.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permexec_hash_build(gh_alloc * alloc, int exe_fd, int argc, char * const * argv, gh_sha256 * out_hash);

#define GH_PERMEXECHASHLIST_INITIALCAPACITY 128

#define GH_PERMEXECHASHLIST_MAXCAPACITY GH_DYNAMICARRAY_NOMAXCAPACITY

/** @brief Dynamically resizable PermExec security policy. */
typedef struct {
    /** @brief Allocator. */
    gh_alloc * alloc;

    /** @brief Buffer of entries. */
    gh_permexec_entry * buffer;

    /** @brief Size of dynamic array. */
    size_t size;

    /** @brief Capacity of dynamic array. */
    size_t capacity;
} gh_permexec_hashlist;

/** @brief Maximum count of arguments to an executable across the PermExec API. */
#define GH_PERMEXEC_MAXARGS 2048

/** @brief Maximum number of allowed (passed through) environment variables. */
#define GH_PERMEXEC_MAXALLOWEDENV 32

/** @brief PermExec permission system. */
typedef struct {
    /** @brief Default mode for entries not found in the security policy.
     *        @ref GH_PERMEXEC_REJECT by default for security reasons. May be
     *        changed by application if needed.
     */
    gh_permexec_mode default_mode;

    /** @brief Dynamic array of entries. */
    gh_permexec_hashlist hashlist;

    /** @brief List of environment variable names. Each environment variable in
     *        this list will be passed through the environment to the external
     *        program. Other environment variables will not be passed.
     */
    const char * allowed_env[GH_PERMEXEC_MAXALLOWEDENV];
} gh_permexec;

#define GH_PERMEXEC_DESCRIPTIONMAX 512

#define GH_PERMEXEC_CMDLINEMAX 4096

#define GH_PERMEXEC_ENVMAX 4096

typedef struct {
    gh_permrequest request;

    char description_buffer[GH_PERMEXEC_DESCRIPTIONMAX];

    char cmdline_buffer[GH_PERMEXEC_CMDLINEMAX];

    char env_buffer[GH_PERMEXEC_ENVMAX];
} gh_permexec_reqdata;

gh_result gh_permexec_hashlist_ctor(gh_permexec_hashlist * list, gh_alloc * alloc);
gh_result gh_permexec_hashlist_add(gh_permexec_hashlist * list, gh_permexec_entry * entry);
gh_result gh_permexec_hashlist_tryget(gh_permexec_hashlist * list, gh_sha256 * hash, gh_permexec_entry ** out_entry);
gh_result gh_permexec_hashlist_dtor(gh_permexec_hashlist * list);

/** @brief Construct a new PermExec permission system.
 *
 * @param permexec Pointer to unconstructed memory that will hold the new instance.
 * @param alloc    Allocator for the dynamic array of policy entries.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permexec_ctor(gh_permexec * permexec, gh_alloc * alloc);

/** @brief Destroy a new PermExec permission system.
 *
 * @param permexec Pointer to a constructed PermExec instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permexec_dtor(gh_permexec * permexec);

/** @brief Check whether an operation to spawn an external program is allowed
 *         by the security policy.
 *
 * @param permexec Pointer to a constructed PermExec instance.
 * @param prompter Prompter to use if the policy indicates a need to prompt the
 *                 end user.
 * @param procfd   Pointer to a ProcFD instance.
 * @param safe_id  A string identifying the source of the request to execute the
 *                 operation. See @ref gh_thread.safe_id for rules on how these
 *                 strings should be chosen.
 * @param exe_fd   PathFD referencing the executable to execute.
 * @param argv     Vector of arguments, in `main()` format (terminated by a
 *                 `NULL` entry).
 * @param envp     Vector of environment variables, in `main()` format (key=value,
 *                 terminated by a `NULL` entry).
 *
 * @return @ref GHR_OK on success. @n
 *         @ref GHR_PERMS_REJECTEDPOLICY indicates that the request has been
 *         rejected by an existing entry in the policy. @n
 *         @ref GHR_PERMS_REJECTEDUSER indicates that the request required an
 *         interactive prompt, which the end user rejected. @n
 *         @ref GHR_PERMS_REJECTEDPROMPT indicates that the request required an
 *         interactive prompt, but the prompt refused the request for some
 *         reason. @n
 *         Other result codes with other error codes may be returned.
 */
gh_result gh_permexec_gate(gh_permexec * permexec, gh_permprompter * prompter, gh_procfd * procfd, const char * safe_id, gh_pathfd exe_fd, char * const * argv, char * const * envp);

gh_result gh_permexec_registerparser(gh_permexec * permexec, gh_permparser * parser);
gh_result gh_permexec_write(gh_permexec * permexec, gh_permwriter * writer);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
