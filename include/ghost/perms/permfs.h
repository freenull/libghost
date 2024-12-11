/** @defgroup permfs permfs
 *
 * @brief Filesystem permission handler.
 *
 * @{
 */

#ifndef GHOST_PERMS_PERMFS_H
#define GHOST_PERMS_PERMFS_H

#include <ghost/generated/gh_permfs_mode.h>
#include <ghost/byte_buffer.h>
#include <ghost/perms/procfd.h>
#include <ghost/perms/request.h>
#include <ghost/perms/prompt.h>
#include <ghost/perms/parser.h>
#include <ghost/perms/writer.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GH_TYPEDEF_THREAD
typedef struct gh_thread gh_thread;
#define GH_TYPEDEF_THREAD
#endif

bool gh_permfs_nextmodeflag(gh_permfs_mode * state, gh_permfs_mode * out_flag);
bool gh_permfs_ismodevalid(gh_permfs_mode mode);
gh_result gh_permfs_mode_fromstr(gh_conststr str, gh_permfs_mode * out_mode);

__attribute__((always_inline))
static inline bool gh_permfs_mode_isaccessmodeflag(gh_permfs_mode flag) {
    return flag >= GH_PERMFS_ACCESS_USER_READ && flag <= GH_PERMFS_ACCESS_OTHER_EXECUTE;
}

#define PERMFS_DESCRIPTIONMAX 2048
typedef struct {
    char description_buffer[PERMFS_DESCRIPTIONMAX];
    gh_abscanonicalpath path;
    gh_permrequest request;
} gh_permfs_reqdata;

/** @brief Set of three @ref gh_permfs_mode for each policy type case. */
typedef struct {
    /** @brief Mode for which requests must be rejected. */
    gh_permfs_mode mode_reject;

    /** @brief Mode for which requests shall be accepted. */
    gh_permfs_mode mode_accept;

    /** @brief Mode for which an interactive prompter should decide the result. */
    gh_permfs_mode mode_prompt;
} gh_permfs_modeset;

#define GH_PERMFS_MODESET_EMPTY ((gh_permfs_modeset){ \
        .mode_reject = 0, \
        .mode_accept = 0, \
        .mode_prompt = 0 \
    })

static inline gh_permfs_modeset gh_permfs_modeset_join(gh_permfs_modeset lhs, gh_permfs_modeset rhs) {
    gh_permfs_modeset mode = lhs;
    mode.mode_reject = (gh_permfs_mode)(mode.mode_reject | rhs.mode_reject);
    mode.mode_accept = (gh_permfs_mode)(mode.mode_accept | rhs.mode_accept);
    mode.mode_prompt = (gh_permfs_mode)(mode.mode_prompt | rhs.mode_prompt);
    return mode;
}

/** @brief Filesystem identifier - absolute canonical path.
 *
 * Do note that the "canonical" part is a misnomer, since once the path has
 * been added to the policy, the state of the filesystem may have changed in
 * a way that has made the path contain symlinks.
 *
 * However, because these paths are compared against paths that do not contain
 * symlinks, these entries are never matched and effectively don't exist.
 */
typedef struct {
    gh_abscanonicalpath path;
} gh_permfs_ident;

/** @brief PermFS policy entry. */
typedef struct {
    /** @brief Filesystem identifier. */
    gh_permfs_ident ident;

    /** @brief Mode set applied to the node at the path of this entry. */
    gh_permfs_modeset self;

    /** @brief Mode set applied to all nodes below the path of this entry in the
     *         hierarchy. */
    gh_permfs_modeset children;
} gh_permfs_entry;

#define GH_PERMFSFILELIST_INITIALCAPACITY 128

#define GH_PERMFSFILELIST_MAXCAPACITY GH_DYNAMICARRAY_NOMAXCAPACITY

/** @brief Dynamically resizable PermFS security policy. */
typedef struct {
    /** @brief Allocator. */
    gh_alloc * alloc;

    /** @brief Buffer of entries. */
    gh_permfs_entry * buffer;

    /** @brief Size of dynamic array. */
    size_t size;

    /** @brief Capacity of dynamic array. */
    size_t capacity;
} gh_permfs_entrylist;

gh_result gh_permfs_entrylist_ctor(gh_permfs_entrylist * list, gh_alloc * alloc);
gh_result gh_permfs_entrylist_append(gh_permfs_entrylist * list, gh_permfs_entry perm);
gh_result gh_permfs_entrylist_getat(gh_permfs_entrylist * list, size_t idx, gh_permfs_entry ** out_perm);
gh_result gh_permfs_entrylist_dtor(gh_permfs_entrylist * list);

/** @brief PermFS permission system. */
typedef struct {
    gh_permfs_entrylist file_perms;
} gh_permfs;

/** @brief Result of a PermFS action. */
typedef struct {
    /** @brief The set of operations that has been rejected. */
    gh_permfs_mode rejected_mode;

    /** @brief Whether the action is a result of the default policy (a matching
     *         entry did not exist).
     */
    bool default_policy;
} gh_permfs_actionresult;

/** @brief Construct a new PermFS permission system.
 *
 * @param permfs   Pointer to unconstructed memory that will hold the new instance.
 * @param alloc    Allocator for the dynamic array of policy entries.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permfs_ctor(gh_permfs * permfs, gh_alloc * alloc);

/** @brief Destroy a PermFS permission system.
 *
 * @param permfs   Pointer to a constructed PermExec instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permfs_dtor(gh_permfs * permfs);

/** @brief Add an entry to the PermFS policy.
 *
 * @param permfs            Pointer to a constructed PermFS instance.
 * @param ident             Filesystem identifier (path).
 * @param self_modeset      Set of modes to apply to the path itself.
 * @param children_modeset  Set of modes to apply to nodes below the path in the
 *                          hierarchy.
 * @param[out] out_entry Will hold a pointer to the new entry.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permfs_add(gh_permfs * permfs, gh_permfs_ident ident, gh_permfs_modeset self_modeset, gh_permfs_modeset children_modeset, gh_permfs_entry ** out_entry);

/** @brief Check whether an operation on a file is allowed by the PermFS
 *         security policy.
 *
 * @param permfs     Pointer to a constructed PermFS instance.
 * @param prompter   Prompter to use if the policy indicates a need to prompt the
 *                   end user.
 * @param procfd     Pointer to a ProcFD instance.
 * @param safe_id    A string identifying the source of the request to execute the
 *                   operation. See @ref gh_thread.safe_id for rules on how these
 *                   strings should be chosen.
 * @param fd         PathFD representing the filesystem path of interest.
 * @param mode       Mode representing the desired operation.
 * @param hint       String provided to the prompter as the value of the `hint`
 *                   field. May be `NULL`.
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
gh_result gh_permfs_gatefile(gh_permfs * permfs, gh_permprompter * prompter, gh_procfd * procfd, const char * safe_id, gh_pathfd fd, gh_permfs_mode mode, const char * hint);

/** @brief Check whether an operation on a file is allowed by the PermFS
 *         security policy based on explicit mode sets.
 *
 * @param permfs     Pointer to a constructed PermFS instance.
 * @param prompter   Prompter to use if the policy indicates a need to prompt the
 *                   end user.
 * @param procfd     Pointer to a ProcFD instance.
 * @param safe_id    A string identifying the source of the request to execute the
 *                   operation. See @ref gh_thread.safe_id for rules on how these
 *                   strings should be chosen.
 * @param fd         PathFD representing the filesystem path of interest.
 * @param self_mode  Mode representing the desired operation on the path itself.
 * @param children_mode Mode representing the desired operation on children of the path.
 * @param hint       String provided to the prompter as the value of the `hint`
 *                   field. May be `NULL`.
 *
 * @param[out] out_wouldprompt Will contain whether the request would have triggered
 *                             an interactive prompt. If this parameter is set as
 *                             anything other than `NULL`, a situation that would
 *                             normally trigger an interactive prompt instead
 *                             sets the value behind this pointer to `true` and
 *                             causes the function to return @ref GHR_OK.
 *
 * @return @ref GHR_OK on success (or if the request requires an interactive
 *         prompt and @p out_wouldprompt is not `NULL`). @n
 *         @ref GHR_PERMS_REJECTEDPOLICY indicates that the request has been
 *         rejected by an existing entry in the policy. @n
 *         @ref GHR_PERMS_REJECTEDUSER indicates that the request required an
 *         interactive prompt, which the end user rejected. @n
 *         @ref GHR_PERMS_REJECTEDPROMPT indicates that the request required an
 *         interactive prompt, but the prompt refused the request for some
 *         reason. @n
 *         Other result codes with other error codes may be returned.
 */
gh_result gh_permfs_requestnode(gh_permfs * permfs, gh_permprompter * prompter, gh_procfd * procfd, const char * safe_id, gh_pathfd fd, gh_permfs_mode self_mode, gh_permfs_mode children_mode, const char * hint, bool * out_wouldprompt);

gh_result gh_permfs_fcntlflags2permfsmode(int fcntl_flags, mode_t create_accessmode, gh_pathfd pathfd, gh_permfs_mode * out_mode);
gh_result gh_permfs_getmode(gh_permfs * permfs, int opath_fd, gh_abscanonicalpath canonical_path, gh_permfs_modeset * out_self_modeset, gh_permfs_modeset * out_children_modeset);
gh_result gh_permfs_registerparser(gh_permfs * permfs, gh_permparser * parser);
gh_result gh_permfs_write(gh_permfs * permfs, gh_permwriter * writer);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
