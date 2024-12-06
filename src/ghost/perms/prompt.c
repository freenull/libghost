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
static gh_result print_generic_description(const gh_permrequest * req, gh_conststr description, FILE * output) {
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
                fprintf(output, "%c", c);
            }
            break;
        case GENERIC_DESC_WHITESPACE:
            if (c == ' ' || c == '\t' || c == '\n') {
                if (fragment_start == NULL) {
                    fragment_start = description.buffer + i;
                }
                fragment_len += 1;
            } else {
                fprintf(output, "%.*s", (int)fragment_len, fragment_start);

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
                fprintf(output, "\n");
                state = GENERIC_DESC_SKIPWHITESPACE;
            } else {
                fprintf(output, "$");
                state = GENERIC_DESC_TEXT;
            }
            break;
        case GENERIC_DESC_INBRACE:
            if (c == '}') {
                char fragment[fragment_len + 1];
                strncpy(fragment, fragment_start, fragment_len);
                fragment[fragment_len] = '\0';

                if      (strcmp(fragment, "group"   ) == 0) fprintf(output, "%s", req->group);
                else if (strcmp(fragment, "resource") == 0) fprintf(output, "%s", req->resource);
                else if (strcmp(fragment, "source"  ) == 0) fprintf(output, "%s", req->source);
                else if (strcmp(fragment, ""        ) == 0) ;
                else {
                    const gh_permrequest_field * field = gh_permrequest_getfield(req, fragment);
                    if (field == NULL) return GHR_PERMPROMPT_MISSINGFIELD;

                    fprintf(output, GH_STR_FORMAT, GH_STR_ARG(field->value));
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

    if (state == GENERIC_DESC_DOLLAR) fprintf(output, "$");
    else if (state == GENERIC_DESC_INBRACE) return GHR_PERMPROMPT_UNTERMINATEDDESC;

    return GHR_OK;
}

static gh_result simpletui_prompter(const gh_permrequest * req, void * userdata, gh_permresponse * out_response) {
    uintptr_t input_userdata = (uintptr_t)(uintptr_t)userdata;

    FILE * output = stdout;
    if (input_userdata & GH_PERMPROMPTER_STDERRFLAG) {
        output = stderr;
        input_userdata &= ~GH_PERMPROMPTER_STDERRFLAG;
    }

    int input_fd = (int)input_userdata;

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

    fprintf(output, "\n");
    fprintf(output, "== INTERACTIVE PERMISSION REQUEST == \n");

    if (description_field == NULL) {
        if (key.size == 0) {
            fprintf(output, "Source '%s' is requesting access to a resource of type '%s/%s'.\n", req->source, req->group, req->resource);
        } else {
            fprintf(output, "Source '%s' is requesting access to a resource of type '%s/%s' identified as '"GH_STR_FORMAT"'.\n", req->source, req->group, req->resource, GH_STR_ARG(key));
        }
    }


    if (description_field != NULL) {
        gh_conststr description = description_field->value;

        gh_result res = print_generic_description(req, description, output);
        if (ghr_iserr(res)) return res;
        fprintf(output, "\n");
    } else {
        fprintf(output, "The contents of the request are as follows:\n");
        for (size_t i = 0; i < GH_PERMREQUEST_MAXFIELDS; i++) {
            const gh_permrequest_field * field = req->fields + i;
            if (field->key[0] == '\0') continue;
            if (strncmp(field->key, "key", GH_PERMREQUEST_IDMAX) == 0) continue;

            fprintf(output, "Field '%s' contains: \""GH_STR_FORMAT"\"\n", field->key, GH_STR_ARG(field->value));
        }
    }

    fprintf(output, "\nIf you accept the request, the script will receive access to the object as specified in the list above.");
    fprintf(output, "\nIf you reject the request, the script will not receive any new permissions.\n");

    char input[3] = " ";
    bool first = true;
    while((input[1] != '\n') || (input[0] != 'Y' && input[0] != 'y' && input[0] != 'X' && input[0] != 'x' && input[0] != 'N' && input[0] != 'n' && (future_req || (input[0] != 'O' && input[0] != 'o')))) {
        if (!first) {
            fprintf(output, "Invalid response. Try again.\n");
        }
        first = false;

        fprintf(output, "\nRespond with:\n");
        fprintf(output, "  Y/y to accept the request and remember your choice\n");
        if (!future_req) {
            fprintf(output, "  O/o to accept the request, but only once\n");
        }
        fprintf(output, "  N/n to reject the request\n");
        fprintf(output, "  X/x to reject the request and remember your choice for this resource\n");
        fprintf(output, "Confirm with Enter.\n");

        fprintf(output, "> ");
        fflush(stdout);
        if (read(input_fd, input, 2) != 2) return ghr_errno(GHR_PERMPROMPT_REJECTEDUNKNOWN);
        input[2] = '\0';
    }

    gh_permresponse resp = GH_PERMRESPONSE_REJECT;
    if (input[0] == 'o' || input[0] == 'O') {
        fprintf(output, "Request accepted.\n");
        resp = GH_PERMRESPONSE_ACCEPT;
    }
    if (input[0] == 'y' || input[0] == 'Y') {
        fprintf(output, "Request accepted and remembered.\n");
        resp = GH_PERMRESPONSE_ACCEPTREMEMBER;
    }
    if (input[0] == 'n' || input[0] == 'N') {
        fprintf(output, "Request rejected.\n");
        resp = GH_PERMRESPONSE_REJECT;
    }
    if (input[0] == 'x' || input[0] == 'X') {
        fprintf(output, "Request rejected and remembered.\n");
        resp = GH_PERMRESPONSE_REJECTREMEMBER;
    }

    fprintf(output, "\n");

    *out_response = resp;
    return GHR_OK;
}

gh_permprompter gh_permprompter_simpletui(int in_fd) {
    return gh_permprompter_new(simpletui_prompter, (void*)(uintptr_t)in_fd);
}

gh_permprompter gh_permprompter_simpletui_stderr(int in_fd) {
    uintptr_t packed = GH_PERMPROMPTER_STDERRFLAG | (uintptr_t)in_fd;

    return gh_permprompter_new(simpletui_prompter, (void*)packed);
}
