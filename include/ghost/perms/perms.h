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

#ifndef GH_TYPEDEF_THREAD
typedef struct gh_thread gh_thread;
#define GH_TYPEDEF_THREAD
#endif

typedef struct {
    gh_permfs filesystem;
} gh_perms;

gh_result gh_perms_ctor(gh_perms * perms, gh_alloc * alloc);
gh_result gh_perms_actfilesystem(gh_thread * thread, gh_pathfd fd, gh_permfs_mode mode, const char * hint);
gh_result gh_perms_dtor(gh_perms * perms);

gh_result gh_perms_readfd(gh_perms * perms, int fd, gh_permparser_error * out_parsererror);
gh_result gh_perms_readbuffer(gh_perms * perms, const char * buffer, size_t buffer_len, gh_permparser_error * out_parsererror);
gh_result gh_perms_write(gh_perms * perms, int fd);

#endif
