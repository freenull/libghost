#include <stdbool.h>
#include <string.h>
#include <ghost/perms/actreq.h>

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
