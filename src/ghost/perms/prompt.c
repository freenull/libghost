#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ghost/result.h>
#include <ghost/perms/perms.h>
#include <ghost/perms/permfs.h>
#include <ghost/perms/prompt.h>
#include <ghost/perms/request.h>

gh_permprompter gh_permprompter_new(gh_permprompter_func * func, void * userdata) {
    return (gh_permprompter) {
        .func = func,
        .userdata = userdata
    };
}

gh_result gh_permprompter_request(gh_permprompter * prompter, const gh_permrequest * req, gh_permresponse * out_response) {
    gh_result res = prompter->func(req, prompter->userdata, out_response);
    if (prompter->fallback != NULL && (ghr_is(res, GHR_PERMPROMPT_UNSUPPORTEDGROUP) || ghr_is(res, GHR_PERMPROMPT_UNSUPPORTEDRESOURCE))) {
        return gh_permprompter_request(prompter->fallback, req, out_response);
    }
    return res;
}

enum generic_desc_state {
    GENERIC_DESC_TEXT,
    GENERIC_DESC_WHITESPACE,
    GENERIC_DESC_SKIPWHITESPACE,
    GENERIC_DESC_DOLLAR,
    GENERIC_DESC_INBRACE,
};
static gh_result print_generic_description(const gh_permrequest * req, gh_conststr description) {
    (void)req;

    enum generic_desc_state state = GENERIC_DESC_TEXT;

    const char * fragment_start = NULL;
    size_t fragment_len = 0;

    for (size_t i = 0; i < description.size; i++) {
        char c = description.buffer[i];

        switch(state) {
        case GENERIC_DESC_TEXT:
            if (c == '$') {
                state = GENERIC_DESC_DOLLAR;
            } else if (c == ' ' || c == '\t' || c == '\n') {
                fragment_start = NULL;
                fragment_len = 0;
                state = GENERIC_DESC_WHITESPACE;
                i -= 1;
            } else {
                printf("%c", c);
            }
            break;
        case GENERIC_DESC_WHITESPACE:
            if (c == ' ' || c == '\t' || c == '\n') {
                if (fragment_start == NULL) {
                    fragment_start = description.buffer + i;
                }
                fragment_len += 1;
            } else {
                printf("%.*s", (int)fragment_len, fragment_start);

                state = GENERIC_DESC_TEXT;
                i -= 1;
            }
            break;
        case GENERIC_DESC_SKIPWHITESPACE:
            if (c != ' ' && c != '\t' && c != '\n') {
                state = GENERIC_DESC_TEXT;
                i -= 1;
            }
            break;
        case GENERIC_DESC_DOLLAR:
            if (c == '{') {
                fragment_start = NULL;
                fragment_len = 0;

                state = GENERIC_DESC_INBRACE;
            } else if (c == '$') {
                printf("\n");
                state = GENERIC_DESC_SKIPWHITESPACE;
            } else {
                printf("$");
                state = GENERIC_DESC_TEXT;
            }
            break;
        case GENERIC_DESC_INBRACE:
            if (c == '}') {
                char fragment[fragment_len + 1];
                strncpy(fragment, fragment_start, fragment_len);
                fragment[fragment_len] = '\0';

                if      (strcmp(fragment, "group"   ) == 0) printf("%s", req->group);
                else if (strcmp(fragment, "resource") == 0) printf("%s", req->resource);
                else if (strcmp(fragment, "source"  ) == 0) printf("%s", req->source);
                else if (strcmp(fragment, ""        ) == 0) ;
                else {
                    const gh_permrequest_field * field = gh_permrequest_getfield(req, fragment);
                    if (field == NULL) return GHR_PERMPROMPT_MISSINGFIELD;

                    printf(GH_STR_FORMAT, GH_STR_ARG(field->value));
                }

                fragment_start = NULL;
                fragment_len = 0;
                state = GENERIC_DESC_TEXT;
            } else if (fragment_start == NULL) {
                fragment_start = description.buffer + i;
                fragment_len += 1;
            } else {
                fragment_len += 1;
            }
            break;
        default: __builtin_unreachable();
        }
    }

    if (state == GENERIC_DESC_DOLLAR) printf("$");
    else if (state == GENERIC_DESC_INBRACE) return GHR_PERMPROMPT_UNTERMINATEDDESC;

    return GHR_OK;
}

static gh_result simpletui_prompter(const gh_permrequest * req, void * userdata, gh_permresponse * out_response) {
    int input_fd = (int)(uintptr_t)userdata;

    gh_conststr key = gh_conststr_fromlit("");
    const gh_permrequest_field * key_field = gh_permrequest_getfield(req, "key");
    if (key_field != NULL) {
        key = key_field->value;
    }

    const gh_permrequest_field * description_field = gh_permrequest_getfield(req, "description");

    const gh_permrequest_field * hint_field = gh_permrequest_getfield(req, "hint");
    gh_conststr hint_str = gh_conststr_fromlit("");
    if (hint_field != NULL) hint_str = hint_field->value;
    bool future_req = gh_conststr_eqz(hint_str, "future");

    printf("\n");
    printf("== INTERACTIVE PERMISSION REQUEST == \n");

    if (description_field == NULL) {
        if (key.size == 0) {
            printf("Source '%s' is requesting access to a resource of type '%s/%s'.\n", req->source, req->group, req->resource);
        } else {
            printf("Source '%s' is requesting access to a resource of type '%s/%s' identified as '"GH_STR_FORMAT"'.\n", req->source, req->group, req->resource, GH_STR_ARG(key));
        }
    }


    if (description_field != NULL) {
        gh_conststr description = description_field->value;

        gh_result res = print_generic_description(req, description);
        if (ghr_iserr(res)) return res;
        printf("\n");
    } else {
        printf("The contents of the request are as follows:\n");
        for (size_t i = 0; i < GH_PERMREQUEST_MAXFIELDS; i++) {
            const gh_permrequest_field * field = req->fields + i;
            if (field->key[0] == '\0') continue;
            if (strncmp(field->key, "key", GH_PERMREQUEST_IDMAX) == 0) continue;

            printf("Field '%s' contains: \""GH_STR_FORMAT"\"\n", field->key, GH_STR_ARG(field->value));
        }
    }

    printf("\nIf you accept the request, the script will receive access to the object as specified in the list above.");
    printf("\nIf you reject the request, the script will not receive any new permissions.\n");

    char input[3] = " ";
    bool first = true;
    while((input[1] != '\n') || (input[0] != 'Y' && input[0] != 'y' && input[0] != 'X' && input[0] != 'x' && input[0] != 'N' && input[0] != 'n' && (future_req || (input[0] != 'O' && input[0] != 'o')))) {
        if (!first) {
            printf("Invalid response. Try again.\n");
        }
        first = false;

        printf("\nRespond with:\n");
        printf("  Y/y to accept the request and remember your choice\n");
        if (!future_req) {
            printf("  O/o to accept the request, but only once\n");
        }
        printf("  N/n to reject the request\n");
        printf("  X/x to reject the request and remember your choice for this resource\n");
        printf("Confirm with Enter.\n");

        printf("> ");
        fflush(stdout);
        if (read(input_fd, input, 2) != 2) return ghr_errno(GHR_PERMPROMPT_REJECTEDUNKNOWN);
        input[2] = '\0';
    }

    gh_permresponse resp = GH_PERMRESPONSE_REJECT;
    if (input[0] == 'o' || input[0] == 'O') {
        printf("Request accepted.\n");
        resp = GH_PERMRESPONSE_ACCEPT;
    }
    if (input[0] == 'y' || input[0] == 'Y') {
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

    printf("\n");

    *out_response = resp;
    return GHR_OK;
}

gh_permprompter gh_permprompter_simpletui(int fd) {
    return gh_permprompter_new(simpletui_prompter, (void*)(uintptr_t)fd);
}
