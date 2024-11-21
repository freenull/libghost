#define _GNU_SOURCE
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ghost/thread.h>
#include <ghost/result.h>
#include <ghost/dynamic_array.h>
#include <ghost/perms/prompt.h>
#include <ghost/perms/perms.h>

static gh_result permfsfilelist_dtorelement(gh_dynamicarray da, void * elem_voidp, void * userdata) {
    (void)userdata;
    gh_permfs_entry * entry = (gh_permfs_entry *)elem_voidp;
    return gh_alloc_delete(da.alloc, (void**)&entry->ident.path.ptr, (entry->ident.path.len + 1) * sizeof(char));
}

static const gh_dynamicarrayoptions permfsfilelist_daopts = {
    .initial_capacity = GH_PERMFSFILELIST_INITIALCAPACITY,
    .max_capacity = GH_PERMFSFILELIST_MAXCAPACITY,
    .element_size = sizeof(gh_permfs_entry),
   
    .dtorelement_func = permfsfilelist_dtorelement,
    .userdata = NULL
};

gh_result gh_permfs_entrylist_ctor(gh_permfs_entrylist * list, gh_alloc * alloc) {
    list->alloc = alloc;
    return gh_dynamicarray_ctor(GH_DYNAMICARRAY(list), &permfsfilelist_daopts);
}

gh_result gh_permfs_entrylist_append(gh_permfs_entrylist * list, gh_permfs_entry perm) {
    return gh_dynamicarray_append(GH_DYNAMICARRAY(list), &permfsfilelist_daopts, &perm);
}

gh_result gh_permfs_entrylist_getat(gh_permfs_entrylist * list, size_t idx, gh_permfs_entry ** out_perm) {
    gh_result res = gh_dynamicarray_getat(GH_DYNAMICARRAY(list), &permfsfilelist_daopts, idx, (void**)out_perm);
    if (ghr_iserr(res)) return res;
    return GHR_OK;
}

gh_result gh_permfs_entrylist_dtor(gh_permfs_entrylist * list) {
    return gh_dynamicarray_dtor(GH_DYNAMICARRAY(list), &permfsfilelist_daopts);
}

#define GH_PROCFD_GETFDPATH_FDFILENAMEMAXFD 9999
gh_result gh_procfd_ctor(gh_procfd * procfd, gh_alloc * alloc) {
    int fd = open("/proc/self/fd", O_PATH | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return ghr_errno(GHR_PROCFD_PROCPATHFDOPEN);
    *procfd = (gh_procfd){ .fd = fd, .alloc = alloc };

    return GHR_OK;

}

gh_result gh_procfd_getfdfilename(gh_procfd * procfd, gh_pathfd fd, char * out_buffer) {
    (void)procfd;

    if (fd.fd < 0) return ghr_errnoval(GHR_PROCFD_INVALIDFD, EBADFD);
    if ((unsigned int)fd.fd > GH_PROCFD_GETFDPATH_FDFILENAMEMAXFD) return GHR_PROCFD_BIGFDPATH;

    int snprintf_result = snprintf(
        out_buffer, GH_PROCFD_FDFILENAMESIZE,
        "%d", fd.fd
    );

    if (snprintf_result < 0) return ghr_errno(GHR_PROCFD_FDPATHCONSTRUCT);
    return GHR_OK;
}

gh_result gh_procfd_fdpathctor(gh_procfd * procfd, gh_pathfd fd, gh_permfs_abscanonicalpath * out_path) {
    gh_result inner_res = GHR_OK;

    if (fd.fd < 0) return ghr_errnoval(GHR_PROCFD_INVALIDFD, EBADFD);
    if ((unsigned int)fd.fd > GH_PROCFD_GETFDPATH_FDFILENAMEMAXFD) return GHR_PROCFD_BIGFDPATH;

    char fd_filename[GH_PROCFD_FDFILENAMESIZE];
    gh_result res = gh_procfd_getfdfilename(procfd, fd, fd_filename);
    if (ghr_iserr(res)) return res;

    char * buffer = NULL;
    size_t buffer_size = GH_PROCFD_GETFDPATH_INITIALBUFFERSIZE;
    res = gh_alloc_new(procfd->alloc, (void**)&buffer, buffer_size);
    if (ghr_iserr(res)) return res;
    memset(buffer, 0, buffer_size);

    ssize_t readlinkat_res = 0;
    size_t len = 0;
    do {
        readlinkat_res = readlinkat(procfd->fd, fd_filename, buffer, buffer_size);
        if (readlinkat_res < 0) {
            res = ghr_errno(GHR_PROCFD_READLINK);
            goto fail_readlinkat;
        }

        if ((size_t)readlinkat_res < buffer_size) {
            buffer[readlinkat_res + 1] = '\0';
            len = (size_t)readlinkat_res;
            break;
        }

        size_t new_buffer_size = buffer_size + GH_PROCFD_GETFDPATH_BUFFERSIZEINCREMENT;
        if (new_buffer_size > SSIZE_MAX) {
            res = GHR_PROCFD_LARGELINK;
            goto fail_readlinkat;
        }

        res = gh_alloc_resize(procfd->alloc, (void**)&buffer, buffer_size, new_buffer_size);
        if (ghr_iserr(res)) goto fail_resize;

        buffer_size = new_buffer_size;
        memset(buffer, 0, buffer_size);
    } while (false);

    goto success;

fail_resize:
fail_readlinkat:
    inner_res = gh_alloc_delete(procfd->alloc, (void**)&buffer, buffer_size);
    if (ghr_iserr(inner_res)) res = inner_res;
    
success:
    if (out_path != NULL) {
        out_path->ptr = buffer;
        out_path->len = len;
    }

    return res;
}

gh_result gh_procfd_fdpathdtor(gh_procfd * procfd, gh_permfs_abscanonicalpath * path) {
    return gh_alloc_delete(procfd->alloc, (void**)&path->ptr, path->len + 1);
}

gh_result gh_procfd_reopen(gh_procfd * procfd, gh_pathfd fd, int flags, mode_t create_mode, int * out_fd) {
    gh_result res = GHR_OK;

    char fdfilename[GH_PROCFD_FDFILENAMESIZE];
    res = gh_procfd_getfdfilename(procfd, fd, fdfilename);
    if (ghr_iserr(res)) return res;

    int new_fd = openat(procfd->fd, fdfilename, flags, create_mode);
    if (new_fd < 0) return ghr_errno(GHR_PROCFD_REOPENFAIL);

    *out_fd = new_fd;
    return GHR_OK;
}

gh_result gh_procfd_dtor(gh_procfd * procfd) {
    if (close(procfd->fd) < 0) return ghr_errno(GHR_PROCFD_PROCPATHFDCLOSE);
    return GHR_OK;

}

gh_result gh_permfs_ctor(gh_permfs * permfs, gh_alloc * alloc, gh_permaction default_action) {
    permfs->default_action = default_action;
    gh_result res = gh_permfs_entrylist_ctor(&permfs->file_perms, alloc);
    if (ghr_iserr(res)) return res;

    res = gh_procfd_ctor(&permfs->procfd, alloc);
    return res;
}

static gh_result permfs_appendentry(gh_permfs * permfs, gh_permfs_ident ident, gh_permfs_modeset self_modeset, gh_permfs_modeset children_modeset) {
    gh_permfs_entry file_perm = {0};
    file_perm.self = self_modeset;

    file_perm.children = children_modeset;

    file_perm.ident.path.len = 0;
    file_perm.ident.path.ptr = NULL;

    gh_result res = gh_alloc_new(permfs->file_perms.alloc, (void**)&file_perm.ident.path.ptr, (ident.path.len + 1) * sizeof(char));
    if (ghr_iserr(res)) return res;
    strncpy(file_perm.ident.path.ptr, ident.path.ptr, ident.path.len + 1);
    file_perm.ident.path.len = ident.path.len;

    res = gh_permfs_entrylist_append(&permfs->file_perms, file_perm);
    if (ghr_iserr(res)) return res;

    return GHR_OK;
}

static bool permfs_identeq(gh_permfs_ident * lhs, gh_permfs_ident * rhs) {
    if (lhs->path.len != rhs->path.len) return false;
    return strncmp(lhs->path.ptr, rhs->path.ptr, lhs->path.len) == 0;
}

static void merge_modeset(gh_permfs_modeset * dest, gh_permfs_modeset src) {
    dest->mode_reject |= src.mode_reject;
    dest->mode_accept |= src.mode_accept;
    dest->mode_prompt |= src.mode_prompt;
}

gh_result gh_permfs_add(gh_permfs * permfs, gh_permfs_ident ident, gh_permfs_modeset self_modeset, gh_permfs_modeset children_modeset) {
    gh_result res = GHR_OK;
    for (size_t i = 0; i < permfs->file_perms.size; i++) {
        gh_permfs_entry * entry = NULL;
        res = gh_permfs_entrylist_getat(&permfs->file_perms, i, &entry);
        if (ghr_iserr(res)) return res;

        if (permfs_identeq(&entry->ident, &ident)) {
            merge_modeset(&entry->self, self_modeset);
            merge_modeset(&entry->self, children_modeset);

            return GHR_OK;
        }
    }

    return permfs_appendentry(permfs, ident, self_modeset, children_modeset);
}

inline static bool file_type_allowed(mode_t mode) {
    return S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode);
}

gh_result gh_permfs_getmode(gh_permfs * permfs, int opath_fd, gh_permfs_abscanonicalpath canonical_path, gh_permfs_modeset * out_modeset) {
    struct stat stat;
    int fstat_result = fstat(opath_fd, &stat);
    bool exists = false;

    if (fstat_result < 0 && errno == ENOENT) exists = false;
    else if (fstat_result < 0) return ghr_errno(GHR_PERMFS_STATFAIL);

    exists = true;

    if (exists && !file_type_allowed(stat.st_mode)) return GHR_PERMFS_BADTYPE;

    /* gh_permfs_abscanonicalpath path; */
    /* gh_result res = gh_procfd_fdpathctor(&permfs->procfd, fd, &path); */
    /* if (ghr_iserr(res)) return res; */

    gh_result res = GHR_OK;
    gh_permfs_modeset mode = {0};
    for (size_t i = 0; i < permfs->file_perms.size; i++) {
        gh_permfs_entry * perm = NULL;
        res = gh_permfs_entrylist_getat(&permfs->file_perms, i, &perm);
        if (ghr_iserr(res)) return res;

        // exact match - for when the exact file path is in the permfsentrylist
        if (strncmp(perm->ident.path.ptr, canonical_path.ptr, canonical_path.len + 1) == 0) {
            mode = gh_permfs_modeset_join(mode, perm->self);
            continue;
        }

        // directory match - for when the searched path starts with the entry's path + /
        //
        // this can't be symlink raced, because the input (path) is an absolute canonical path
        // that will NOT have symlinks
        if (strncmp(perm->ident.path.ptr, canonical_path.ptr, canonical_path.len) != 0) continue;
        if (canonical_path.ptr[canonical_path.len] != '/') continue;

        mode = gh_permfs_modeset_join(mode, perm->children);
    }

    if (out_modeset != NULL) *out_modeset = mode;
    return GHR_OK;
}

gh_result gh_permfs_act(gh_permfs * permfs, gh_permfs_modeset modeset, gh_permfs_mode mode, gh_permactionresult * out_result) {
    (void)permfs;

    if ((modeset.mode_reject & mode) != 0) {
        if (out_result != NULL) {
            out_result->action = GH_PERMACTION_REJECT;
            out_result->rejected_mode = mode & (~modeset.mode_reject);
            out_result->default_policy = false;
        }

        return GHR_PERMS_REJECTEDPOLICY;
    }

    if ((modeset.mode_prompt & mode) != 0) {
        if (out_result != NULL) {
            out_result->action = GH_PERMACTION_PROMPT;
            out_result->rejected_mode = mode & (~modeset.mode_prompt);
            out_result->default_policy = false;
        }
        return GHR_PERMS_REJECTEDPROMPT;
    }

    if ((modeset.mode_accept & mode) == mode) {
        if (out_result != NULL) {
            out_result->action = GH_PERMACTION_ACCEPT;
            out_result->rejected_mode = 0;
            out_result->default_policy = false;
        }
        return GHR_OK;
    }

    gh_permaction action = permfs->default_action;
    if (action != GH_PERMACTION_ACCEPT
    && action != GH_PERMACTION_REJECT
    && action != GH_PERMACTION_PROMPT) {
        action = GH_PERMACTION_REJECT;
    }

    if (out_result != NULL) {
        out_result->action = action;
        out_result->rejected_mode = mode;
        out_result->default_policy = true;
    }

    switch(action) {
    case GH_PERMACTION_PROMPT: return GHR_PERMS_REJECTEDPROMPT;
    case GH_PERMACTION_REJECT: return GHR_PERMS_REJECTEDPOLICY;
    case GH_PERMACTION_ACCEPT: return GHR_OK;
    default: return GHR_PERMS_REJECTEDPOLICY;
    }
}

gh_result gh_pathfdopen(gh_permfs * permfs, int dirfd, const char * path, gh_pathfd * out_pathfd) {
    (void)permfs;
    int opath_fd = openat(dirfd, path, O_PATH, 0);
    if (opath_fd < 0) return ghr_errno(GHR_PERMFS_PATHFDOPENFAIL);

    out_pathfd->fd = opath_fd;
    return GHR_OK;
}

gh_result gh_pathfdclose(gh_permfs * permfs, gh_pathfd pathfd) {
    (void)permfs;
    if (close(pathfd.fd) < 0) return ghr_errno(GHR_PERMFS_PATHFDCLOSEFAIL);
    return GHR_OK;
}

static const gh_permrequest permfs_reqtemplate = {
    .group = "filesystem",
    .resource = "node",
    .fields = {
        { .key = "path" },
        { .key = "mode" }
    },
};

static gh_result permfs_mode_fill(gh_bytebuffer * buffer, gh_permfs_mode mode) {
    gh_result res;
    size_t offs = 1;

    if (mode & GH_PERMFS_CREATEFILE) {
        res = gh_bytebuffer_append(buffer, (char*)",createfile" + offs, sizeof("createfile") - offs);
        if (ghr_iserr(res)) return res;
        offs = 0;
    }
    if (mode & GH_PERMFS_CREATEDIR) {
        res = gh_bytebuffer_append(buffer, (char*)",createdir" + offs, sizeof("createdir") - offs);
        if (ghr_iserr(res)) return res;
        offs = 0;
    }
    if (mode & GH_PERMFS_READ) {
        res = gh_bytebuffer_append(buffer, (char*)",read" + offs, sizeof("read") - offs);
        if (ghr_iserr(res)) return res;
        offs = 0;
    }
    if (mode & GH_PERMFS_WRITE) {
        res = gh_bytebuffer_append(buffer, (char*)",write" + offs, sizeof("write") - offs);
        if (ghr_iserr(res)) return res;
        offs = 0;
    }
    if (mode & GH_PERMFS_UNLINK) {
        res = gh_bytebuffer_append(buffer, (char*)",unlink" + offs, sizeof("unlink") - offs);
        if (ghr_iserr(res)) return res;
        offs = 0;
    }

    return GHR_OK;
}

gh_result gh_permfs_reqctor(gh_permfs * permfs, const char * source, gh_permfs_abscanonicalpath path, gh_permactionresult * result, gh_permfs_reqdata * out_reqdata) {
    gh_result inner_res = GHR_OK;

    gh_bytebuffer buf;
    gh_result res = gh_bytebuffer_ctor(&buf, permfs->file_perms.alloc);
    if (ghr_iserr(res)) return res;

    res = permfs_mode_fill(&buf, result->rejected_mode);
    if (ghr_iserr(res)) goto err_fill;

    res = gh_bytebuffer_append(&buf, "\0", 1);
    if (ghr_iserr(res)) goto err_append;
    
    out_reqdata->request = permfs_reqtemplate;
    out_reqdata->request.fields[0].value = path.ptr;
    out_reqdata->request.fields[0].value_len = path.len;

    strcpy(out_reqdata->request.source, source);
    out_reqdata->request.fields[1].value = buf.buffer;
    out_reqdata->request.fields[1].value_len = buf.size - 1;


    out_reqdata->path = path;
    out_reqdata->buffer = buf;
    goto success;

err_fill:
err_append:
    inner_res = gh_bytebuffer_dtor(&buf);
    if (ghr_iserr(inner_res)) res = inner_res;

    return res;

success:
    return GHR_OK;
}

gh_result gh_permfs_reqdtor(gh_permfs * permfs, gh_permfs_reqdata * reqdata) {
    (void)permfs;

    gh_result res = gh_bytebuffer_dtor(&reqdata->buffer);
    if (ghr_iserr(res)) return res;

    return GHR_OK;
}

gh_result gh_permfs_dtor(gh_permfs * permfs) {
    gh_result res = gh_permfs_entrylist_dtor(&permfs->file_perms);
    if (ghr_iserr(res)) return res;

    res = gh_procfd_dtor(&permfs->procfd);
    return res;
}

gh_result gh_permfs_fcntlflags2permfsmode(int fcntl_flags, gh_permfs_mode * out_mode) {
#define GH_PERMS_FCNTLMODE2PERMMODE_EXCHANGEFLAG(fcntl_flag, permfs_flag) if (fcntl_flags & (fcntl_flag)) { \
        permfs_mode = (permfs_mode | (permfs_flag)); \
        fcntl_flags = (int)((unsigned int)fcntl_flags & (~((unsigned int)(fcntl_flag)))); \
    }

    gh_permfs_mode permfs_mode = {0};

    // these 3 are special - not only is rdonly defined as 0, but the three are mutually exclusive
    // these are ignored if O_PATH is passed, but O_PATH is not supported (yet?)
    if ((fcntl_flags & O_ACCMODE) == O_RDONLY) {
        permfs_mode |= GH_PERMFS_READ;
        fcntl_flags = (int)((unsigned int)fcntl_flags & (~((unsigned int)(O_RDONLY))));
        // even though O_RDONLY is 0, this line is harmless to keep and makes it clear that we
        // didn't forget about it, so it is kept here
    } else if ((fcntl_flags & O_ACCMODE) == O_WRONLY) {
        permfs_mode |= GH_PERMFS_WRITE;
        fcntl_flags = (int)((unsigned int)fcntl_flags & (~((unsigned int)(O_WRONLY))));
    } else if ((fcntl_flags & O_ACCMODE) == O_RDWR) {
        permfs_mode = permfs_mode | GH_PERMFS_READ | GH_PERMFS_WRITE;
        fcntl_flags = (int)((unsigned int)fcntl_flags & (~((unsigned int)(O_RDWR))));
    } else {
        return GHR_PERMFS_BADFCNTLMODE;
    }
    
    GH_PERMS_FCNTLMODE2PERMMODE_EXCHANGEFLAG(O_APPEND, GH_PERMFS_WRITE);
    GH_PERMS_FCNTLMODE2PERMMODE_EXCHANGEFLAG(O_CREAT, GH_PERMFS_CREATEFILE);
    GH_PERMS_FCNTLMODE2PERMMODE_EXCHANGEFLAG(O_DIRECTORY, 0);

    if (fcntl_flags != 0) {
        return GHR_PERMFS_BADFCNTLMODE;
    }

    if (out_mode != NULL) *out_mode = permfs_mode;
    return GHR_OK;

#undef GH_PERMS_FCNTLMODE2PERMMODE
}

gh_result gh_perms_ctor(gh_perms * perms, gh_alloc * alloc, gh_permprompter * prompter) {
    gh_result res = gh_permfs_ctor(&perms->filesystem, alloc, GH_PERMACTION_PROMPT);
    if (ghr_iserr(res)) return res;

    perms->prompter = prompter;
    return GHR_OK;
}

static gh_result perms_promptrequest(gh_perms * perms, gh_thread * thread, gh_permfs_abscanonicalpath canonical_path, gh_permactionresult * action_result) {
    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    gh_permfs_reqdata reqdata;
    res = gh_permfs_reqctor(
        &perms->filesystem, thread->safe_id,
        canonical_path, action_result, &reqdata
    );
    if (ghr_iserr(res)) return res;

    gh_permresponse resp;
    res = gh_permprompter_request(perms->prompter, &reqdata.request, &resp);
    if (ghr_iserr(res)) goto end;

    switch(resp) {
    case GH_PERMRESPONSE_ACCEPT:
        break;

    case GH_PERMRESPONSE_REJECT:
        res = GHR_PERMS_REJECTEDUSER;
        break;

    case GH_PERMRESPONSE_ACCEPTREMEMBER:
        res = gh_permfs_add(
            &perms->filesystem,
            (gh_permfs_ident) {
                .path = canonical_path
            },
            (gh_permfs_modeset){
                .mode_accept = action_result->rejected_mode
            },
            (gh_permfs_modeset){0}
        );
        break;

    case GH_PERMRESPONSE_REJECTREMEMBER:
        res = gh_permfs_add(
            &perms->filesystem,
            (gh_permfs_ident) {
                .path = canonical_path
            },
            (gh_permfs_modeset){
                .mode_reject = action_result->rejected_mode
            },
            (gh_permfs_modeset){0}
        );
        if (ghr_isok(res)) res = GHR_PERMS_REJECTEDUSER;
        break;

    case GH_PERMRESPONSE_EMERGENCYKILL:
        res = gh_thread_forcekill(thread);
        if (ghr_isok(res)) res = GHR_PERMS_REJECTEDKILL;
        break;

    default:
        res = GHR_PERMS_REJECTEDPROMPT;
        break;
    }

end:
    inner_res = gh_permfs_reqdtor(&perms->filesystem, &reqdata);
    if (ghr_iserr(inner_res)) return inner_res;
    return res;
}

gh_result gh_perms_actfilesystem(gh_perms * perms, gh_thread * thread, gh_pathfd fd, gh_permfs_mode mode) {
    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    gh_permfs_abscanonicalpath canonical_path;
    res = gh_procfd_fdpathctor(&perms->filesystem.procfd, fd, &canonical_path);
    if (ghr_iserr(res)) return res;

    gh_permfs_modeset modeset = {0};
    res = gh_permfs_getmode(&perms->filesystem, fd.fd, canonical_path, &modeset);
    if (ghr_iserr(res)) return res;

    gh_permactionresult actionres = {0};
    res = gh_permfs_act(&perms->filesystem, modeset, mode, &actionres);

    if (ghr_is(res, GHR_PERMS_REJECTEDPROMPT)) {
        res = perms_promptrequest(perms, thread, canonical_path, &actionres);
    }

    inner_res = gh_procfd_fdpathdtor(&perms->filesystem.procfd, &canonical_path);
    if (ghr_iserr(inner_res)) res = inner_res;
    return res;
}


gh_result gh_perms_openat(gh_perms * perms, gh_thread * thread, int dirfd, const char * path, int flags, mode_t create_mode, int * out_fd) {
    gh_result res = GHR_OK;

    gh_pathfd pathfd = {0};
    res = gh_pathfdopen(&perms->filesystem, dirfd, path, &pathfd);
    if (ghr_iserr(res)) return res;


    gh_permfs_mode mode;
    res = gh_permfs_fcntlflags2permfsmode(flags, &mode);
    if (ghr_iserr(res)) return res;

    res = gh_perms_actfilesystem(perms, thread, pathfd, mode);
    if (ghr_iserr(res)) return res;

    int new_fd = -1;
    res = gh_procfd_reopen(&perms->filesystem.procfd, pathfd, flags, create_mode, &new_fd);
    if (ghr_iserr(res)) return res;

    *out_fd = new_fd;
    return GHR_OK;
}

gh_result gh_perms_dtor(gh_perms * perms) {
    gh_result res = gh_permfs_dtor(&perms->filesystem);
    if (ghr_iserr(res)) return res;
    return GHR_OK;
}
