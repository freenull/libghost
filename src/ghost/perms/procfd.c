#define _GNU_SOURCE
#include <stdbool.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <ghost/perms/procfd.h>

#define GH_PROCFD_FDFILENAMEMAXFD 9999
gh_result gh_procfd_ctor(gh_procfd * procfd, gh_alloc * alloc) {
    int fd = open("/proc/self/fd", O_PATH | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return ghr_errno(GHR_PROCFD_PROCPATHFDOPEN);
    *procfd = (gh_procfd){ .fd = fd, .alloc = alloc };

    return GHR_OK;

}

#define GH_PROCFD_GETFDPATH_INITIALBUFFERSIZE 128
#define GH_PROCFD_GETFDPATH_BUFFERSIZEINCREMENT 128
#define GH_PROCFD_FDFILENAMESIZE sizeof("XXXX")

static gh_result procfd_getfdfilename(gh_procfd * procfd, gh_pathfd fd, char * out_buffer) {
    (void)procfd;

    if (fd.fd < 0) return ghr_errnoval(GHR_PROCFD_INVALIDFD, EBADFD);
    if ((unsigned int)fd.fd > GH_PROCFD_FDFILENAMEMAXFD) return GHR_PROCFD_BIGFDPATH;

    int snprintf_result = snprintf(
        out_buffer, GH_PROCFD_FDFILENAMESIZE,
        "%d", fd.fd
    );

    if (snprintf_result < 0) return ghr_errno(GHR_PROCFD_FDPATHCONSTRUCT);
    return GHR_OK;
}

gh_result gh_procfd_fdpathctor(gh_procfd * procfd, gh_pathfd fd, gh_abscanonicalpath * out_path) {
    gh_result inner_res = GHR_OK;

    if (fd.fd < 0) return ghr_errnoval(GHR_PROCFD_INVALIDFD, EBADFD);
    if ((unsigned int)fd.fd > GH_PROCFD_FDFILENAMEMAXFD) return GHR_PROCFD_BIGFDPATH;

    char fd_filename[GH_PROCFD_FDFILENAMESIZE];
    gh_result res = procfd_getfdfilename(procfd, fd, fd_filename);
    if (ghr_iserr(res)) return res;

    char * buffer = NULL;
    size_t buffer_size = GH_PROCFD_GETFDPATH_INITIALBUFFERSIZE;
    res = gh_alloc_new(procfd->alloc, (void**)&buffer, buffer_size);
    if (ghr_iserr(res)) return res;
    memset(buffer, 0, buffer_size);

    ssize_t readlinkat_res = 0;
    size_t len = 0;
    do {
        readlinkat_res = readlinkat(procfd->fd, fd_filename, buffer, buffer_size);
        if (readlinkat_res < 0) {
            res = ghr_errno(GHR_PROCFD_READLINK);
            goto fail_readlinkat;
        }

        if ((size_t)readlinkat_res < buffer_size) {
            buffer[readlinkat_res + 1] = '\0';
            len = (size_t)readlinkat_res;
            break;
        }

        size_t new_buffer_size = buffer_size + GH_PROCFD_GETFDPATH_BUFFERSIZEINCREMENT;
        if (new_buffer_size > SSIZE_MAX) {
            res = GHR_PROCFD_LARGELINK;
            goto fail_readlinkat;
        }

        res = gh_alloc_resize(procfd->alloc, (void**)&buffer, buffer_size, new_buffer_size);
        if (ghr_iserr(res)) goto fail_resize;

        buffer_size = new_buffer_size;
        memset(buffer, 0, buffer_size);
    } while (false);

    size_t slash = 1;
    if (len == 1 && buffer[0] == '/') slash = 0;

    struct stat statbuf;
    if (fstat(fd.fd, &statbuf) >= 0) {
        if (statbuf.st_nlink == 0) {
            // Linux will add ' (deleted)' suffix, remove it
            static const char * suffix = " (deleted)";
            const size_t suffix_len = strlen(suffix);

            if (len >= suffix_len && strncmp(buffer + len - suffix_len, suffix, suffix_len) == 0) {
                len -= suffix_len;
            }
        }
    }

    if (fd.trailing_name[0] != '\0') {
        size_t trailing_name_len = strlen(fd.trailing_name);
        size_t len_with_trailing = len + slash + trailing_name_len;
        if (len_with_trailing + 1 > buffer_size) {
            size_t new_buffer_size = len_with_trailing + 1;
            res = gh_alloc_resize(procfd->alloc, (void**)&buffer, buffer_size, new_buffer_size);
            if (ghr_iserr(res)) goto fail_resize;
            buffer_size = new_buffer_size;
        }

        if (slash) {
            buffer[len] = '/';
        }
        strcpy(buffer + len + slash, fd.trailing_name);
        buffer[len_with_trailing] = '\0';
        len = len_with_trailing;
    }

    goto success;

fail_resize:
fail_readlinkat:
    inner_res = gh_alloc_delete(procfd->alloc, (void**)&buffer, buffer_size);
    if (ghr_iserr(inner_res)) res = inner_res;
    
success:
    if (out_path != NULL) {
        out_path->ptr = buffer;
        out_path->len = len;
    }

    return res;
}

gh_result gh_procfd_fdpathdtor(gh_procfd * procfd, gh_abscanonicalpath * path) {
    return gh_alloc_delete(procfd->alloc, (void**)(void*)&path->ptr, path->len + 1);
}

gh_result gh_procfd_reopen(gh_procfd * procfd, gh_pathfd fd, int flags, mode_t create_mode, int * out_fd) {
    gh_result res = GHR_OK;

    char fdfilename[GH_PROCFD_FDFILENAMESIZE];
    res = procfd_getfdfilename(procfd, fd, fdfilename);
    if (ghr_iserr(res)) return res;

    int new_fd = -1;

    if (gh_pathfd_guaranteedtoexist(fd)) {
        new_fd = openat(procfd->fd, fdfilename, flags, create_mode);
        if (new_fd < 0) return ghr_errno(GHR_PROCFD_REOPENFAIL);
    } else {
        new_fd = openat(fd.fd, fd.trailing_name, flags, create_mode);
        if (new_fd < 0) return ghr_errno(GHR_PROCFD_REOPENFAIL);
    }

    *out_fd = new_fd;
    return GHR_OK;
}

gh_result gh_procfd_dtor(gh_procfd * procfd) {
    if (close(procfd->fd) < 0) return ghr_errno(GHR_PROCFD_PROCPATHFDCLOSE);
    return GHR_OK;

}
