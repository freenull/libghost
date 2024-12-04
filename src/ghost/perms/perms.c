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
#include <ghost/perms/request.h>

gh_result gh_perms_ctor(gh_perms * perms, gh_alloc * alloc, gh_permprompter prompter) {
    *perms = (gh_perms) {0};
    perms->generic_count = 0;
    perms->prompter = prompter;

    gh_result res = gh_permfs_ctor(&perms->filesystem, alloc);
    if (ghr_iserr(res)) return res;
    return GHR_OK;
}

gh_result gh_perms_gatefile(gh_thread * thread, gh_pathfd fd, gh_permfs_mode mode, const char * hint) {
    return gh_permfs_gatefile(&thread->perms.filesystem, &thread->perms.prompter, thread->safe_id, fd, mode, hint);
}

gh_result gh_perms_fsrequest(gh_thread * thread, gh_pathfd fd, gh_permfs_mode self_mode, gh_permfs_mode children_mode, const char * hint, bool * out_wouldprompt) {
    return gh_permfs_requestnode(&thread->perms.filesystem, &thread->perms.prompter,thread->safe_id, fd, self_mode, children_mode, hint, out_wouldprompt);
}

gh_result gh_perms_dtor(gh_perms * perms) {
    gh_result res = GHR_OK;
    for (size_t i = 0; i < perms->generic_count; i++) {
        gh_permgeneric_instance * instance = perms->generic + i;
        res = instance->vtable->dtor(instance->instance, instance->vtable->userdata);

        // We intentionally ignore the result here to let perms clean up
        // even in the event of an error from a generic permission handler.
    }

    res = gh_permfs_dtor(&perms->filesystem);
    if (ghr_iserr(res)) return res;
    return GHR_OK;
}

static gh_result permgeneric_matches(gh_permparser * parser, gh_permrequest_id group_id, gh_permrequest_id resource_id, void * userdata) {
    (void)parser;

    gh_permgeneric_instance * instance = (gh_permgeneric_instance *)userdata;
    return instance->vtable->match(instance->instance, group_id, resource_id, instance->vtable->userdata);
}

static gh_result permgeneric_newentry(gh_permparser * parser, gh_conststr key, void * userdata, void ** out_entry) {
    (void)parser;

    gh_permgeneric_instance * instance = (gh_permgeneric_instance *)userdata;

    const gh_permgeneric_key pg_key = {
        .key = key
    };

    return instance->vtable->newentry(instance->instance, &pg_key, instance->vtable->userdata, out_entry);
}

static gh_result permgeneric_setfield(gh_permparser * parser, void * entry_voidp, gh_permrequest_id field, void * userdata) {
    gh_permgeneric_instance * instance = (gh_permgeneric_instance *)userdata;

    return instance->vtable->loadentry(instance->instance, entry_voidp, field, parser, instance->vtable->userdata);
}

static gh_result perms_parse(gh_perms * perms, gh_permparser * parser, gh_permparser_error * out_parsererror) {
    gh_result res = gh_permfs_registerparser(&perms->filesystem, parser);
    if (ghr_iserr(res)) return res;

    for (size_t i = 0; i < perms->generic_count; i++) {
        gh_permgeneric_instance * instance = perms->generic + i;

        res = gh_permparser_registerresource(parser, (gh_permresourceparser) {
            .userdata = (void*)instance,
            .matches = permgeneric_matches,
            .newentry = permgeneric_newentry,
            .setfield = permgeneric_setfield
        });
        if (ghr_iserr(res)) return res;
    }

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

gh_result gh_perms_write(gh_perms * perms, int fd) {
    gh_permwriter writer = gh_permwriter_new(fd);
    gh_result res = gh_permfs_write(&perms->filesystem, &writer);
    if (ghr_iserr(res)) return res;

    for (size_t i = 0; i < perms->generic_count; i++) {
        gh_permgeneric_instance * instance = perms->generic + i;
        res = instance->vtable->save(instance->instance, &writer, instance->vtable->userdata);
        if (ghr_iserr(res)) return res;
    }
    
    return res;
}

gh_result gh_perms_registergeneric(gh_perms * perms, const char * id, const gh_permgeneric * generic) {
    size_t id_len = strlen(id);
    if (id_len > GH_PERMGENERIC_IDMAX - 1) return GHR_PERMS_GENERICLARGEID;

    if (perms->generic_count >= GH_PERMS_MAXGENERIC) return GHR_PERMS_MAXGENERIC;

    void * inst = generic->ctor(generic->userdata);
    if (inst == NULL) return GHR_PERMS_GENERICCTOR;

    gh_permgeneric_instance instance = (gh_permgeneric_instance) {
        .vtable = generic,
        .instance = inst
    };
    strcpy(instance.id, id);

    perms->generic[perms->generic_count] = instance;
    perms->generic_count += 1;

    return GHR_OK;
}

void * gh_perms_getgeneric(gh_perms * perms, const char * id) {
    for (size_t i = 0; i < perms->generic_count; i++) {
        gh_permgeneric_instance * instance = perms->generic + i;

        if (strcmp(instance->id, id) == 0) return instance->instance;
    }

    return NULL;
}
