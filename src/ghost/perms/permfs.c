#define _GNU_SOURCE
#include <string.h>
#include <sys/stat.h>
#include <ghost/result.h>
#include <ghost/dynamic_array.h>
#include <ghost/perms/permfs.h>
#include <ghost/perms/parser.h>

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

gh_result gh_permfs_ctor(gh_permfs * permfs, gh_alloc * alloc, gh_permaction default_action) {
    permfs->default_action = default_action;
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

    gh_result res = gh_alloc_new(permfs->file_perms.alloc, (void**)&file_perm.ident.path.ptr, (ident.path.len + 1) * sizeof(char));
    if (ghr_iserr(res)) return res;
    strncpy(file_perm.ident.path.ptr, ident.path.ptr, ident.path.len + 1);
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

gh_result gh_permfs_getmode(gh_permfs * permfs, int opath_fd, gh_abscanonicalpath canonical_path, gh_permfs_modeset * out_modeset) {
    struct stat stat;
    int fstat_result = fstat(opath_fd, &stat);
    bool exists = false;

    if (fstat_result < 0 && errno == ENOENT) exists = false;
    else if (fstat_result < 0) return ghr_errno(GHR_PERMFS_STATFAIL);

    exists = true;

    if (exists && !file_type_allowed(stat.st_mode)) return GHR_PERMFS_BADTYPE;

    /* gh_abscanonicalpath path; */
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
        if (strncmp(perm->ident.path.ptr, canonical_path.ptr, perm->ident.path.len) != 0) continue;

        if (canonical_path.ptr[perm->ident.path.len] != '/') continue;

        mode = gh_permfs_modeset_join(mode, perm->children);
    }

    if (out_modeset != NULL) *out_modeset = mode;
    return GHR_OK;
}

gh_result gh_permfs_act(gh_permfs * permfs, gh_permfs_modeset modeset, gh_permfs_mode mode, gh_permfs_actionresult * out_result) {
    (void)permfs;

    if ((modeset.mode_reject & mode) != 0) {
        if (out_result != NULL) {
            out_result->action = GH_PERMACTION_REJECT;
            out_result->rejected_mode = mode & (~modeset.mode_reject);
            out_result->default_policy = false;
        }

        return GHR_PERMS_REJECTEDPOLICY;
    }

    if ((modeset.mode_accept & mode) == mode) {
        if (out_result != NULL) {
            out_result->action = GH_PERMACTION_ACCEPT;
            out_result->rejected_mode = 0;
            out_result->default_policy = false;
        }
        return GHR_OK;
    }


    if ((modeset.mode_prompt & mode) != 0) {
        if (out_result != NULL) {
            out_result->action = GH_PERMACTION_PROMPT;
            out_result->rejected_mode = modeset.mode_prompt;
            out_result->default_policy = false;
        }
        return GHR_PERMS_REJECTEDPROMPT;
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

static const gh_permrequest permfs_reqtemplate = {
    .group = "filesystem",
    .resource = "node",
    .fields = {
        { .key = "path" },
        { .key = "mode_self" }
    },
};

static gh_result permfs_mode_fill(gh_bytebuffer * buffer, gh_permfs_mode mode) {
    gh_result res = GHR_OK;

    bool first = true;
    for (size_t i = 0; i < sizeof(gh_permfs_mode) * 8; i++) {
        gh_permfs_mode flag = mode & (1 << i);
        if (flag == 0) continue;

        const char * flagstr = gh_permfs_mode_ident(flag);
        if (flagstr == NULL) continue;

        if (!first) {
            res = gh_bytebuffer_append(buffer, ",", 1);
            if (ghr_iserr(res)) return res;
        }
        first = false;

        res = gh_bytebuffer_append(buffer, flagstr, strlen(flagstr));
        if (ghr_iserr(res)) return res;
    }

    return GHR_OK;
}

gh_result gh_permfs_reqctor(gh_permfs * permfs, const char * source, gh_abscanonicalpath path, const char * hint, gh_permfs_actionresult * result, gh_permfs_reqdata * out_reqdata) {
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

    if (hint != NULL) {
        strcpy(out_reqdata->request.fields[2].key, "hint");
        out_reqdata->request.fields[2].value = hint;
        out_reqdata->request.fields[2].value_len = strlen(hint);
    }

    // request/rejected_mode-based requests will NEVER have the mode_children field,
    // as all automatic requests are executed with the lowest possible range of
    // permissions

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

        if (!gh_pathfd_exists(pathfd)) {
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

static gh_result permfsparser_newentry(gh_permparser * parser, gh_str value, void * userdata, void ** out_entry) {
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

        gh_str action;
        res = gh_permparser_nextidentifier(parser, &action);
        if (ghr_iserr(res)) return res;

        gh_permfs_mode * mode = NULL;
        if (gh_str_eqz(action, "accept")) {
            mode = &modeset->mode_accept;
        } else if (gh_str_eqz(action, "reject")) {
            mode = &modeset->mode_reject;
        } else if (gh_str_eqz(action, "prompt")) {
            mode = &modeset->mode_prompt;
        } else {
            return gh_permparser_resourceerror(parser, "Unknown action type");
        }

        while (true) {
            gh_str arg;
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
