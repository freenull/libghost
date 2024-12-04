/** @defgroup fdmem fdmem
 *
 * @brief Anonymous cross-process shared memory backed by Linux memory files.
 *
 * @{
 */

#ifndef GHOST_FDMEM_H
#define GHOST_FDMEM_H

#include <stddef.h>
#include <ghost/result.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t gh_fdmem_ptr;
typedef struct {
    void * data;
    size_t occupied;
    size_t size;
    int fd;
} gh_fdmem;

#define GH_IPCFDMEM_INITIALCAPACITY 1024
gh_result gh_fdmem_ctorfdo(gh_fdmem * fdmem, int fd, size_t occupied);
gh_result gh_fdmem_ctorfd(gh_fdmem * fdmem, int fd);
gh_result gh_fdmem_ctor(gh_fdmem * fdmem);
gh_result gh_fdmem_sync(gh_fdmem * fdmem);
gh_result gh_fdmem_seal(gh_fdmem * fdmem);
gh_result gh_fdmem_new(gh_fdmem * fdmem, size_t size, void ** out_ptr);
void * gh_fdmem_realptr(gh_fdmem * fdmem, gh_fdmem_ptr ptr, size_t size);
gh_fdmem_ptr gh_fdmem_virtptr(gh_fdmem * fdmem, void * ptr, size_t size);
gh_result gh_fdmem_dtor(gh_fdmem * fdmem);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
