#ifndef GHOST_PERMS_PROMPT_H
#define GHOST_PERMS_PROMPT_H

#include <stddef.h>
#include <stdbool.h>
#include <ghost/result.h>
#include <ghost/perms/actreq.h>

typedef enum {
    GH_PERMRESPONSE_ACCEPT,
    GH_PERMRESPONSE_REJECT,
    GH_PERMRESPONSE_ACCEPTREMEMBER,
    GH_PERMRESPONSE_REJECTREMEMBER,
} gh_permresponse;

typedef gh_result gh_permprompter_func(const gh_permrequest * req, void * userdata, gh_permresponse * out_response);

// prompter is responsible for adding to permlist
typedef struct {
    gh_permprompter_func * func;
    void * userdata;
} gh_permprompter;

gh_permprompter gh_permprompter_new(gh_permprompter_func * func, void * userdata);
gh_result gh_permprompter_request(gh_permprompter * prompter, const gh_permrequest * req, gh_permresponse * out_response);
gh_permprompter gh_permprompter_simpletui(int fd);

#endif
