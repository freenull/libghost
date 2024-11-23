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
#include <ghost/perms/permfs.h>
#include <ghost/perms/parser.h>
#include <ghost/perms/writer.h>

gh_result gh_perms_ctor(gh_perms * perms, gh_alloc * alloc) {
    gh_result res = gh_permfs_ctor(&perms->filesystem, alloc, GH_PERMACTION_PROMPT);
    if (ghr_iserr(res)) return res;
    return GHR_OK;
}

static gh_result perms_promptrequest(gh_perms * perms, gh_thread * thread, gh_abscanonicalpath canonical_path, gh_permfs_actionresult * action_result) {
    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    gh_permfs_reqdata reqdata;
    res = gh_permfs_reqctor(
        &perms->filesystem, thread->safe_id,
        canonical_path, action_result, &reqdata
    );
    if (ghr_iserr(res)) return res;

    gh_permresponse resp;
    res = gh_permprompter_request(&thread->rpc->prompter, &reqdata.request, &resp);
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
            (gh_permfs_modeset){0},
            NULL
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
            (gh_permfs_modeset){0},
            NULL
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

    gh_abscanonicalpath canonical_path;
    res = gh_procfd_fdpathctor(&perms->filesystem.procfd, fd, &canonical_path);
    if (ghr_iserr(res)) return res;

    gh_permfs_modeset modeset = {0};
    res = gh_permfs_getmode(&perms->filesystem, fd.fd, canonical_path, &modeset);
    if (ghr_iserr(res)) return res;

    gh_permfs_actionresult actionres = {0};
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
    res = gh_pathfd_open(dirfd, path, &pathfd);
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

static gh_result perms_parse(gh_perms * perms, gh_permparser * parser, gh_permparser_error * out_parsererror) {
    gh_result res = gh_permfs_registerparser(&perms->filesystem, parser);
    if (ghr_iserr(res)) return res;

    res = gh_permparser_parse(parser);
    if (ghr_iserr(res)) {
        if (out_parsererror != NULL) *out_parsererror = parser->error;
        return res;
    }

    return res;
}

gh_result gh_perms_readfd(gh_perms * perms, int fd, gh_permparser_error * out_parsererror) {
    gh_permparser parser;

    gh_result res = gh_permparser_ctorfd(&parser, perms->filesystem.file_perms.alloc, fd);
    if (ghr_iserr(res)) return res;

    res = perms_parse(perms, &parser, out_parsererror);
    
    gh_result inner_res = gh_permparser_dtor(&parser);
    if (ghr_iserr(inner_res)) res = inner_res;

    return res;
}

gh_result gh_perms_readbuffer(gh_perms * perms, const char * buffer, size_t buffer_len, gh_permparser_error * out_parsererror) {
    gh_permparser parser;

    gh_result res = gh_permparser_ctorbuffer(&parser, perms->filesystem.file_perms.alloc, buffer, buffer_len);
    if (ghr_iserr(res)) return res;

    return perms_parse(perms, &parser, out_parsererror);
}

void gh_perms_write(gh_perms * perms, int fd) {
    gh_permwriter writer = gh_permwriter_new(fd);
    gh_permfs_write(&perms->filesystem, &writer);
}
