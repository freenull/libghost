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

typedef struct {
    gh_permfs_mode mode_reject;
    gh_permfs_mode mode_accept;
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

typedef struct {
    gh_abscanonicalpath path;
    /* bool allow_symlinked; */
} gh_permfs_ident;

typedef struct {
    gh_permfs_ident ident;

    gh_permfs_modeset self;
    gh_permfs_modeset children;
} gh_permfs_entry;

#define GH_PERMFSFILELIST_INITIALCAPACITY 128
#define GH_PERMFSFILELIST_MAXCAPACITY GH_DYNAMICARRAY_NOMAXCAPACITY

typedef struct {
    gh_alloc * alloc;
    gh_permfs_entry * buffer;
    size_t size;
    size_t capacity;
} gh_permfs_entrylist;

gh_result gh_permfs_entrylist_ctor(gh_permfs_entrylist * list, gh_alloc * alloc);
gh_result gh_permfs_entrylist_append(gh_permfs_entrylist * list, gh_permfs_entry perm);
gh_result gh_permfs_entrylist_getat(gh_permfs_entrylist * list, size_t idx, gh_permfs_entry ** out_perm);
gh_result gh_permfs_entrylist_dtor(gh_permfs_entrylist * list);

typedef struct {
    gh_procfd procfd;
    gh_permfs_entrylist file_perms;
} gh_permfs;

typedef struct {
    gh_permfs_mode rejected_mode;
    bool default_policy;
} gh_permfs_actionresult;

gh_result gh_permfs_ctor(gh_permfs * permfs, gh_alloc * alloc);
gh_result gh_permfs_add(gh_permfs * permfs, gh_permfs_ident ident, gh_permfs_modeset self_modeset, gh_permfs_modeset children_modeset, gh_permfs_entry ** out_entry);
gh_result gh_permfs_getmode(gh_permfs * permfs, int opath_fd, gh_abscanonicalpath canonical_path, gh_permfs_modeset * out_self_modeset, gh_permfs_modeset * out_children_modeset);
gh_result gh_permfs_gatefile(gh_permfs * permfs, gh_permprompter * prompter, const char * safe_id, gh_pathfd fd, gh_permfs_mode mode, const char * hint);
gh_result gh_permfs_requestnode(gh_permfs * permfs, gh_permprompter * prompter, const char * safe_id, gh_pathfd fd, gh_permfs_mode self_mode, gh_permfs_mode children_mode, const char * hint, bool * out_wouldprompt);
gh_result gh_permfs_dtor(gh_permfs * permfs);
gh_result gh_permfs_fcntlflags2permfsmode(int fcntl_flags, mode_t create_accessmode, gh_pathfd pathfd, gh_permfs_mode * out_mode);
gh_result gh_permfs_registerparser(gh_permfs * permfs, gh_permparser * parser);
gh_result gh_permfs_write(gh_permfs * permfs, gh_permwriter * writer);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
