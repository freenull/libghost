/** @defgroup perms Permission store
 *
 * @brief Handles serialization and deserialization of permfs and custom permission stores.
 *
 * @{
 */

#ifndef GHOST_PERMS_H
#define GHOST_PERMS_H

#define _GNU_SOURCE
#include <stdbool.h>
#include <stddef.h>
#include <fcntl.h>
#include <limits.h>
#include <ghost/dynamic_array.h>
#include <ghost/byte_buffer.h>
#include <ghost/result.h>
#include <ghost/alloc.h>
#include <ghost/perms/procfd.h>
#include <ghost/perms/prompt.h>
#include <ghost/perms/permfs.h>
#include <ghost/perms/permexec.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GH_TYPEDEF_THREAD
typedef struct gh_thread gh_thread;
#define GH_TYPEDEF_THREAD
#endif

/** @brief Key of a generic permission system. */
typedef struct {
    /** @brief String containing the key. */
    gh_conststr key;
} gh_permgeneric_key;

/** @brief Callback type: generic permission system constructor.
 *
 * @param userdata Arbitrary userdata passed through @ref gh_perms_registergeneric.
 *
 * @return Pointer to newly constructed instance of generic permission system.
 *         If `NULL`, registering will fail with @ref GHR_PERMS_GENERICCTOR.
 */
typedef void * gh_permgeneric_ctor_func(void * userdata);

/** @brief Callback type: generic permission system destructor.
 *
 * @param instance Return value of a previous call to @ref gh_permgeneric_ctor_func.
 * @param userdata Arbitrary userdata passed through @ref gh_perms_registergeneric.
 *
 * @return Arbitrary result code. @ref GHR_OK indicate success.
 */
typedef gh_result gh_permgeneric_dtor_func(void * instance, void * userdata);

/** @brief Callback type: generic permission system group/resource match check.
 *
 * Called to check if the generic permission system supports a particular pair of
 * group and resource types.
 *
 * @param instance    Return value of a previous call to @ref gh_permgeneric_ctor_func.
 * @param group_id    String containing the group type.
 * @param resource_id String containing the resource type.
 * @param userdata Arbitrary userdata passed through @ref gh_perms_registergeneric.
 *
 * @return Should return @ref GHR_OK if the generic permission system supports this
 *         resource type or @ref GHR_PERMPARSER_NOMATCH if not. @n
 *         Other result codes may be returned and indicate an error that will be
 *         bubbled up.
 */
typedef gh_result gh_permgeneric_match_func(void * instance, gh_permrequest_id group_id, gh_permrequest_id resource_id, void * userdata);

/** @brief Callback type: new entry of generic permission system.
 *
 * Called during parsing when there is a need for the generic permission system
 * to create a new, default entry in its policy.
 *
 * @param instance       Return value of a previous call to @ref gh_permgeneric_ctor_func.
 * @param entry          Key of the new entry.
 * @param userdata       Arbitrary userdata passed through @ref gh_perms_registergeneric.
 * @param[out] out_entry May be used to write in a pointer to the new entry.
 *
 * @return Arbitrary result code. @ref GHR_OK indicate success.
 */
typedef gh_result gh_permgeneric_newentry_func(void * instance, const gh_permgeneric_key * entry, void * userdata, void ** out_entry);

/** @brief Callback type: set field on entry of generic permission system.
 *
 * Called during parsing for each key of a field of an entry. The generic
 * permission system is responsible for parsing the field's values and/or
 * indicating an error.
 *
 * @param instance       Return value of a previous call to @ref gh_permgeneric_ctor_func.
 * @param entry          Value of `*out_entry` as set by @ref gh_permgeneric_newentry_func.
 * @param field          Key of the field.
 * @param parser         Instance of the parser to use to read field values.
 * @param userdata       Arbitrary userdata passed through @ref gh_perms_registergeneric.
 *
 * @return Arbitrary result code. @ref GHR_OK indicate success.
 */
typedef gh_result gh_permgeneric_loadentry_func(void * instance, void * entry, gh_permrequest_id field, gh_permparser * parser, void * userdata);

/** @brief Callback type: save generic permission system policy.
 *
 * Called during writing. The generic permission system is responsible for
 * writing the entire group-resource block - i.e. the function should start
 * and end with @ref gh_permwriter_beginresource and @ref gh_permwriter_endresource.
 *
 * @param instance       Return value of a previous call to @ref gh_permgeneric_ctor_func.
 * @param writer         Instance of the GHPERM writer.
 * @param userdata       Arbitrary userdata passed through @ref gh_perms_registergeneric.
 *
 * @return Arbitrary result code. @ref GHR_OK indicate success.
 */
typedef gh_result gh_permgeneric_save_func(void * instance, gh_permwriter * writer, void * userdata);

/** @brief Generic permission system. */
typedef struct {
    /** @brief Arbitrary userdata intended to hold the system's state. May be `NULL`. */
    void * userdata;

    /** @brief Constructor. See @ref gh_permgeneric_ctor_func. */
    gh_permgeneric_ctor_func * ctor;

    /** @brief Destructor. See @ref gh_permgeneric_dtor_func. */
    gh_permgeneric_dtor_func * dtor;

    /** @brief Match. See @ref gh_permgeneric_match_func. */
    gh_permgeneric_match_func * match;

    /** @brief New entry. See @ref gh_permgeneric_newentry_func. */
    gh_permgeneric_newentry_func * newentry;

    /** @brief Load entry field. See @ref gh_permgeneric_loadentry_func. */
    gh_permgeneric_loadentry_func * loadentry;

    /** @brief Save. See @ref gh_permgeneric_save_func. */
    gh_permgeneric_save_func * save;
} gh_permgeneric;

/** @brief Maximum size (including null terminator) of a generic permission
 *        system ID.
 */
#define GH_PERMGENERIC_IDMAX 64

typedef struct {
    char id[GH_PERMGENERIC_IDMAX];
    const gh_permgeneric * vtable;
    void * instance;
} gh_permgeneric_instance;

/** @brief Maximum number of generic permission systems that may be registered
 *         at once.
 */
#define GH_PERMS_MAXGENERIC 16

/** @brief Centralized permission system. */
typedef struct {
    /** @ref Persistent reference to the `fd` directory of the `/proc` filesystem. */
    gh_procfd procfd;

    /** @ref Prompter to use for interactive prompts. */
    gh_permprompter prompter;

    /** @ref PermFS instance. */
    gh_permfs filesystem;

    /** @ref PermExec instance. */
    gh_permexec exec;

    /** @ref Number of registered generic permission systems. */
    size_t generic_count;

    /** @ref List of registered generic permission systems. */
    gh_permgeneric_instance generic[GH_PERMS_MAXGENERIC];
} gh_perms;

/** @brief Construct a new centralized permission system.
 *
 * @param perms    Pointer to unconstructed memory that will hold the new instance.
 * @param alloc    Allocator.
 * @param prompter Prompter to use for interactive prompts.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_perms_ctor(gh_perms * perms, gh_alloc * alloc, gh_permprompter prompter);

/** @brief Destroy a centralized permission system.
 *
 * @param perms Pointer to a constructed centralized permission system instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_perms_dtor(gh_perms * perms);

/** @brief Check whether an operation on a file is allowed by the security policy.
 *
 * See: @ref gh_permfs_gatefile. This function automatically passes the `prompter`
 * and `procfd`.
 */
gh_result gh_perms_gatefile(gh_perms * perms, const char * safe_id, gh_pathfd fd, gh_permfs_mode mode, const char * hint);

/** @brief Check whether an operation on a file is allowed by the
 *         security policy based on explicit mode sets.
 *
 * See: @ref gh_permfs_requestnode. This function automatically passes the `prompter`
 * and `procfd`.
 */
gh_result gh_perms_fsrequest(gh_perms * perms, const char * safe_id, gh_pathfd fd, gh_permfs_mode self_mode, gh_permfs_mode children_mode, const char * hint, bool * out_wouldprompt);

/** @brief Check whether an operation to spawn an external program is allowed
 *         by the security policy.
 *
 * See: @ref gh_permexec_gate. This function automatically passes the `prompter`
 * and `procfd`.
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_perms_gateexec(gh_perms * perms, const char * safe_id, gh_pathfd exe_fd, char * const * argv, char * const * envp);

/** @brief Deserialize permission system policy from a file descriptor.
 *
 * @param perms Pointer to a constructed centralized permission system instance.
 * @param fd    Forward-readable descriptor of file containing GHPERM data.
 * @param[out] out_parsererror If not `NULL`, will hold details about parser errors.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_perms_readfd(gh_perms * perms, int fd, gh_permparser_error * out_parsererror);

/** @brief Deserialize permission system policy from an in-memory buffer.
 *
 * @param perms      Pointer to a constructed centralized permission system instance.
 * @param buffer     Pointer to the character buffer containing GHPERM data.
 * @param buffer_len Length (without null terminator) of @p buffer.
 * @param[out] out_parsererror If not `NULL`, will hold details about parser errors.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_perms_readbuffer(gh_perms * perms, const char * buffer, size_t buffer_len, gh_permparser_error * out_parsererror);

/** @brief Write permission system policy to file descriptor.
 *
 * @warning In the case of an error, data in the file descriptor may be lost.
 *          You may want to implement a backup system, or be ready to throw away
 *          the security policy if reading it back later fails.
 *
 * @param perms      Pointer to a constructed centralized permission system instance.
 * @param fd         Writable file descriptor.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_perms_write(gh_perms * perms, int fd);

/** @brief Register generic permission system.
 *
 * @param perms      Pointer to a constructed centralized permission system instance.
 * @param id         ID of the generic permission system. See also: @ref GH_PERMGENERIC_IDMAX.
 * @param generic    Structure containing data about the permission system.
 *                   See: @ref gh_permgeneric.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_perms_registergeneric(gh_perms * perms, const char * id, const gh_permgeneric * generic);

/** @brief Retrieve an instance of a generic permission system.
 *
 * @param perms      Pointer to a constructed centralized permission system instance.
 * @param id         ID of the generic permission system.
 *
 * @return Pointer to an instance returned by @ref gh_permgeneric_ctor_func of
 *         the generic permission system, or `NULL` if the ID doesn't match any
 *         existing registered system.
 */
void * gh_perms_getgeneric(gh_perms * perms, const char * id);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
