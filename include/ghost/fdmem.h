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

/** @brief FDMEM shared memory. */
typedef struct {
    /** @brief Pointer to allocated shared memory. */
    void * data;

    /** @brief Size of occupied data in the shared memory. */
    size_t occupied;

    /** @brief Capacity of the shared memory. */
    size_t size;

    /** @brief File descriptor of the backing anonymous file. */
    int fd;
} gh_fdmem;

#define GH_IPCFDMEM_INITIALCAPACITY 1024

/** @brief Construct FDMEM from file descriptor with occupied size.
 *
 * @param fdmem    Pointer to unconstructed memory that will hold the new instance.
 * @param fd       File descriptor of the backing anonymous file.
 * @param occupied Occupied size of the shared memory.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_fdmem_ctorfdo(gh_fdmem * fdmem, int fd, size_t occupied);

/** @brief Construct FDMEM from file descriptor.
 *
 * @note The constructed FDMEM considers all of the shared memory as occupied.
 *
 * @param fdmem    Pointer to unconstructed memory that will hold the new instance.
 * @param fd       File descriptor of the backing anonymous file.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_fdmem_ctorfd(gh_fdmem * fdmem, int fd);

/** @brief Construct new FDMEM.
 *
 * @param fdmem    Pointer to unconstructed memory that will hold the new instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_fdmem_ctor(gh_fdmem * fdmem);

/** @brief Destroy FDMEM instance.
 *
 * @param fdmem    FDMEM instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_fdmem_dtor(gh_fdmem * fdmem);

/** @brief Synchronize the FDMEM.
 *
 * Remote thread may resize the shared memory. This function updates the size of
 * the memory mapping to the current size of the backing anonymous file.
 *
 * @param fdmem    FDMEM instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_fdmem_sync(gh_fdmem * fdmem);

/** @brief Seal the FDMEM.
 *
 * Before calling this function, there may be no writable instances of this FDMEM
 * in any thread on the operating system. The anonymous file may not be open or
 * mapped in writable mode in any thread in general.
 *
 * After calling this function, the backing anonymous file may never again be
 * opened or mapped in writable mode.
 *
 * @param fdmem    FDMEM instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_fdmem_seal(gh_fdmem * fdmem);

/** @brief Allocate new block of memory in FDMEM.
 *
 * @param fdmem    FDMEM instance.
 * @param size     Size of the block to allocate.
 * @param[out] out_ptr Will hold the pointer to the newly allocated block.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_fdmem_new(gh_fdmem * fdmem, size_t size, void ** out_ptr);

/** @brief Convert FDMEM virtual pointer to real pointer to address space.
 *
 * @param fdmem    FDMEM instance.
 * @param ptr      FDMEM virtual pointer.
 * @param size     Size of the desired block.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
void * gh_fdmem_realptr(gh_fdmem * fdmem, gh_fdmem_ptr ptr, size_t size);

/** @brief Convert real pointer to address space to FDMEM virtual pointer.
 *
 * @param fdmem    FDMEM instance.
 * @param ptr      Pointer.
 * @param size     Size of the desired block.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_fdmem_ptr gh_fdmem_virtptr(gh_fdmem * fdmem, void * ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
