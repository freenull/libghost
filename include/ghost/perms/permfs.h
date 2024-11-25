#ifndef GHOST_PERMS_PERMFS_H
#define GHOST_PERMS_PERMFS_H

#include <ghost/generated/gh_permfs_mode.h>
#include <ghost/byte_buffer.h>
#include <ghost/perms/procfd.h>
#include <ghost/perms/actreq.h>
#include <ghost/perms/prompt.h>
#include <ghost/perms/parser.h>
#include <ghost/perms/writer.h>

bool gh_permfs_nextmodeflag(gh_permfs_mode * state, gh_permfs_mode * out_flag);

__attribute__((always_inline))
static inline bool gh_permfs_mode_isaccessmodeflag(gh_permfs_mode flag) {
    return flag >= GH_PERMFS_ACCESS_USER_READ && flag <= GH_PERMFS_ACCESS_OTHER_EXECUTE;
}

typedef struct {
    gh_bytebuffer buffer;
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
    mode.mode_reject |= rhs.mode_reject;
    mode.mode_accept |= rhs.mode_accept;
    mode.mode_prompt |= rhs.mode_prompt;
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
    gh_permaction default_action;
    gh_permfs_entrylist file_perms;
} gh_permfs;

typedef struct {
    gh_permaction action;
    gh_permfs_mode rejected_mode;
    bool default_policy;
} gh_permfs_actionresult;

gh_result gh_permfs_ctor(gh_permfs * permfs, gh_alloc * alloc, gh_permaction default_action);
gh_result gh_permfs_add(gh_permfs * permfs, gh_permfs_ident ident, gh_permfs_modeset self_modeset, gh_permfs_modeset children_modeset, gh_permfs_entry ** out_entry);
gh_result gh_permfs_getmode(gh_permfs * permfs, int opath_fd, gh_abscanonicalpath canonical_path, gh_permfs_modeset * out_modeset);
gh_result gh_permfs_act(gh_permfs * permfs, gh_permfs_modeset modeset, gh_permfs_mode mode, gh_permfs_actionresult * out_result);
gh_result gh_permfs_actfd(gh_permfs * permfs, gh_permprompter * prompter, gh_pathfd fd, gh_permfs_mode mode, gh_permfs_actionresult * out_actionresult);
gh_result gh_permfs_actpath(gh_permfs * permfs, int dirfd, const char * path, gh_permfs_mode mode, gh_permfs_actionresult * out_actionresult, int * out_safefd);
gh_result gh_permfs_reqctor(gh_permfs * permfs, const char * source, gh_abscanonicalpath path, const char * hint, gh_permfs_actionresult * result, gh_permfs_reqdata * out_reqdata);
gh_result gh_permfs_reqdtor(gh_permfs * permfs, gh_permfs_reqdata * reqdata);
gh_result gh_permfs_dtor(gh_permfs * permfs);
gh_result gh_permfs_fcntlflags2permfsmode(int fcntl_flags, mode_t create_accessmode, gh_pathfd pathfd, gh_permfs_mode * out_mode);
gh_result gh_permfs_registerparser(gh_permfs * permfs, gh_permparser * parser);
gh_result gh_permfs_write(gh_permfs * permfs, gh_permwriter * writer);

#endif
