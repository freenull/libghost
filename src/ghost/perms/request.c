#include <stdbool.h>
#include <string.h>
#include <ghost/perms/request.h>

bool gh_permrequest_isgroup(const gh_permrequest * req, const char * group) {
    return strncmp(req->group, group, strlen(group) + 1) == 0;
}

bool gh_permrequest_isresource(const gh_permrequest * req, const char * resource) {
    return strncmp(req->resource, resource, strlen(resource) + 1) == 0;
}

const gh_permrequest_field * gh_permrequest_getfield(const gh_permrequest * req, const char * key) {
    size_t key_len = strlen(key);
    for (size_t i = 0; i < GH_PERMREQUEST_MAXFIELDS; i++) {
        const gh_permrequest_field * field = req->fields + i;
        if (strncmp(field->key, key, key_len) == 0) {
            return field;
        }
    }

    return NULL;
}

void gh_permrequest_setgroup(gh_permrequest * req, const char * group) {
    strncpy(req->group, group, GH_PERMREQUEST_IDMAX);
    req->group[GH_PERMREQUEST_IDMAX - 1] = '\0';
}

void gh_permrequest_setresource(gh_permrequest * req, const char * resource) {
    strncpy(req->resource, resource, GH_PERMREQUEST_IDMAX);
    req->resource[GH_PERMREQUEST_IDMAX - 1] = '\0';
}

gh_permrequest_field * gh_permrequest_setfield(gh_permrequest * req, const char * key, gh_conststr value) {
    size_t key_len = strlen(key);
    if (key_len > GH_PERMREQUEST_IDMAX - 1) return NULL;

    for (size_t i = 0; i < GH_PERMREQUEST_MAXFIELDS; i++) {
        gh_permrequest_field * field = req->fields + i;
        if (field->key[0] == '\0') {
            strncpy(field->key, key, GH_PERMREQUEST_IDMAX);
            field->value = value;
            return field;
        }
    }

    return NULL;
}
