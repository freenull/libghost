#ifndef GHOST_PROMPT_H
#define GHOST_PROMPT_H

#include <stddef.h>
#include <stdbool.h>
#include <ghost/result.h>

#define GH_PERMREQUEST_IDMAX 64
typedef char gh_permrequest_id[GH_PERMREQUEST_IDMAX];

#define GH_PERMREQUEST_SOURCEMAX 256
typedef char gh_permrequest_source[GH_PERMREQUEST_SOURCEMAX];

typedef struct {
    gh_permrequest_id key;
    size_t value_len;
    char * value;
} gh_permrequest_field;

#define GH_PERMREQUEST_MAXFIELDS 16

typedef struct {
    gh_permrequest_source source;
    gh_permrequest_id group;
    gh_permrequest_id resource;
    gh_permrequest_field fields[GH_PERMREQUEST_MAXFIELDS];
} gh_permrequest;

bool gh_permrequest_isgroup(const gh_permrequest * req, const char * group);
bool gh_permrequest_isresource(const gh_permrequest * req, const char * resource);
const gh_permrequest_field * gh_permrequest_getfield(const gh_permrequest * req, const char * key);

typedef enum {
    GH_PERMRESPONSE_ACCEPT,
    GH_PERMRESPONSE_REJECT,
    GH_PERMRESPONSE_ACCEPTREMEMBER,
    GH_PERMRESPONSE_REJECTREMEMBER,
    GH_PERMRESPONSE_EMERGENCYKILL
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
