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

/** @brief Maximum size (with null terminator) of permission request identifiers:
 *         field key, group and resource.
 */
#define GH_PERMREQUEST_IDMAX 64

/** @brief Permission request identifier string. */
typedef char gh_permrequest_id[GH_PERMREQUEST_IDMAX];

/** @brief Maximum size (with null terminator) of permission request source. */
#define GH_PERMREQUEST_SOURCEMAX 512

/** @brief Permission request source string. */
typedef char gh_permrequest_source[GH_PERMREQUEST_SOURCEMAX];

/** @brief Permission request field. */
typedef struct {
    /** @brief Field key. */
    gh_permrequest_id key;
    /** @brief Field value. */
    gh_conststr value;
} gh_permrequest_field;

/** @brief Maximum amount of fields on a permission request. */
#define GH_PERMREQUEST_MAXFIELDS 16

/** @brief Permission request. */
typedef struct {
    /** @brief Source of the permission request. See @ref gh_thread.safe_id. */
    gh_permrequest_source source;
    /** @brief Permission group type. */
    gh_permrequest_id group;
    /** @brief Permission resource type. */
    gh_permrequest_id resource;
    /** @brief List of fields. */
    gh_permrequest_field fields[GH_PERMREQUEST_MAXFIELDS];
} gh_permrequest;

/** @brief Check if permission request concerns a group type.
 *
 * @param req    Pointer to permission request.
 * @param group  Null-terminated group type.
 *
 * @return True if yes, false if not.
 */
bool gh_permrequest_isgroup(const gh_permrequest * req, const char * group);

/** @brief Check if permission request concerns a resource type.
 *
 * @param req       Pointer to permission request.
 * @param resource  Null-terminated resource type.
 *
 * @return True if yes, false if not.
 */
bool gh_permrequest_isresource(const gh_permrequest * req, const char * resource);

/** @brief Retrieve field from permission request.
 *
 * @param req       Pointer to permission request.
 * @param key       Field key.
 *
 * @return Pointer to field data or `NULL` if field with specified key doesn't
 *         exist.
 */
const gh_permrequest_field * gh_permrequest_getfield(const gh_permrequest * req, const char * key);

/** @brief Set field on permission request (or add to).
 *
 * @param req       Pointer to permission request.
 * @param key       Field key.
 * @param value     Value.
 *
 * @return Pointer to field data or `NULL` if setting failed (e.g. if the
 *         field limit has been reached)
 */
gh_permrequest_field * gh_permrequest_setfield(gh_permrequest * req, const char * key, gh_conststr value);

/** @brief Set group type of permission request.
 *
 * @param req       Pointer to permission request.
 * @param group     Null terminated group type string.
 */
void gh_permrequest_setgroup(gh_permrequest * req, const char * group);

/** @brief Set resource type of permission request.
 *
 * @param req       Pointer to permission request.
 * @param resource     Null terminated resource type string.
 */
void gh_permrequest_setresource(gh_permrequest * req, const char * resource);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
