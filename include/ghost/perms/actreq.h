#ifndef GHOST_PERMS_ACTREQ_H
#define GHOST_PERMS_ACTREQ_H

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    GH_PERMACTION_REJECT,
    GH_PERMACTION_ACCEPT,
    GH_PERMACTION_PROMPT,
} gh_permaction;

#define GH_PERMREQUEST_IDMAX 64
typedef char gh_permrequest_id[GH_PERMREQUEST_IDMAX];

#define GH_PERMREQUEST_SOURCEMAX 256
typedef char gh_permrequest_source[GH_PERMREQUEST_SOURCEMAX];

typedef struct {
    gh_permrequest_id key;
    size_t value_len;
    const char * value;
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

#endif
