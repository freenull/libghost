#include "ghost/perms/request.h"
#include "ghost/perms/writer.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <ghost/perms/permexec.h>
#include <ghost/result.h>
#include <ghost/dynamic_array.h>
#include <ghost/alloc.h>
#include <ghost/sha256provider.h>
#include <ghost/perms/procfd.h>
#include <ghost/strings.h>

gh_result gh_permexec_hash_build(gh_alloc * alloc, int exe_fd, int argc, char * const * argv, gh_sha256 * out_hash) {
    gh_sha256 exe_hash;
    gh_result res = gh_sha256_fd(exe_fd, alloc, &exe_hash);
    if (ghr_iserr(res)) return res;

    if (argc < 0) return GHR_PERMEXEC_NEGARGC;
    if (argc > GH_PERMEXEC_MAXARGS) return GHR_PERMEXEC_TOOMANYARGS;

    char combined_buffer[GH_SHA256_DIGESTLEN * (1 + argc)];
    off_t combined_buffer_offs = 0;
    memcpy(combined_buffer + combined_buffer_offs, exe_hash.hash, GH_SHA256_DIGESTLEN);
    combined_buffer_offs += GH_SHA256_DIGESTLEN;

    for (int i = 0; i < argc; i++) {
        char * arg = argv[i];
        size_t arg_size = strlen(arg);

        gh_sha256 arg_hash = {0};
        res = gh_sha256_buffer(arg_size, arg, &arg_hash);
        if (ghr_iserr(res)) return res;

        memcpy(combined_buffer + combined_buffer_offs, arg_hash.hash, GH_SHA256_DIGESTLEN);
        combined_buffer_offs += GH_SHA256_DIGESTLEN;
    }

    res = gh_sha256_buffer(sizeof(combined_buffer), combined_buffer, out_hash);
    if (ghr_iserr(res)) return res;

    return res;
}

static const gh_dynamicarrayoptions permexechashlist_daopts = {
    .initial_capacity = GH_PERMEXECHASHLIST_INITIALCAPACITY,
    .max_capacity = GH_PERMEXECHASHLIST_MAXCAPACITY,
    .element_size = sizeof(gh_permexec_entry),
   
    .dtorelement_func = NULL,
    .userdata = NULL
};

gh_result gh_permexec_hashlist_ctor(gh_permexec_hashlist * list, gh_alloc * alloc) {
    list->alloc = alloc;
    return gh_dynamicarray_ctor(GH_DYNAMICARRAY(list), &permexechashlist_daopts);
}

gh_result gh_permexec_hashlist_add(gh_permexec_hashlist * list, gh_permexec_entry * entry) {
    return gh_dynamicarray_append(GH_DYNAMICARRAY(list), &permexechashlist_daopts, entry);
}

gh_result gh_permexec_hashlist_tryget(gh_permexec_hashlist * list, gh_sha256 * hash, gh_permexec_entry ** out_entry) {
    for (size_t i = 0; i < list->size; i++) {
        gh_permexec_entry * entry = list->buffer + i;
        if (gh_sha256_eq(&entry->combined_hash, hash)) {
            *out_entry = entry;
            return GHR_OK;
        }
    }

    return GHR_OK;
}

gh_result gh_permexec_hashlist_dtor(gh_permexec_hashlist * list) {
    return gh_dynamicarray_dtor(GH_DYNAMICARRAY(list), &permexechashlist_daopts);
}

gh_result gh_permexec_ctor(gh_permexec * permexec, gh_alloc * alloc) {
    *permexec = (gh_permexec) { .default_mode = GH_PERMEXEC_REJECT };
    return gh_permexec_hashlist_ctor(&permexec->hashlist, alloc);
}

gh_result gh_permexec_dtor(gh_permexec * permexec) {
    return gh_permexec_hashlist_dtor(&permexec->hashlist);
}

static bool append_escaped(gh_str * str, const char * ptr, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = ptr[i];
        switch(c) {
        case '\\':
            if (!gh_str_appendc(str, "\\\\", 2, false)) return false;
            break;
        case '"':
            if (!gh_str_appendc(str, "\\\"", 2, false)) return false;
            break;
        case '\n':
            if (!gh_str_appendc(str, "\\n", 2, false)) return false;
            break;
        case '\t':
            if (!gh_str_appendc(str, "\\t", 2, false)) return false;
            break;
        default:
            if (!gh_str_appendchar(str, c)) return false;
            break;
        }
    }

    return true;
}

static bool permexec_buildcmdline(gh_str * str, gh_abscanonicalpath exe_path, int argc, char * const * argv) {
    if (!gh_str_appendc(str, exe_path.ptr, exe_path.len, false)) return false;
    if (argc == 0) return true;

    for (int i = 1; i < argc; i++) {
        char * const arg = argv[i];
        if (!gh_str_appendc(str, " \"", 2, false)) return false;
        if (!append_escaped(str, arg, strlen(arg))) return false;
        if (!gh_str_appendchar(str, '"')) return false;
    }

    if (!gh_str_insertnull(str)) return false;

    return true;
}

static bool permexec_buildenv(gh_str * str, int envc, char * const * envp) {
    bool first = true;

    for (int i = 0; i < envc; i++) {
        if (!first) {
            if (!gh_str_appendchar(str, ' ')) return false;
        }
        first = false;

        char * const env = envp[i];
        if (!gh_str_appendchar(str, '"')) return false;
        if (!append_escaped(str, env, strlen(env))) return false;
        if (!gh_str_appendchar(str, '"')) return false;
    }

    if (!gh_str_insertnull(str)) return false;

    return true;
}

static bool permexec_builddescription(gh_str * str, int envc) {
    if (!gh_str_appendz(str,
        "Source '${source}' is asking for permission to run the external program '${programname}' using the following command line: $$"
        "${}    ${cmdline}"
    , true)) return false;

    if (envc > 0) {
        if (!gh_str_appendz(str,
            "$$ The following environment variables will be provided to the program: $$ ${}    ${env}"
        , true)) return false;
    }

    if (!gh_str_appendz(str,
        "$$ If you are not sure of what this program may do, REJECT the request!"
    , true)) return false;

    if (!gh_str_insertnull(str)) return false;

    return true;
}


static gh_result permexec_makerequest(const char * source, gh_abscanonicalpath exe_path, int argc, char * const * argv, int envc, char * const * envp, gh_permexec_reqdata * out_reqdata) {
    out_reqdata->request = (gh_permrequest) {
        .group = "exec",
        .resource = "cmdline"
    };

    strcpy(out_reqdata->request.source, source);

    gh_str cmdline = gh_str_fromc(out_reqdata->cmdline_buffer, 0, GH_PERMEXEC_CMDLINEMAX);
    if (!permexec_buildcmdline(&cmdline, exe_path, argc, argv)) return GHR_PERMEXEC_LARGECMDLINE;

    gh_str env = gh_str_fromc(out_reqdata->env_buffer, 0, GH_PERMEXEC_ENVMAX);
    if (!permexec_buildenv(&env, envc, envp)) return GHR_PERMEXEC_LARGEENV;

    gh_str description = gh_str_fromc(out_reqdata->description_buffer, 0, GH_PERMEXEC_DESCRIPTIONMAX);
    if (!permexec_builddescription(&description, envc)) return GHR_PERMEXEC_LARGEDESCRIPTION;

    size_t field_idx = 0;

    strcpy(out_reqdata->request.fields[field_idx].key, "description");
    out_reqdata->request.fields[field_idx].value.buffer = description.buffer;
    out_reqdata->request.fields[field_idx].value.size = description.size;
    field_idx += 1;

    strcpy(out_reqdata->request.fields[field_idx].key, "cmdline");
    out_reqdata->request.fields[field_idx].value.buffer = cmdline.buffer;
    out_reqdata->request.fields[field_idx].value.size = cmdline.size;
    field_idx += 1;

    strcpy(out_reqdata->request.fields[field_idx].key, "programname");
    if (argc > 0) {
        out_reqdata->request.fields[field_idx].value.buffer = argv[0];
        out_reqdata->request.fields[field_idx].value.size = strlen(argv[0]);
    } else {
        out_reqdata->request.fields[field_idx].value.buffer = "<unknown>";
        out_reqdata->request.fields[field_idx].value.size = strlen("<unknown>");
    }
    field_idx += 1;

    if (envc > 0) {
        strcpy(out_reqdata->request.fields[field_idx].key, "env");
        out_reqdata->request.fields[field_idx].value.buffer = env.buffer;
        out_reqdata->request.fields[field_idx].value.size = env.size;
        field_idx += 1;
    }

    return GHR_OK;
}

gh_result gh_permexec_gate(gh_permexec * permexec, gh_permprompter * prompter, gh_procfd * procfd, const char * safe_id, gh_pathfd exe_fd, char * const * argv, char * const * envp) {
    gh_result inner_res = GHR_OK;
    gh_result res = GHR_OK;

    int readable_exe_fd;
    res = gh_procfd_reopen(procfd, exe_fd, O_RDONLY, 0, &readable_exe_fd);
    if (ghr_iserr(res)) return res;

    char * new_envp[GH_PERMEXEC_MAXALLOWEDENV + 1];
    size_t new_envp_idx = 0;

    for (char * const * kv = envp; *kv != NULL; kv++) {
        if (new_envp_idx >= GH_PERMEXEC_MAXALLOWEDENV) break;

        char * eq_sign = strchr(*kv, '=');
        if (eq_sign == NULL) continue;

        ptrdiff_t key_len = (eq_sign - *kv);
        gh_conststr key = gh_conststr_fromc(*kv, (size_t)key_len);

        for (size_t i = 0; i < GH_PERMEXEC_MAXALLOWEDENV; i++) {
            const char * allowed_key = permexec->allowed_env[i];
            if (allowed_key == NULL) break;
            if (gh_conststr_eqz(key, allowed_key)) {
                new_envp[new_envp_idx] = *kv;
                new_envp_idx += 1;
                if (new_envp_idx >= GH_PERMEXEC_MAXALLOWEDENV) break;
            }
        }
    }

    int envc = (int)new_envp_idx;

    new_envp[new_envp_idx] = NULL;

    int argc = 0;
    for (char * const * arg = argv; *arg != NULL; arg++) {
        argc += 1;
        if (argc >= INT_MAX) break;
    }

    gh_sha256 combined_hash = {0};
    res = gh_permexec_hash_build(permexec->hashlist.alloc, readable_exe_fd, argc, argv, &combined_hash);
    if (ghr_iserr(res)) return res;

    gh_permexec_entry * entry = NULL;
    res = gh_permexec_hashlist_tryget(&permexec->hashlist, &combined_hash, &entry);
    if (ghr_iserr(res)) return res;

    gh_permexec_mode mode = permexec->default_mode;
    if (entry != NULL) mode = entry->mode;

    if (mode == GH_PERMEXEC_ACCEPT) return GHR_OK;
    if (mode == GH_PERMEXEC_REJECT) return GHR_PERMS_REJECTEDPOLICY;

    // Must be PROMPT then.

    gh_abscanonicalpath exe_path;
    res = gh_procfd_fdpathctor(procfd, exe_fd, &exe_path);
    if (ghr_iserr(res)) return res;

    gh_permexec_reqdata reqdata = {0};
    res = permexec_makerequest(safe_id, exe_path, argc, argv, envc, new_envp, &reqdata);
    if (ghr_iserr(res)) goto dtor_exe_path;

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
        res = gh_permexec_hashlist_add(&permexec->hashlist, &(gh_permexec_entry) {
            .mode = GH_PERMEXEC_ACCEPT,
            .combined_hash = combined_hash
        });
        break;

    case GH_PERMRESPONSE_REJECTREMEMBER:
        res = gh_permexec_hashlist_add(&permexec->hashlist, &(gh_permexec_entry) {
            .mode = GH_PERMEXEC_REJECT,
            .combined_hash = combined_hash
        });
        if (ghr_isok(res)) res = GHR_PERMS_REJECTEDUSER;
        break;

    default:
        res = GHR_PERMS_REJECTEDPROMPT;
        break;
    }

dtor_exe_path:
    inner_res = gh_procfd_fdpathdtor(procfd, &exe_path);
    if (ghr_iserr(inner_res)) res = inner_res;

    return res;
}

static gh_result permexecparser_matches(gh_permparser * parser, gh_permrequest_id group_id, gh_permrequest_id resource_id, void * userdata) {
    (void)parser;
    (void)userdata;

    if (gh_str_eqlzrz(group_id, "exec") && gh_str_eqlzrz(resource_id, "cmdline")) return GHR_OK;

    return GHR_PERMPARSER_NOMATCH;
}

static bool hex_fromnibble(char c, uint8_t * out_nibble) {
    if (c >= 'A' && c <= 'F') {
        *out_nibble = ((uint8_t)c - 'A') + 10;
        return true;
    } else if (c >= 'a' && c <= 'f') {
        *out_nibble = ((uint8_t)c - 'a') + 10;
        return true;
    } else if (c >= '0' && c <= '9') {
        *out_nibble = ((uint8_t)c - '0');
        return true;
    }

    return false;
}

static void hex_tonibbles(uint8_t byte, char * out_h, char * out_l) {
    uint8_t low = byte & 0xF;
    uint8_t high = (byte & 0xF0) / 16;

    if (high >= 10) *out_h = 'a' + (high - 10);
    else            *out_h = '0' + high;

    if (low >= 10) *out_l = 'a' + (low - 10);
    else           *out_l = '0' + low;
}

static gh_result permexec_readhash(gh_conststr str, gh_sha256 * hash) {
    if (str.size != GH_SHA256_DIGESTLEN * 2) return GHR_PERMEXEC_NOTHASH; 
    for (size_t i = 0; i < GH_SHA256_DIGESTLEN; i++) {
        uint8_t nibble_high;
        uint8_t nibble_low;
        if (!hex_fromnibble(str.buffer[i * 2 + 0], &nibble_high)) return GHR_PERMEXEC_NOTHASH;
        if (!hex_fromnibble(str.buffer[i * 2 + 1], &nibble_low)) return GHR_PERMEXEC_NOTHASH;
        char byte = nibble_high * 16 + nibble_low;
        hash->hash[i] = byte;
    }

    return GHR_OK;
}

static gh_result permexecparser_newentry(gh_permparser * parser, gh_conststr value, void * userdata, void ** out_entry) {
    (void)parser;

    gh_permexec * permexec = (gh_permexec *)userdata;

    gh_sha256 combined_hash = {0};
    gh_result res = permexec_readhash(value, &combined_hash);
    if (ghr_iserr(res)) return res;

    res = gh_permexec_hashlist_add(&permexec->hashlist, &(gh_permexec_entry) {
        .mode = permexec->default_mode,
        .combined_hash = combined_hash
    });
    if (ghr_iserr(res)) return res;

    *out_entry = permexec->hashlist.buffer + permexec->hashlist.size - 1;

    return GHR_OK;
}

static gh_result permexecparser_setfield(gh_permparser * parser, void * entry_voidp, gh_permrequest_id field, void * userdata) {
    (void)parser;
    (void)userdata;

    gh_permexec_entry * entry = (gh_permexec_entry *)entry_voidp;

    gh_result res;

    if (gh_str_eqlzrz(field, "mode")) {
        gh_conststr action;
        res = gh_permparser_nextidentifier(parser, &action);
        if (ghr_iserr(res)) return res;

        gh_permexec_mode mode = 0;
        if      (gh_conststr_eqz(action, "accept")) mode = GH_PERMEXEC_ACCEPT;
        else if (gh_conststr_eqz(action, "reject")) mode = GH_PERMEXEC_REJECT;
        else if (gh_conststr_eqz(action, "prompt")) mode = GH_PERMEXEC_PROMPT;
        else return gh_permparser_resourceerror(parser, "Unknown action type");

        entry->mode = mode;
    } else {
        return GHR_PERMPARSER_UNKNOWNFIELD;
    }

    return GHR_OK;
}


gh_result gh_permexec_registerparser(gh_permexec * permfs, gh_permparser * parser) {
    return gh_permparser_registerresource(parser, (gh_permresourceparser) {
        .userdata = (void*)permfs,
        .matches = permexecparser_matches,
        .newentry = permexecparser_newentry,
        .setfield = permexecparser_setfield
    });
}

gh_result gh_permexec_write(gh_permexec * permexec, gh_permwriter * writer) {
    gh_result res = GHR_OK;
    res = gh_permwriter_beginresource(writer, "exec", "cmdline");
    if (ghr_iserr(res)) return res;

    for (size_t i = 0; i < permexec->hashlist.size; i++) {
        gh_permexec_entry * entry = permexec->hashlist.buffer + i;

        char hash_bytes[GH_SHA256_DIGESTLEN * 2];
        for (size_t j = 0; j < GH_SHA256_DIGESTLEN; j++) {
            hex_tonibbles((uint8_t)entry->combined_hash.hash[j], hash_bytes + (j * 2 + 0), hash_bytes + (j * 2 + 1));
        }

        res = gh_permwriter_beginentry(writer, hash_bytes, GH_SHA256_DIGESTLEN * 2);
        if (ghr_iserr(res)) return res;

        res = gh_permwriter_field(writer, "mode");
        if (ghr_iserr(res)) return res;

        switch(entry->mode) {
        case GH_PERMEXEC_ACCEPT:
            res = gh_permwriter_fieldargident(writer, "accept", strlen("accept"));
            break;
        case GH_PERMEXEC_REJECT:
            res = gh_permwriter_fieldargstring(writer, "reject", strlen("reject"));
            break;
        case GH_PERMEXEC_PROMPT:
        default:
            res = gh_permwriter_fieldargstring(writer, "prompt", strlen("prompt"));
            break;
        }

        if (ghr_iserr(res)) return res;

        res = gh_permwriter_endentry(writer);
        if (ghr_iserr(res)) return res;
    }
    return gh_permwriter_endresource(writer);
}
