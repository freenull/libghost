#define _GNU_SOURCE
#include <libgen.h>
#include <fcntl.h>
#include <string.h>
#include <ghost/perms/pathfd.h>
#include <ghost/result.h>

gh_result gh_pathfd_opentrailing(int dirfd, const char * path, gh_pathfd * out_pathfd) {
    size_t path_len = strlen(path);

    char basename_buf[path_len + 1];
    strncpy(basename_buf, path, path_len + 1);
    basename_buf[path_len] = '\0';
    char * basename_str = basename(basename_buf);

    if (strncmp(basename_str, ".", sizeof(".")) == 0) {
        return ghr_errnoval(GHR_PATHFD_OPENFAIL, ENOENT);
    }

    if (strncmp(basename_str, "..", sizeof("..")) == 0) {
        return ghr_errnoval(GHR_PATHFD_OPENFAIL, ENOENT);
    }

    if (strncmp(basename_str, "/", sizeof("/")) == 0) {
        return ghr_errnoval(GHR_PATHFD_OPENFAIL, ENOENT);
    }

    char dirname_buf[path_len + 1];
    strncpy(dirname_buf, path, path_len + 1);
    dirname_buf[path_len] = '\0';
    char * dirname_str = dirname(dirname_buf);

    int opath_fd = openat(dirfd, dirname_str, O_PATH | O_DIRECTORY);

    if (opath_fd < 0) return ghr_errno(GHR_PATHFD_OPENFAIL);

    size_t basename_len = strlen(basename_str);
    if (basename_len > GH_PATHFD_TRAILINGNAMEMAX - 1) {
        return GHR_PATHFD_LARGETRAILINGNAME;
    }

    strcpy(out_pathfd->trailing_name, basename_str);
    out_pathfd->fd = opath_fd;
    out_pathfd->trailing_name[basename_len] = '\0';

    return GHR_OK;
}

gh_result gh_pathfd_open(int dirfd, const char * path, gh_pathfd * out_pathfd) {
    memset(out_pathfd->trailing_name, 0, GH_PATHFD_TRAILINGNAMEMAX * sizeof(char));

    int opath_fd = openat(dirfd, path, O_PATH, 0);
    if (opath_fd < 0) {
        if (errno == ENOENT) {
            return gh_pathfd_opentrailing(dirfd, path, out_pathfd);
        } else {
            return ghr_errno(GHR_PATHFD_OPENFAIL);
        }
    }

    out_pathfd->fd = opath_fd;
    return GHR_OK;
}

bool gh_pathfd_exists(gh_pathfd pathfd) {
    return pathfd.trailing_name[0] == '\0';
}

gh_result gh_pathfd_close(gh_pathfd pathfd) {
    if (close(pathfd.fd) < 0) return ghr_errno(GHR_PATHFD_CLOSEFAIL);
    return GHR_OK;
}

