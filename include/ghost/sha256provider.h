#ifndef GHOST_SHA256PROVIDER_H
#define GHOST_SHA256PROVIDER_H

#include <ghost/result.h>
#include <ghost/alloc.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GH_SHA256_DIGESTLEN 32

typedef struct {
    char hash[GH_SHA256_DIGESTLEN];
} gh_sha256;

#define GH_SHA256_MAXBUFFER (4 * 1024 * 1024 * 1024) // 4GiB

gh_result gh_sha256_fd(int fd, gh_alloc * alloc, gh_sha256 * out_hash);
gh_result gh_sha256_buffer(size_t size, const char * buffer, gh_sha256 * out_hash);

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
