/** @defgroup alloc Allocators
 *
 * @brief Generic allocator interface.
 *
 * @{
 */

#ifndef GHOST_ALLOC_H
#define GHOST_ALLOC_H

#include <stdlib.h>
#include <ghost/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Generic allocator callback.
 *
 * Through the values passed in parameters to this callback, three different
 * operations can be distinguished:
 * - For allocation, `*ptr == NULL` and `old_size == 0`.
 * - For reallocation, `*ptr != NULL` and `new_size != 0`.
 * - For deletion (free), `*ptr != NULL` and `new_size == 0`.
 * - For destruction of the allocator itself, `ptr == NULL`.
 *
 * @param ptr      Indirect pointer to memory. Allocation receives a pointer to a
 *                 pointer holding `NULL`, to which the newly allocated pointer must
 *                 be written. Both reallocation and deletion receive a pointer to a
 *                 non-`NULL` pointer, which is the previously written pointer.
 * @param old_size Old size of the object. Allocation receives 0, both reallocation 
 *                 and deletion receive the actual old size of the buffer.
 * @param new_size New size of the object. Both allocation and reallocation receive
 *                 the desired size. Deletion receives 0.
 *
 * @return Arbitrary result code. @ref GHR_OK indicate success.
 */
typedef gh_result gh_alloc_func(void ** ptr, size_t old_size, size_t new_size, void * userdata);

/** @brief Generic allocator. */
typedef struct {
    /** @brief Callback. See @ref gh_alloc_func. */
    gh_alloc_func * func;

    /** @brief Arbitrary userdata. */
    void * userdata;
} gh_alloc;

/** @brief Construct a new allocator.
 *
 * @param alloc    Pointer to unconstructed memory that will hold the new instance.
 * @param func     Callback. See: @ref gh_alloc_func.
 * @param userdata Arbitrary userdata.
 */
__attribute__((always_inline))
static inline void gh_alloc_ctor(gh_alloc * alloc, gh_alloc_func func, void * userdata) {
    alloc->func = func;
    alloc->userdata = userdata;
}

/** @brief Destroy an allocator.
 *
 * @note Depending on the generic allocator's implementation, destroying it
 *       may be optional.
 *
 * @param alloc    Allocator instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_alloc_dtor(gh_alloc * alloc);


/** @brief Create an allocator based on standard C memory management
 *         functions (`malloc`, `free`, `realloc`).
 *
 * @return New allocator. Destroying this allocator is optional.
 */
gh_alloc gh_alloc_default(void);

/** @brief Allocate memory.
 *
 * @param alloc    Allocator instance.
 * @param out_ptr  Pointer to variable that will hold a pointer to newly
 *                 allocated memory. The variable must be initialized to
 *                 `NULL` before this call.
 * @param size     Size of the memory to allocate.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_alloc_new(gh_alloc * alloc, void ** out_ptr, size_t size);

/** @brief Delete (free) memory.
 *
 * @param alloc    Allocator instance.
 * @param ptr      Pointer to variable that holds a pointer to memory
 *                 allocated by the same allocator. The variable will
 *                 be set to `NULL` after this call.
 * @param old_size Size of the allocated memory.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_alloc_delete(gh_alloc * alloc, void ** ptr, size_t old_size);

/** @brief Resize (realloc) memory.
 *
 * @param alloc    Allocator instance.
 * @param out_ptr  Pointer to variable that holds a pointer to memory
 *                 allocated by the same allocator. The variable may
 *                 be set to a new pointer after this call.
 * @param old_size Size of the allocated memory.
 * @param new_size Size of the new resized memory buffer.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_alloc_resize(gh_alloc * alloc, void ** out_ptr, size_t old_size, size_t new_size);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
