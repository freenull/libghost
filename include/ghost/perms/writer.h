/** @defgroup writer Permission writer
 *
 * @brief Writer for the on-disk permission store format (GHPERM).
 *
 * @{
 */

#ifndef GHOST_PERMS_WRITER_H
#define GHOST_PERMS_WRITER_H

#include <ghost/result.h>
#include <ghost/strings.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GH_PERMWRITER_STACKSIZE 2

typedef enum {
    GH_PERMWRITER_GROUPRESOURCE,
    GH_PERMWRITER_ENTRY,
    GH_PERMWRITER_FIELDARGS
} gh_permwriter_layer;

typedef struct {
    int fd;
    int scope;

    gh_permwriter_layer layer;
} gh_permwriter;

gh_permwriter gh_permwriter_new(int fd);
gh_result gh_permwriter_beginresource(gh_permwriter * writer, const char * group, const char * resource);
gh_result gh_permwriter_endresource(gh_permwriter * writer);
gh_result gh_permwriter_beginentry(gh_permwriter * writer, const char * entry, size_t entry_len);
gh_result gh_permwriter_endentry(gh_permwriter * writer);
gh_result gh_permwriter_field(gh_permwriter * writer, const char * key);
gh_result gh_permwriter_fieldargident(gh_permwriter * writer, const char * value, size_t value_len);
gh_result gh_permwriter_fieldargstring(gh_permwriter * writer, const char * value, size_t value_len);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
