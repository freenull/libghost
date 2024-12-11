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

/** @brief Create new GHPERM writer.
 *
 * @param fd File descriptor to write to.
 *
 * @return New GHPERM writer.
 */
gh_permwriter gh_permwriter_new(int fd);

/** @brief Begin resource block.
 *
 * Writes: `<group> <resource> {`.
 *
 * @param writer GHPERM writer.
 * @param group Null terminated group type.
 * @param resource Null terminated resource type.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permwriter_beginresource(gh_permwriter * writer, const char * group, const char * resource);

/** @brief End resource block.
 *
 * Writes: `}`.
 *
 * @param writer GHPERM writer.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permwriter_endresource(gh_permwriter * writer);

/** @brief Begin entry block.
 *
 * Writes: `"<entry>" {`.
 *
 * @param writer GHPERM writer.
 * @param entry Null terminated entry key.
 * @param entry_len Length of @p entry.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permwriter_beginentry(gh_permwriter * writer, const char * entry, size_t entry_len);

/** @brief End entry block.
 *
 * Writes: `}`.
 *
 * @param writer GHPERM writer.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permwriter_endentry(gh_permwriter * writer);

/** @brief Begin field.
 *
 * Writes: `<field>`.
 *
 * @param writer GHPERM writer.
 * @param key Null terminated field key.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permwriter_field(gh_permwriter * writer, const char * key);

/** @brief Write argument (as identifier).
 *
 * Writes: ` <value>`.
 *
 * @param writer GHPERM writer.
 * @param value Null terminated argument.
 * @param value_len Length of @p value.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permwriter_fieldargident(gh_permwriter * writer, const char * value, size_t value_len);

/** @brief Write argument (as string).
 *
 * Writes: ` "<value>"`.
 *
 * @param writer GHPERM writer.
 * @param value Null terminated argument.
 * @param value_len Length of @p value.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permwriter_fieldargstring(gh_permwriter * writer, const char * value, size_t value_len);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
