#include "ghost/strings.h"
#define _GNU_SOURCE
#include <string.h>
#include <sys/stat.h>
#include <ghost/result.h>
#include <ghost/dynamic_array.h>
#include <ghost/perms/permfs.h>
#include <ghost/perms/parser.h>
#include <ghost/thread.h>

bool gh_permfs_nextmodeflag(gh_permfs_mode * state, gh_permfs_mode * out_flag) {
    if (*state == 0) return false;

    for (size_t i = 0; i < sizeof(gh_permfs_mode) * 8; i++) {
        gh_permfs_mode flag = (*state) & (1 << i);
        if (flag == 0) continue;

        *state &= ~flag;
        *out_flag = flag;
        break;
    }

    return true;
}

bool gh_permfs_ismodevalid(gh_permfs_mode mode) {
    return mode <= GH_PERMFS_ALLFLAGS;
}

gh_result gh_permfs_mode_fromstr(gh_conststr str, gh_permfs_mode * out_mode) {
    char str_copy[str.size + 1];
    strncpy(str_copy, str.buffer, str.size);
    str_copy[str.size] = '\0';

    *out_mode = GH_PERMFS_NONE;

    char * strtok_saveptr = NULL;
    char * s = strtok_r(str_copy, ",", &strtok_saveptr);
    while (s != NULL) {
        gh_permfs_mode flag = gh_permfs_mode_fromident(s);
        if (flag == GH_PERMFS_NONE) return ghr_errnoval(GHR_PERMFS_UNKNOWNMODE, EINVAL);

        *out_mode |= flag;

        s = strtok_r(NULL, ",", &strtok_saveptr);
    }

    return GHR_OK;
}

static gh_result permfsfilelist_dtorelement(gh_dynamicarray da, void * elem_voidp, void * userdata) {
    (void)userdata;
    gh_permfs_entry * entry = (gh_permfs_entry *)elem_voidp;
    return gh_alloc_delete(da.alloc, (void**)(void*)&entry->ident.path.ptr, (entry->ident.path.len + 1) * sizeof(char));
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

gh_result gh_permfs_ctor(gh_permfs * permfs, gh_alloc * alloc) {
    gh_result res = gh_permfs_entrylist_ctor(&permfs->file_perms, alloc);
    if (ghr_iserr(res)) return res;

    res = gh_procfd_ctor(&permfs->procfd, alloc);
    return res;
}

static gh_result permfs_appendentry(gh_permfs * permfs, gh_permfs_ident ident, gh_permfs_modeset self_modeset, gh_permfs_modeset children_modeset, gh_permfs_entry ** out_entry) {
    gh_permfs_entry file_perm = {0};
    file_perm.self = self_modeset;

    file_perm.children = children_modeset;

    file_perm.ident.path.len = 0;
    file_perm.ident.path.ptr = NULL;

    gh_result res = gh_alloc_new(permfs->file_perms.alloc, (void**)(void*)&file_perm.ident.path.ptr, (ident.path.len + 1) * sizeof(char));
    if (ghr_iserr(res)) return res;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"
    // RATIONALE: This constructs the path. The memory for it was just allocated.
    strncpy(file_perm.ident.path.ptr, ident.path.ptr, ident.path.len + 1);
#pragma clang diagnostic pop
    file_perm.ident.path.len = ident.path.len;

    res = gh_permfs_entrylist_append(&permfs->file_perms, file_perm);
    if (ghr_iserr(res)) return res;

    if (out_entry != NULL) *out_entry = permfs->file_perms.buffer + (permfs->file_perms.size - 1);

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

gh_result gh_permfs_add(gh_permfs * permfs, gh_permfs_ident ident, gh_permfs_modeset self_modeset, gh_permfs_modeset children_modeset, gh_permfs_entry ** out_entry) {
    gh_result res = GHR_OK;
    for (size_t i = 0; i < permfs->file_perms.size; i++) {
        gh_permfs_entry * entry = NULL;
        res = gh_permfs_entrylist_getat(&permfs->file_perms, i, &entry);
        if (ghr_iserr(res)) return res;

        if (permfs_identeq(&entry->ident, &ident)) {
            merge_modeset(&entry->self, self_modeset);
            merge_modeset(&entry->self, children_modeset);

            if (out_entry != NULL) *out_entry = entry;
            return GHR_OK;
        }
    }

    return permfs_appendentry(permfs, ident, self_modeset, children_modeset, out_entry);
}

inline static bool file_type_allowed(mode_t mode) {
    return S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode);
}

gh_result gh_permfs_getmode(gh_permfs * permfs, int opath_fd, gh_abscanonicalpath canonical_path, gh_permfs_modeset * out_self_modeset, gh_permfs_modeset * out_children_modeset) {
    struct stat stat;
    int fstat_result = fstat(opath_fd, &stat);
    bool exists = false;

    if (fstat_result < 0 && errno == ENOENT) exists = false;
    else if (fstat_result < 0) return ghr_errno(GHR_PERMFS_STATFAIL);

    exists = true;

    if (exists && !file_type_allowed(stat.st_mode)) return GHR_PERMFS_BADTYPE;

    gh_result res = GHR_OK;
    gh_permfs_modeset self_modeset = {0};
    gh_permfs_modeset children_modeset = {0};

    for (size_t i = 0; i < permfs->file_perms.size; i++) {
        gh_permfs_entry * perm = NULL;
        res = gh_permfs_entrylist_getat(&permfs->file_perms, i, &perm);
        if (ghr_iserr(res)) return res;

        // exact match - for when the exact file path is in the permfsentrylist
        if (strncmp(perm->ident.path.ptr, canonical_path.ptr, canonical_path.len + 1) == 0) {
            self_modeset = gh_permfs_modeset_join(self_modeset, perm->self);
            children_modeset = gh_permfs_modeset_join(children_modeset, perm->children);
            continue;
        }

        // directory match - for when the searched path starts with the entry's path + /
        //
        // this can't be symlink raced, because the input (path) is an absolute canonical path
        // that will NOT have symlinks
        if (strncmp(perm->ident.path.ptr, canonical_path.ptr, perm->ident.path.len) != 0) continue;

        if (canonical_path.ptr[perm->ident.path.len] != '/') continue;

        self_modeset = gh_permfs_modeset_join(self_modeset, perm->children);
        children_modeset = gh_permfs_modeset_join(self_modeset, perm->children);
    }

    if (out_self_modeset != NULL) *out_self_modeset = self_modeset;
    if (out_children_modeset != NULL) *out_children_modeset = children_modeset;
    return GHR_OK;
}

static gh_result permfs_actmode(gh_permfs * permfs, gh_permfs_modeset modeset, gh_permfs_mode mode, gh_permfs_actionresult * out_result) {
    (void)permfs;

    if ((modeset.mode_reject & mode) != 0) {
        if (out_result != NULL) {
            out_result->rejected_mode = mode & (~modeset.mode_reject);
            out_result->default_policy = false;
        }

        return GHR_PERMS_REJECTEDPOLICY;
    }

    gh_permfs_mode accepted_mode = modeset.mode_accept & mode;
    if (accepted_mode != 0) {
        if (accepted_mode == mode) {
            if (out_result != NULL) {
                out_result->rejected_mode = 0;
                out_result->default_policy = false;
            }
            return GHR_OK;
        } else {
            mode = mode & ~(accepted_mode);
        }
    }


    if ((modeset.mode_prompt & mode) != 0) {
        if (out_result != NULL) {
            out_result->rejected_mode = modeset.mode_prompt;
            out_result->default_policy = false;
        }
        return GHR_PERMS_REJECTEDPROMPT;
    }

    if (out_result != NULL) {
        out_result->rejected_mode = mode;
        out_result->default_policy = true;
    }
    return GHR_PERMS_REJECTEDPROMPT;
}

static bool permfs_buildreqdescription(gh_str * str, gh_permfs_mode self_mode, gh_permfs_mode children_mode) {
    if (!gh_str_appendz(str, "Source '${source}' is requesting access to the ${node_type} at ${key}.", true)) return false;

    if (self_mode != GH_PERMFS_NONE) {
        const char * s = "$$If you accept, the script will be able to do the following with the ${node_type} at ${key}:";
        if (!gh_str_appendz(str, s, true)) return false;

        gh_permfs_mode iter = self_mode;
        gh_permfs_mode flag;
        while (gh_permfs_nextmodeflag(&iter, &flag)) {
            if (!gh_str_appendz(str, "$$ - ", false)) return false;

            const char * desc = gh_permfs_mode_desc(flag);
            if (!gh_str_appendz(str, desc, true)) return false;
        }
    }

    if (children_mode != GH_PERMFS_NONE) {
        if (str->size > 0) {
            if (!gh_str_appendz(str, "$$", false)) return false;
        }

        const char * s = "If you accept, the script will be able to do the following with ANY files and directories inside the ${node_type} at ${key}:";
        if (!gh_str_appendz(str, s, true)) return false;

        gh_permfs_mode iter = children_mode;
        gh_permfs_mode flag;
        while (gh_permfs_nextmodeflag(&iter, &flag)) {
            if (!gh_str_appendz(str, "$$- ", false)) return false;

            const char * desc = gh_permfs_mode_desc(flag);
            if (!gh_str_appendz(str, desc, true)) return false;
        }
    }

    gh_str_insertnull(str);
    return true;
}

static gh_result permfs_makerequest(const char * source, gh_abscanonicalpath path, const char * node_type, const char * hint, gh_permfs_mode self_mode, gh_permfs_mode children_mode, gh_permfs_reqdata * out_reqdata) {
    gh_str description_buffer = gh_str_fromc(out_reqdata->description_buffer, 0, sizeof(out_reqdata->description_buffer));
    if (!permfs_buildreqdescription(&description_buffer, self_mode, children_mode)) {
        return GHR_PERMFS_LARGEDESCRIPTION;
    }

    out_reqdata->request = (gh_permrequest) {0};

    strcpy(out_reqdata->request.group, "filesystem");
    strcpy(out_reqdata->request.resource, "node");

    strcpy(out_reqdata->request.fields[0].key, "key");
    out_reqdata->request.fields[0].value.buffer = path.ptr;
    out_reqdata->request.fields[0].value.size = path.len;

    strcpy(out_reqdata->request.fields[1].key, "description");
    out_reqdata->request.fields[1].value.buffer = description_buffer.buffer;
    out_reqdata->request.fields[1].value.size = description_buffer.size;

    strcpy(out_reqdata->request.source, source);

    int field_idx = 2;

    if (node_type == NULL) {
        node_type = "filesystem node";
    }

    strcpy(out_reqdata->request.fields[field_idx].key, "node_type");
    out_reqdata->request.fields[field_idx].value.buffer = node_type;
    out_reqdata->request.fields[field_idx].value.size = strlen(node_type);
    field_idx += 1;

    if (hint != NULL) {
        strcpy(out_reqdata->request.fields[field_idx].key, "hint");
        out_reqdata->request.fields[field_idx].value.buffer = hint;
        out_reqdata->request.fields[field_idx].value.size = strlen(hint);
    }

    // request/rejected_mode-based requests will NEVER have the mode_children field,
    // as all automatic requests are executed with the lowest possible range of
    // permissions

    out_reqdata->path = path;
    goto success;

success:
    return GHR_OK;
}

static gh_result permfs_promptrequest(gh_permfs * permfs, gh_permprompter * prompter, const char * safe_id, gh_abscanonicalpath canonical_path, const char * node_type, const char * hint, gh_permfs_mode self_mode, gh_permfs_mode children_mode) {
    gh_result res = GHR_OK;

    gh_permfs_reqdata reqdata;
    res = permfs_makerequest(
        safe_id,
        canonical_path, node_type,
        hint,
        self_mode, children_mode,
        &reqdata
    );
    if (ghr_iserr(res)) return res;

    gh_permresponse resp;
    res = gh_permprompter_request(prompter, &reqdata.request, &resp);
    if (ghr_iserr(res)) return res;

    switch(resp) {
    case GH_PERMRESPONSE_ACCEPT:
        break;

    case GH_PERMRESPONSE_REJECT:
        res = GHR_PERMS_REJECTEDUSER;
        break;

    case GH_PERMRESPONSE_ACCEPTREMEMBER:
        res = gh_permfs_add(
            permfs,
            (gh_permfs_ident) {
                .path = canonical_path
            },
            (gh_permfs_modeset){
                .mode_accept = self_mode
            },
            (gh_permfs_modeset){
                .mode_accept = children_mode
            },
            NULL
        );
        break;

    case GH_PERMRESPONSE_REJECTREMEMBER:
        res = gh_permfs_add(
            permfs,
            (gh_permfs_ident) {
                .path = canonical_path
            },
            (gh_permfs_modeset){
                .mode_reject = self_mode
            },
            (gh_permfs_modeset){
                .mode_reject = children_mode
            },
            NULL
        );
        if (ghr_isok(res)) res = GHR_PERMS_REJECTEDUSER;
        break;

    default:
        res = GHR_PERMS_REJECTEDPROMPT;
        break;
    }

    return res;
}

static gh_result permfs_getnodetype(gh_pathfd fd, const char ** out_node_type) {
    *out_node_type = NULL;

    struct stat statbuf;
    gh_result res = gh_pathfd_stat(fd, &statbuf);

    if (ghr_isok(res)) {
        if      (S_ISREG(statbuf.st_mode)) *out_node_type = "file";
        else if (S_ISDIR(statbuf.st_mode)) *out_node_type = "directory";
        else if (S_ISLNK(statbuf.st_mode)) *out_node_type = "symbolic link";
        else return GHR_PERMFS_BADTYPE;
    } else if (ghr_is(res, GHR_PATHFD_MAYNOTEXIST)) {
        res = GHR_OK;
    }

    return res;
}

gh_result gh_permfs_gatefile(gh_permfs * permfs, gh_permprompter * prompter, const char * safe_id, gh_pathfd fd, gh_permfs_mode mode, const char * hint) {
    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    gh_abscanonicalpath canonical_path;
    res = gh_procfd_fdpathctor(&permfs->procfd, fd, &canonical_path);
    if (ghr_iserr(res)) return res;

    gh_permfs_modeset modeset = {0};
    res = gh_permfs_getmode(permfs, fd.fd, canonical_path, &modeset, NULL);
    if (ghr_iserr(res)) goto dtor_fdpath;

    gh_permfs_actionresult actionres = {0};
    res = permfs_actmode(permfs, modeset, mode, &actionres);

    if (ghr_is(res, GHR_PERMS_REJECTEDPROMPT)) {
        const char * node_type;
        res = permfs_getnodetype(fd, &node_type);
        if (ghr_iserr(res)) return res;

        res = permfs_promptrequest(permfs, prompter, safe_id, canonical_path, node_type, hint, actionres.rejected_mode, 0);
    }

dtor_fdpath:
    inner_res = gh_procfd_fdpathdtor(&permfs->procfd, &canonical_path);
    if (ghr_iserr(inner_res)) res = inner_res;

    return res;
}

gh_result gh_permfs_requestnode(gh_permfs * permfs, gh_permprompter * prompter, const char * safe_id, gh_pathfd fd, gh_permfs_mode self_mode, gh_permfs_mode children_mode, const char * hint, bool * out_wouldprompt) {
    if (out_wouldprompt != NULL) *out_wouldprompt = false;

    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    if (self_mode == GH_PERMFS_NONE && children_mode == GH_PERMFS_NONE) {
        // If both modes are 0, that may indicate a bug. If we show a prompt
        // with both modes 0, the prompt may fail or show the user a blank
        // question, which is both confusing and potentially risky.
        // Because of this, we fail early here.
        return GHR_PERMFS_NULLREQUEST;
    }

    gh_abscanonicalpath canonical_path;
    res = gh_procfd_fdpathctor(&permfs->procfd, fd, &canonical_path);
    if (ghr_iserr(res)) return res;

    const char * node_type;
    res = permfs_getnodetype(fd, &node_type);
    if (ghr_iserr(res)) goto dtor_fdpath;

    gh_permfs_modeset self_modeset = {0};
    gh_permfs_modeset children_modeset = {0};
    res = gh_permfs_getmode(permfs, fd.fd, canonical_path, &self_modeset, &children_modeset);
    if (ghr_iserr(res)) goto dtor_fdpath;

    if (self_mode != GH_PERMFS_NONE) {
        gh_permfs_actionresult actionres = {0};
        res = permfs_actmode(permfs, self_modeset, self_mode, &actionres);
        if (ghr_isok(res)) {
            // If actmode returned success, that means that all of the mode flags
            // are already allowed for this entry - we will not prompt for self
            // flags at all
            self_mode = GH_PERMFS_NONE;
        } else if (ghr_is(res, GHR_PERMS_REJECTEDPROMPT)) {
            // If actmode says we need to prompt for this permission, we just update
            // the mode to what was rejected (the missing flags) and move on, since
            // we're about to prompt anyway
            self_mode = actionres.rejected_mode;
            res = GHR_OK;
        } else {
            // On other errors, including rejection by security policy (i.e. by
            // 'self reject' specifically), we bail. Scripts cannot prompt for
            // permissions that have been explicitly rejected forever.
            goto dtor_fdpath;
        }
    }

    if (children_mode != GH_PERMFS_NONE) {
        // This works exactly like the self_modeset setup above
        gh_permfs_actionresult actionres = {0};
        res = permfs_actmode(permfs, children_modeset, children_mode, &actionres);
        if (ghr_isok(res)) {
            children_mode = GH_PERMFS_NONE;
        } else if (ghr_is(res, GHR_PERMS_REJECTEDPROMPT)) {
            children_mode = actionres.rejected_mode;
            res = GHR_OK;
        } else {
            goto dtor_fdpath;
        }
    }

    if (self_mode == GH_PERMFS_NONE && children_mode == GH_PERMFS_NONE) {
        // If this is true now, that means that both the requested self and children 
        // modesets are already met. If so, just return without prompting the user.
        // This is the only condition where requesting permissions succeeds
        // without actually triggering the prompt.
        goto dtor_fdpath;
    }

    // If out_wouldprompt is provided, this is a dry-run and doesn't actually
    // ask for permissions
    if (out_wouldprompt != NULL) {
        *out_wouldprompt = true;
    } else {
        res = permfs_promptrequest(permfs, prompter, safe_id, canonical_path, node_type, hint, self_mode, children_mode);
    }

dtor_fdpath:
    inner_res = gh_procfd_fdpathdtor(&permfs->procfd, &canonical_path);
    if (ghr_iserr(inner_res)) res = inner_res;
    return res;
}

gh_result gh_permfs_dtor(gh_permfs * permfs) {
    gh_result res = gh_permfs_entrylist_dtor(&permfs->file_perms);
    if (ghr_iserr(res)) return res;

    res = gh_procfd_dtor(&permfs->procfd);
    return res;
}

gh_result gh_permfs_fcntlflags2permfsmode(int fcntl_flags, mode_t create_accessmode, gh_pathfd pathfd, gh_permfs_mode * out_mode) {
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
    
    if (fcntl_flags & O_TRUNC) fcntl_flags &= ~O_TRUNC;
    GH_PERMS_FCNTLMODE2PERMMODE_EXCHANGEFLAG(O_APPEND, GH_PERMFS_WRITE);

    if (fcntl_flags & O_CREAT) {
        fcntl_flags = (int)((unsigned int)fcntl_flags & (~((unsigned int)(O_CREAT))));

        if (!gh_pathfd_guaranteedtoexist(pathfd)) {
            permfs_mode = permfs_mode | GH_PERMFS_CREATEFILE;

            mode_t cm = 0;
            if ((cm = create_accessmode & S_IRUSR)) {
                permfs_mode |= GH_PERMFS_ACCESS_USER_READ;
                create_accessmode &= ~cm;
            }
            if ((cm = create_accessmode & S_IWUSR)) {
                permfs_mode |= GH_PERMFS_ACCESS_USER_WRITE;
                create_accessmode &= ~cm;
            }
            if ((cm = create_accessmode & S_IXUSR)) {
                permfs_mode |= GH_PERMFS_ACCESS_USER_EXECUTE;
                create_accessmode &= ~cm;
            }

            if ((cm = create_accessmode & S_IRGRP)) {
                permfs_mode |= GH_PERMFS_ACCESS_GROUP_READ;
                create_accessmode &= ~cm;
            }
            if ((cm = create_accessmode & S_IWGRP)) {
                permfs_mode |= GH_PERMFS_ACCESS_GROUP_WRITE;
                create_accessmode &= ~cm;
            }
            if ((cm = create_accessmode & S_IXGRP)) {
                permfs_mode |= GH_PERMFS_ACCESS_GROUP_EXECUTE;
                create_accessmode &= ~cm;
            }

            if ((cm = create_accessmode & S_IROTH)) {
                permfs_mode |= GH_PERMFS_ACCESS_OTHER_READ;
                create_accessmode &= ~cm;
            }
            if ((cm = create_accessmode & S_IWOTH)) {
                permfs_mode |= GH_PERMFS_ACCESS_OTHER_WRITE;
                create_accessmode &= ~cm;
            }
            if ((cm = create_accessmode & S_IXOTH)) {
                permfs_mode |= GH_PERMFS_ACCESS_OTHER_EXECUTE;
                create_accessmode &= ~cm;
            }

            if (create_accessmode != 0) return GHR_PERMFS_BADFCNTLMODE;
        }
    }
    GH_PERMS_FCNTLMODE2PERMMODE_EXCHANGEFLAG(O_DIRECTORY, 0);

    if (fcntl_flags != 0) {
        return GHR_PERMFS_BADFCNTLMODE;
    }

    if (out_mode != NULL) *out_mode = permfs_mode;
    return GHR_OK;

#undef GH_PERMS_FCNTLMODE2PERMMODE
}

static gh_result permfsparser_matches(gh_permparser * parser, gh_permrequest_id group_id, gh_permrequest_id resource_id, void * userdata) {
    (void)parser;
    (void)userdata;

    if (gh_str_eqlzrz(group_id, "filesystem") && gh_str_eqlzrz(resource_id, "node")) return GHR_OK;

    return GHR_PERMPARSER_NOMATCH;
}

static gh_result permfsparser_newentry(gh_permparser * parser, gh_conststr value, void * userdata, void ** out_entry) {
    (void)parser;

    gh_permfs * permfs = (gh_permfs *)userdata;
    gh_result res = gh_permfs_add(permfs, (gh_permfs_ident) {
        .path = (gh_abscanonicalpath) {
            .ptr = value.buffer,
            .len = value.size
        }
    }, GH_PERMFS_MODESET_EMPTY, GH_PERMFS_MODESET_EMPTY, (gh_permfs_entry **)out_entry);
    if (ghr_iserr(res)) return res;

    return GHR_OK;
}

static gh_result permfsparser_setfield(gh_permparser * parser, void * entry_voidp, gh_permrequest_id field, void * userdata) {
    (void)parser;
    (void)userdata;

    gh_permfs_entry * entry = (gh_permfs_entry *)entry_voidp;

    gh_result res;

    if (gh_str_eqlzrz(field, "self") || gh_str_eqlzrz(field, "children")) {
        gh_permfs_modeset * modeset = gh_str_eqlzrz(field, "self") ? &entry->self : &entry->children;

        gh_conststr action;
        res = gh_permparser_nextidentifier(parser, &action);
        if (ghr_iserr(res)) return res;

        gh_permfs_mode * mode = NULL;
        if (gh_conststr_eqz(action, "accept")) {
            mode = &modeset->mode_accept;
        } else if (gh_conststr_eqz(action, "reject")) {
            mode = &modeset->mode_reject;
        } else if (gh_conststr_eqz(action, "prompt")) {
            mode = &modeset->mode_prompt;
        } else {
            return gh_permparser_resourceerror(parser, "Unknown action type");
        }

        while (true) {
            gh_conststr arg;
            res = gh_permparser_nextstring(parser, &arg);
            if (ghr_is(res, GHR_PERMPARSER_EXPECTEDSTRING)) break;
            if (ghr_iserr(res)) return res;

            char flag_ident[arg.size + 1];
            strcpy(flag_ident, arg.buffer);
            flag_ident[arg.size] = '\0';

            gh_permfs_mode new_mode_flag = gh_permfs_mode_fromident(flag_ident);
            if (new_mode_flag == 0) return gh_permparser_resourceerror(parser, "Unknown mode flag");

            *mode |= new_mode_flag;
        }
    } else {
        return GHR_PERMPARSER_UNKNOWNFIELD;
    }

    return GHR_OK;
}


gh_result gh_permfs_registerparser(gh_permfs * permfs, gh_permparser * parser) {
    return gh_permparser_registerresource(parser, (gh_permresourceparser) {
        .userdata = (void*)permfs,
        .matches = permfsparser_matches,
        .newentry = permfsparser_newentry,
        .setfield = permfsparser_setfield
    });
}

static gh_result permfs_writemode(gh_permfs * permfs, gh_permwriter * writer, const char * modeset_name, const char * mode_name, gh_permfs_mode mode) {
    gh_result res = GHR_OK;

    if (mode == 0) return res;

    (void)permfs;

    res = gh_permwriter_field(writer, modeset_name);
    if (ghr_iserr(res)) return res;

    res = gh_permwriter_fieldargident(writer, mode_name, strlen(mode_name));
    if (ghr_iserr(res)) return res;

    gh_permfs_mode iter = mode;
    gh_permfs_mode flag;
    while (gh_permfs_nextmodeflag(&iter, &flag)) {
        const char * flag_str = gh_permfs_mode_ident(flag);
        if (flag_str == NULL) return GHR_PERMFS_UNKNOWNMODE;
        res = gh_permwriter_fieldargstring(writer, flag_str, strlen(flag_str));
        if (ghr_iserr(res)) return res;
    }

    return res;
}

static gh_result permfs_writemodeset(gh_permfs * permfs, gh_permwriter * writer, const char * modeset_name, gh_permfs_modeset modeset) {
    gh_result res = GHR_OK;
    res = permfs_writemode(permfs, writer, modeset_name, "accept", modeset.mode_accept);
    if (ghr_iserr(res)) return res;

    res = permfs_writemode(permfs, writer, modeset_name, "reject", modeset.mode_reject);
    if (ghr_iserr(res)) return res;

    res = permfs_writemode(permfs, writer, modeset_name, "prompt", modeset.mode_prompt);
    if (ghr_iserr(res)) return res;

    return res;
}

gh_result gh_permfs_write(gh_permfs * permfs, gh_permwriter * writer) {
    gh_result res = GHR_OK;
    res = gh_permwriter_beginresource(writer, "filesystem", "node");
    if (ghr_iserr(res)) return res;

    for (size_t i = 0; i < permfs->file_perms.size; i++) {
        gh_permfs_entry * entry = permfs->file_perms.buffer + i;
        res = gh_permwriter_beginentry(writer, entry->ident.path.ptr, entry->ident.path.len);
        if (ghr_iserr(res)) return res;

        res = permfs_writemodeset(permfs, writer, "self", entry->self);
        if (ghr_iserr(res)) return res;

        res = permfs_writemodeset(permfs, writer, "children", entry->children);
        if (ghr_iserr(res)) return res;

        res = gh_permwriter_endentry(writer);
        if (ghr_iserr(res)) return res;
    }
    return gh_permwriter_endresource(writer);
}
