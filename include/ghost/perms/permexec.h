/** @defgroup permfs permfs
 *
 * @brief External program execution permission handler.
 *
 * @{
 */

#ifndef GHOST_PERMS_PERMEXEC_H
#define GHOST_PERMS_PERMEXEC_H

#include <linux/limits.h>
#include <stdint.h>
#include <ghost/perms/pathfd.h>
#include <ghost/perms/procfd.h>
#include <ghost/perms/prompt.h>
#include <ghost/perms/parser.h>
#include <ghost/perms/writer.h>
#include <ghost/result.h>
#include <ghost/alloc.h>
#include <ghost/dynamic_array.h>
#include <ghost/sha256provider.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GH_PERMEXEC_ACCEPT,
    GH_PERMEXEC_REJECT,
    GH_PERMEXEC_PROMPT
} gh_permexec_mode;

typedef struct {
    gh_permexec_mode mode;
    gh_sha256 combined_hash;
} gh_permexec_entry;

gh_result gh_permexec_hash_build(gh_alloc * alloc, int exe_fd, int argc, char * const * argv, gh_sha256 * out_hash);

#define GH_PERMEXECHASHLIST_INITIALCAPACITY 128
#define GH_PERMEXECHASHLIST_MAXCAPACITY GH_DYNAMICARRAY_NOMAXCAPACITY

typedef struct {
    gh_alloc * alloc;
    gh_permexec_entry * buffer;
    size_t size;
    size_t capacity;
} gh_permexec_hashlist;

#define GH_PERMEXEC_MAXARGS 2048
#define GH_PERMEXEC_MAXALLOWEDENV 32
typedef struct {
    gh_permexec_mode default_mode;
    gh_permexec_hashlist hashlist;
    const char * allowed_env[GH_PERMEXEC_MAXALLOWEDENV];
} gh_permexec;

#define GH_PERMEXEC_DESCRIPTIONMAX 512
#define GH_PERMEXEC_CMDLINEMAX 4096
#define GH_PERMEXEC_ENVMAX 4096
typedef struct {
    gh_permrequest request;
    char description_buffer[GH_PERMEXEC_DESCRIPTIONMAX];
    char cmdline_buffer[GH_PERMEXEC_CMDLINEMAX];
    char env_buffer[GH_PERMEXEC_ENVMAX];
} gh_permexec_reqdata;

gh_result gh_permexec_hashlist_ctor(gh_permexec_hashlist * list, gh_alloc * alloc);
gh_result gh_permexec_hashlist_add(gh_permexec_hashlist * list, gh_permexec_entry * entry);
gh_result gh_permexec_hashlist_tryget(gh_permexec_hashlist * list, gh_sha256 * hash, gh_permexec_entry ** out_entry);
gh_result gh_permexec_hashlist_dtor(gh_permexec_hashlist * list);

gh_result gh_permexec_ctor(gh_permexec * permexec, gh_alloc * alloc);
gh_result gh_permexec_dtor(gh_permexec * permexec);
gh_result gh_permexec_gate(gh_permexec * permexec, gh_permprompter * prompter, gh_procfd * procfd, const char * safe_id, gh_pathfd exe_fd, char * const * argv, char * const * envp);
gh_result gh_permexec_registerparser(gh_permexec * permexec, gh_permparser * parser);
gh_result gh_permexec_write(gh_permexec * permexec, gh_permwriter * writer);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
