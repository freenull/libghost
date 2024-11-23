#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ghost/result.h>
#include <ghost/perms/perms.h>
#include <ghost/perms/prompt.h>

gh_permprompter gh_permprompter_new(gh_permprompter_func * func, void * userdata) {
    return (gh_permprompter) {
        .func = func,
        .userdata = userdata
    };
}

gh_result gh_permprompter_request(gh_permprompter * prompter, const gh_permrequest * req, gh_permresponse * out_response) {
    return prompter->func(req, prompter->userdata, out_response);
}

static const char * human_readable_filesystem_mode(const char * s) {
    if (strcmp(s, "createfile") == 0) return "Create a regular file at the requested path";
    if (strcmp(s, "createdir") == 0) return "Create a directory at the requested path";
    if (strcmp(s, "read") == 0) return "Read the contents of the file";
    if (strcmp(s, "write") == 0) return "Write into the file or truncate it";
    if (strcmp(s, "unlink") == 0) return "Remove the file or empty directory at the requested path";
    return NULL;
}

static gh_result simpletui_printmode(const gh_permrequest_field * field) {
    char mode_str[field->value_len + 1];
    strncpy(mode_str, field->value, field->value_len + 1);

    char * strtok_saveptr = NULL;

    char * s = strtok_r(mode_str, ",", &strtok_saveptr);
    do {
        const char * human_readable = human_readable_filesystem_mode(s);
        if (human_readable == NULL) return ghr_errnoval(GHR_PERMPROMPT_REJECTEDUNKNOWN, EINVAL);
        printf("- %s\n", human_readable);

        s = strtok_r(NULL, ",", &strtok_saveptr);
    } while (s != NULL);

    return GHR_OK;
}

static gh_result simpletui_prompter_filesystem(const gh_permrequest * req, void * userdata, gh_permresponse * out_response) {
    int input_fd = (int)(uintptr_t)userdata;
    
    const gh_permrequest_field * path = gh_permrequest_getfield(req, "path");
    if (path == NULL) return GHR_PERMPROMPT_MISSINGFIELD;
    const gh_permrequest_field * mode_self = gh_permrequest_getfield(req, "mode_self");
    const gh_permrequest_field * mode_children = gh_permrequest_getfield(req, "mode_children");
    if (mode_self == NULL && mode_children == NULL) return GHR_PERMPROMPT_MISSINGFIELD;

    if (mode_self->value_len == 0 && mode_children->value_len == 0) return ghr_errnoval(GHR_PERMPROMPT_REJECTEDUNKNOWN, EINVAL);

    if (gh_permrequest_isresource(req, "node")) {
        printf("\n");
        printf("== INTERACTIVE PERMISSION REQUEST == \n");
        printf("Source '%s' is requesting access to the file path '%s'.\n", req->source, req->resource);

        gh_str path_str = gh_str_fromc(path->value, path->value_len, path->value_len);

        if (mode_self != NULL && mode_self->value_len > 0)  {
            printf("If you accept, the script will have access to do the following with '"GH_STR_FORMAT"':\n", GH_STR_ARG(path_str));
            gh_result res = simpletui_printmode(mode_self);
            if (ghr_iserr(res)) return res;
        }

        if (mode_children != NULL && mode_children->value_len > 0)  {
            printf("If you accept, the script will have access to do the following with ANY files and directories inside '"GH_STR_FORMAT"':\n", GH_STR_ARG(path_str));
            gh_result res = simpletui_printmode(mode_children);
            if (ghr_iserr(res)) return res;
        }

        printf("\nIf you reject the request, the script will not receive any new permissions.\n");
        printf("The script will receive a notification that this request failed unless you respond with '!'.\n");

        printf("\nRespond with:\n");
        printf("  Y/y to accept the request once\n");
        printf("  A/a to accept the request and remember your choice\n");
        printf("  N/n to reject the request\n");
        printf("  X/x to reject the request and remember your choice\n");
        printf("    ! to reject the request and kill the script before it can see the result\n");
        printf("Confirm with Enter.\n");

        char input[3] = " ";
        bool first = true;
        while((input[1] != '\n') || (input[0] != 'Y' && input[0] != 'y' && input[0] != 'A' && input[0] != 'a' && input[0] != 'N' && input[0] != 'n' && input[0] != 'X' && input[0] != 'x' && input[0] != '!')) {
            if (!first) {
                printf("Invalid response. Try again.\n");
            }
            first = false;

            printf("> ");
            if (read(input_fd, input, 2) != 2) return ghr_errno(GHR_PERMPROMPT_REJECTEDUNKNOWN);
            input[2] = '\0';
        }

        gh_permresponse resp = GH_PERMRESPONSE_REJECT;
        if (input[0] == 'y' || input[0] == 'Y') {
            printf("Request accepted.\n");
            resp = GH_PERMRESPONSE_ACCEPT;
        }
        if (input[0] == 'a' || input[0] == 'A') {
            printf("Request accepted and remembered.\n");
            resp = GH_PERMRESPONSE_ACCEPTREMEMBER;
        }
        if (input[0] == 'n' || input[0] == 'N') {
            printf("Request rejected.\n");
            resp = GH_PERMRESPONSE_REJECT;
        }
        if (input[0] == 'x' || input[0] == 'X') {
            printf("Request rejected and remembered.\n");
            resp = GH_PERMRESPONSE_REJECTREMEMBER;
        }
        if (input[0] == '!') {
            printf("Request rejected. The script will be killed immediately.\n");
            resp = GH_PERMRESPONSE_EMERGENCYKILL;
        }

        printf("\n");

        *out_response = resp;
        return GHR_OK;
    }

    return GHR_PERMPROMPT_UNSUPPORTEDRESOURCE;
}

static gh_result simpletui_prompter(const gh_permrequest * req, void * userdata, gh_permresponse * out_response) {
    (void)out_response;
    (void)userdata;

    if (gh_permrequest_isgroup(req, "filesystem")) {
        return simpletui_prompter_filesystem(req, userdata, out_response);
    }

    return GHR_PERMPROMPT_UNSUPPORTEDGROUP;
}

gh_permprompter gh_permprompter_simpletui(int fd) {
    return gh_permprompter_new(simpletui_prompter, (void*)(uintptr_t)fd);
}
