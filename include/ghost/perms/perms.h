/** @defgroup perms Permission store
 *
 * @brief Handles serialization and deserialization of permfs and custom permission stores.
 *
 * @{
 */

#ifndef GHOST_PERMS_H
#define GHOST_PERMS_H

#define _GNU_SOURCE
#include <stdbool.h>
#include <stddef.h>
#include <fcntl.h>
#include <limits.h>
#include <ghost/dynamic_array.h>
#include <ghost/byte_buffer.h>
#include <ghost/result.h>
#include <ghost/alloc.h>
#include <ghost/perms/procfd.h>
#include <ghost/perms/prompt.h>
#include <ghost/perms/permfs.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GH_TYPEDEF_THREAD
typedef struct gh_thread gh_thread;
#define GH_TYPEDEF_THREAD
#endif

// Future proofing struct - in case there is ever support for keys that
// are more than just a single string.
typedef struct {
    gh_conststr key;
} gh_permgeneric_key;

typedef void * gh_permgeneric_ctor_func(void * userdata);
typedef gh_result gh_permgeneric_dtor_func(void * instance, void * userdata);
typedef gh_result gh_permgeneric_match_func(void * instance, gh_permrequest_id group_id, gh_permrequest_id resource_id, void * userdata);
typedef gh_result gh_permgeneric_newentry_func(void * instance, const gh_permgeneric_key * entry, void * userdata, void ** out_entry);
typedef gh_result gh_permgeneric_loadentry_func(void * instance, void * entry, gh_permrequest_id field, gh_permparser * parser, void * userdata);
typedef gh_result gh_permgeneric_save_func(void * instance, gh_permwriter * writer, void * userdata);

typedef struct {
    void * userdata;
    gh_permgeneric_ctor_func * ctor;
    gh_permgeneric_dtor_func * dtor;
    gh_permgeneric_match_func * match;
    gh_permgeneric_newentry_func * newentry;
    gh_permgeneric_loadentry_func * loadentry;
    gh_permgeneric_save_func * save;
    size_t request_cache_size;
} gh_permgeneric;

#define GH_PERMGENERIC_INSTANCE ((gh_permgeneric){ 0 })
gh_result gh_permgeneric_request(gh_permgeneric * permgeneric);

#define GH_PERMGENERIC_IDMAX 64

typedef struct {
    char id[GH_PERMGENERIC_IDMAX];
    const gh_permgeneric * vtable;
    void * instance;
} gh_permgeneric_instance;

#define GH_PERMS_MAXGENERIC 16

typedef struct {
    gh_permprompter prompter;
    gh_permfs filesystem;
    size_t generic_count;
    gh_permgeneric_instance generic[GH_PERMS_MAXGENERIC];
} gh_perms;

gh_result gh_perms_ctor(gh_perms * perms, gh_alloc * alloc, gh_permprompter prompter);
gh_result gh_perms_dtor(gh_perms * perms);
gh_result gh_perms_gatefile(gh_thread * thread, gh_pathfd fd, gh_permfs_mode mode, const char * hint);
gh_result gh_perms_fsrequest(gh_thread * thread, gh_pathfd fd, gh_permfs_mode self_mode, gh_permfs_mode children_mode, const char * hint, bool * out_wouldprompt);

gh_result gh_perms_readfd(gh_perms * perms, int fd, gh_permparser_error * out_parsererror);
gh_result gh_perms_readbuffer(gh_perms * perms, const char * buffer, size_t buffer_len, gh_permparser_error * out_parsererror);
gh_result gh_perms_write(gh_perms * perms, int fd);

gh_result gh_perms_registergeneric(gh_perms * perms, const char * id, const gh_permgeneric * generic);
void * gh_perms_getgeneric(gh_perms * perms, const char * id);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
