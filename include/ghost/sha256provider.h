/** @defgroup sha256provider SHA256 Provider
 *
 * @brief Generic functions implementing an interface for hashing files and buffers of bytes.
 *
 * @{
 */

#ifndef GHOST_SHA256PROVIDER_H
#define GHOST_SHA256PROVIDER_H

#include <ghost/result.h>
#include <ghost/alloc.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Size of the SHA256 hash in bytes. */
#define GH_SHA256_DIGESTLEN 32

/** @brief SHA256 hash. */
typedef struct {
    /** @brief Bytes making up the hash. */
    char hash[GH_SHA256_DIGESTLEN];
} gh_sha256;

#define GH_SHA256_MAXBUFFER (4 * 1024 * 1024 * 1024) // 4GiB

/** @brief Hash a file referenced by a file descriptor.
 *
 * @param fd File descriptor.
 * @param alloc Allocator.
 * @param[out] out_hash Will contain the resulting SHA256 hash.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_sha256_fd(int fd, gh_alloc * alloc, gh_sha256 * out_hash);

/** @brief Hash a buffer of bytes.
 *
 * @param size Size of the buffer.
 * @param buffer Buffer of bytes.
 * @param[out] out_hash Will contain the resulting SHA256 hash.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_sha256_buffer(size_t size, const char * buffer, gh_sha256 * out_hash);

/** @brief Compare two SHA256 hashes.
 *
 * @param lhs Lefthand side of the equation.
 * @param rhs Righthand side of the equation.
 *
 * @return True if equal, otherwise false.
 */
__attribute__((always_inline))
static inline bool gh_sha256_eq(gh_sha256 * lhs, gh_sha256 * rhs) {
    for (size_t i = 0; i < GH_SHA256_DIGESTLEN; i++) {
        if (lhs->hash[i] != rhs->hash[i]) return false;
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif

/** @} */
