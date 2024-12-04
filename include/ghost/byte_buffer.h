/** @defgroup byte_buffer Byte buffers
 *
 * @brief Dynamically resizable buffer. Like an allocator, but made for building strings and contiguous data structures.
 *
 * @{
 */

#ifndef GHOST_BYTEBUFFER_H
#define GHOST_BYTEBUFFER_H

#include <ghost/result.h>
#include <ghost/alloc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GH_BYTEBUFFER_INITIALCAPACITY 1024
#define GH_BYTEBUFFER_MAXCAPACITY GH_DYNAMICARRAY_NOMAXCAPACITY

typedef struct {
    gh_alloc * alloc;
    char * buffer;
    size_t size;
    size_t capacity;
} gh_bytebuffer;

gh_result gh_bytebuffer_ctor(gh_bytebuffer * buffer, gh_alloc * alloc);
gh_result gh_bytebuffer_append(gh_bytebuffer * buffer, const char * bytes, size_t size);
gh_result gh_bytebuffer_expand(gh_bytebuffer * buffer, size_t size, char ** out_ptr);
gh_result gh_bytebuffer_clear(gh_bytebuffer * buffer);
gh_result gh_bytebuffer_dtor(gh_bytebuffer * buffer);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
