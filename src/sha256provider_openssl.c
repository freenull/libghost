#include <ghost/alloc.h>
#include <ghost/sha256provider.h>
#include <ghost/ipc.h> // provides STATIC_ASSERT, should move
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <string.h>
#include <unistd.h>

gh_result gh_sha256_fd(int fd, gh_alloc * alloc, gh_sha256 * out_hash) {
    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    off_t lseek_res = lseek(fd, 0, SEEK_END);
    if (lseek_res < 0) return ghr_errno(GHR_SHA256_FAILED);
    size_t size = (size_t)lseek_res;

    lseek_res = lseek(fd, 0, SEEK_SET);
    if (lseek_res < 0) return ghr_errno(GHR_SHA256_FAILED);

    char * buffer = NULL;
    res = gh_alloc_new(alloc, (void**)&buffer, size);
    if (ghr_iserr(res)) return res;

    off_t readn = read(fd, buffer, size);
    if (readn < 0) {
        res = ghr_errno(GHR_SHA256_FAILED);
        goto free_buffer;
    }

    if ((size_t)readn != size) {
        res = GHR_SHA256_FAILED;
        goto free_buffer;
    }

    lseek_res = lseek(fd, 0, SEEK_SET);
    if (lseek_res < 0) {
        res = GHR_SHA256_FAILED;
        goto free_buffer;
    }

    res = gh_sha256_buffer(size, buffer, out_hash);

free_buffer:
    inner_res = gh_alloc_delete(alloc, (void**)&buffer, size);
    if (ghr_iserr(inner_res)) res = inner_res;

    return res;
}

gh_result gh_sha256_buffer(size_t size, const char * buffer, gh_sha256 * out_hash) {
    gh_result res = GHR_OK; 

    EVP_MD_CTX * mdctx = EVP_MD_CTX_new();
    if (mdctx == NULL) return GHR_SHA256_FAILED;


    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL) != 1) {
        res = GHR_SHA256_FAILED;
        goto free_mdctx;
    }

    if (EVP_DigestUpdate(mdctx, buffer, size) != 1) {
        res = GHR_SHA256_FAILED;
        goto free_mdctx;
    }

    unsigned char * digest = OPENSSL_malloc(EVP_MD_size(EVP_sha256()));
    if (digest == NULL) {
        res = GHR_SHA256_FAILED;
        goto free_mdctx;
    }

    unsigned int digest_len;

	if (EVP_DigestFinal_ex(mdctx, digest, &digest_len) != 1) {
        res = GHR_SHA256_FAILED;
        goto free_digest;
    }

    if (digest_len != GH_SHA256_DIGESTLEN) {
        res = GHR_SHA256_FAILED;
        goto free_digest;
    }

    memcpy(out_hash->hash, digest, GH_SHA256_DIGESTLEN);

free_digest:
    OPENSSL_free(digest);

free_mdctx:
    EVP_MD_CTX_free(mdctx);
    return res;
}
