/** @defgroup request Permission request
 *
 * @brief A set of key-value pairs and additional information intended to be read and displayed by a prompter.
 *
 * @{
 */

#ifndef GHOST_PERMS_REQUEST_H
#define GHOST_PERMS_REQUEST_H

#include <stddef.h>
#include <stdbool.h>
#include <ghost/strings.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GH_PERMREQUEST_IDMAX 64
typedef char gh_permrequest_id[GH_PERMREQUEST_IDMAX];

#define GH_PERMREQUEST_SOURCEMAX 256
typedef char gh_permrequest_source[GH_PERMREQUEST_SOURCEMAX];

typedef struct {
    gh_permrequest_id key;
    gh_conststr value;
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
void gh_permrequest_setgroup(gh_permrequest * req, const char * group);
void gh_permrequest_setresource(gh_permrequest * req, const char * resource);
gh_permrequest_field * gh_permrequest_setfield(gh_permrequest * req, const char * key, gh_conststr value);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
