#ifndef GHOST_PERMS_H
#define GHOST_PERMS_H

#define _GNU_SOURCE
#include <stdbool.h>
#include <stddef.h>
#include <fcntl.h>
#include <limits.h>
#include <ghost/thread.h>
#include <ghost/dynamic_array.h>
#include <ghost/byte_buffer.h>
#include <ghost/result.h>
#include <ghost/alloc.h>
#include <ghost/perms/prompt.h>
#include <ghost/generated/gh_permfs_mode.h>

typedef enum {
    GH_PERMACTION_REJECT,
    GH_PERMACTION_ACCEPT,
    GH_PERMACTION_PROMPT,
} gh_permaction;

#define GH_PERMFS_PATHNAMEMAX PATH_MAX

// no ./, no ../, always single slash separating each element, starts with /, no trailing /
typedef struct { char * ptr; size_t len; } gh_permfs_abscanonicalpath;

typedef struct {
    gh_permfs_mode mode_reject;
    gh_permfs_mode mode_accept;
    gh_permfs_mode mode_prompt;
} gh_permfs_modeset;

static inline gh_permfs_modeset gh_permfs_modeset_join(gh_permfs_modeset lhs, gh_permfs_modeset rhs) {
    gh_permfs_modeset mode = lhs;
    mode.mode_reject |= rhs.mode_reject;
    mode.mode_accept |= rhs.mode_accept;
    mode.mode_prompt |= rhs.mode_prompt;
    return mode;
}

typedef struct {
    gh_permfs_abscanonicalpath path;
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
    int fd;
} gh_pathfd;

typedef struct {
    int fd;
    gh_alloc * alloc;
} gh_procfd;

gh_result gh_procfd_ctor(gh_procfd * procfd, gh_alloc * alloc);
#define GH_PROCFD_GETFDPATH_INITIALBUFFERSIZE 128
#define GH_PROCFD_GETFDPATH_BUFFERSIZEINCREMENT 128
#define GH_PROCFD_FDFILENAMESIZE sizeof("XXXX")
gh_result gh_procfd_getfdfilename(gh_procfd * procfd, gh_pathfd fd, char * out_buffer);
gh_result gh_procfd_fdpathctor(gh_procfd * procfd, gh_pathfd fd, gh_permfs_abscanonicalpath * out_path);
gh_result gh_procfd_fdpathdtor(gh_procfd * procfd, gh_permfs_abscanonicalpath * path);
gh_result gh_procfd_reopen(gh_procfd * procfd, gh_pathfd fd, int flags, mode_t create_mode, int * out_fd);
gh_result gh_procfd_dtor(gh_procfd * procfd);

typedef struct {
    gh_procfd procfd;
    gh_permaction default_action;
    gh_permfs_entrylist file_perms;
} gh_permfs;

typedef struct {
    gh_permaction action;
    gh_permfs_mode rejected_mode;
    bool default_policy;
} gh_permactionresult;

typedef struct {
    gh_bytebuffer buffer;
    gh_permfs_abscanonicalpath path;
    gh_permrequest request;
} gh_permfs_reqdata;

gh_result gh_permfs_ctor(gh_permfs * permfs, gh_alloc * alloc, gh_permaction default_action);
gh_result gh_permfs_add(gh_permfs * permfs, gh_permfs_ident ident, gh_permfs_modeset self_modeset, gh_permfs_modeset children_modeset);
gh_result gh_permfs_getmode(gh_permfs * permfs, int opath_fd, gh_permfs_abscanonicalpath canonical_path, gh_permfs_modeset * out_modeset);
gh_result gh_permfs_act(gh_permfs * permfs, gh_permfs_modeset modeset, gh_permfs_mode mode, gh_permactionresult * out_result);
gh_result gh_permfs_actfd(gh_permfs * permfs, gh_permprompter * prompter, gh_pathfd fd, gh_permfs_mode mode, gh_permactionresult * out_actionresult);
gh_result gh_pathfdopen(gh_permfs * permfs, int dirfd, const char * path, gh_pathfd * out_pathfd);
gh_result gh_pathfdclose(gh_permfs * permfs, gh_pathfd pathfd);
gh_result gh_permfs_actpath(gh_permfs * permfs, int dirfd, const char * path, gh_permfs_mode mode, gh_permactionresult * out_actionresult, int * out_safefd);
gh_result gh_permfs_reqctor(gh_permfs * permfs, const char * source, gh_permfs_abscanonicalpath path, gh_permactionresult * result, gh_permfs_reqdata * out_reqdata);
gh_result gh_permfs_reqdtor(gh_permfs * permfs, gh_permfs_reqdata * reqdata);
gh_result gh_permfs_dtor(gh_permfs * permfs);
gh_result gh_permfs_fcntlflags2permfsmode(int fcntl_flags, gh_permfs_mode * out_mode);

typedef struct {
    gh_permfs filesystem;
    gh_permprompter * prompter;
} gh_perms;

gh_result gh_perms_ctor(gh_perms * perms, gh_alloc * alloc, gh_permprompter * prompter);
gh_result gh_perms_actfilesystem(gh_perms * perms, gh_thread * thread, gh_pathfd fd, gh_permfs_mode mode);
gh_result gh_perms_openat(gh_perms * perms, gh_thread * thread, int dirfd, const char * path, int flags, mode_t create_mode, int * out_fd);
gh_result gh_perms_dtor(gh_perms * perms);

gh_result gh_perms_parseline(gh_perms * perms, const char * line, size_t line_len);

#endif
