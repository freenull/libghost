#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/mman.h>
#include <ghost/fdmem.h>

static gh_result ipcfdmem_ctorfdo(gh_fdmem * fdmem, int fd, int prot, size_t size, size_t occupied) {
    void * map = mmap(NULL, size, prot, (prot & PROT_WRITE) ? MAP_SHARED : MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) return ghr_errno(GHR_IPCFDMEM_MAPFAIL);

    fdmem->fd = fd;
    fdmem->data = map;
    fdmem->size = size;
    fdmem->occupied = occupied;

    return GHR_OK;
}

gh_result gh_fdmem_ctorfdo(gh_fdmem * fdmem, int fd, size_t occupied) {
    off_t offs = lseek(fd, 0, SEEK_END);
    if (offs < 0) return ghr_errno(GHR_IPCFDMEM_GETLEN);

    size_t size = (size_t)offs;
    return ipcfdmem_ctorfdo(fdmem, fd, PROT_READ | PROT_WRITE, size, occupied);
}

gh_result gh_fdmem_ctorfd(gh_fdmem * fdmem, int fd) {
    off_t offs = lseek(fd, 0, SEEK_END);
    if (offs < 0) return ghr_errno(GHR_IPCFDMEM_GETLEN);

    size_t size = (size_t)offs;
    return ipcfdmem_ctorfdo(fdmem, fd, PROT_READ | PROT_WRITE, size, size);
}

gh_result gh_fdmem_ctor(gh_fdmem * fdmem) {
    int fd = memfd_create("ipcfdmem", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return ghr_errno(GHR_IPCFDMEM_OPENMEMFD);

    if (ftruncate(fd, GH_IPCFDMEM_INITIALCAPACITY) < 0) {
        return ghr_errno(GHR_IPCFDMEM_TRUNCATE);
    }

    return ipcfdmem_ctorfdo(fdmem, fd, PROT_READ | PROT_WRITE, GH_IPCFDMEM_INITIALCAPACITY, 0);
}

static gh_result ipcfdmem_resize(gh_fdmem * fdmem, size_t new_size) {
    if (ftruncate(fdmem->fd, (off_t)new_size) < 0) {
        return ghr_errno(GHR_IPCFDMEM_TRUNCATE);
    }

    void * map = mremap(fdmem->data, fdmem->size, new_size, MREMAP_MAYMOVE);
    if (map == MAP_FAILED) {
        return ghr_errno(GHR_IPCFDMEM_REMAPFAIL);
    }

    fdmem->size = new_size;
    return GHR_OK;
}

gh_result gh_fdmem_new(gh_fdmem * fdmem, size_t size, void ** out_ptr) {
    if (fdmem->occupied + size > fdmem->size) {
        size_t new_size = fdmem->size;
        while (fdmem->occupied + size > new_size) {
            new_size *= 2;
        }

        gh_result res = ipcfdmem_resize(fdmem, new_size);
        if (ghr_iserr(res)) return res;
    }

    void * ptr = (char*)fdmem->data + fdmem->occupied;
    fdmem->occupied += size;
    *out_ptr = ptr;

    return GHR_OK;
}

static void * ipcfdmem_realptr(gh_fdmem * fdmem, gh_fdmem_ptr ptr) {
    if (ptr == 0) return NULL;
    ptr -= 1;
    if (ptr >= fdmem->occupied) return NULL;
    return (char*)fdmem->data + ptr;
}

void * gh_fdmem_realptr(gh_fdmem * fdmem, gh_fdmem_ptr ptr, size_t size) {
    if (size == 0) return NULL;
    if (ipcfdmem_realptr(fdmem, ptr + size - 1) == NULL) return NULL;
    return ipcfdmem_realptr(fdmem, ptr);
}

static gh_fdmem_ptr ipcfdmem_virtptr(gh_fdmem * fdmem, void * ptr) {
    if (ptr < fdmem->data || ptr >= (void*)((char*)fdmem->data + fdmem->occupied)) return 0;
    return (gh_fdmem_ptr)((char*)ptr - (char*)fdmem->data) + 1;
}

gh_fdmem_ptr gh_fdmem_virtptr(gh_fdmem * fdmem, void * ptr, size_t size) {
    if (size == 0) return 0;
    if (ipcfdmem_virtptr(fdmem, (char*)ptr + size - 1) == 0) return 0;
    return ipcfdmem_virtptr(fdmem, ptr);
}

gh_result gh_fdmem_dtor(gh_fdmem * fdmem) {
    if (munmap(fdmem->data, fdmem->size) < 0) {
        return ghr_errno(GHR_IPCFDMEM_UNMAPFAIL);
    }

    if (close(fdmem->fd) < 0) {
        return ghr_errno(GHR_IPCFDMEM_CLOSE);
    }

    return GHR_OK;
}

gh_result gh_fdmem_sync(gh_fdmem * fdmem) {
    if (munmap(fdmem->data, fdmem->size) < 0) {
        return ghr_errno(GHR_IPCFDMEM_UNMAPFAIL);
    }
    
    return gh_fdmem_ctorfd(fdmem, fdmem->fd);
}

gh_result gh_fdmem_seal(gh_fdmem * fdmem) {
    if (munmap(fdmem->data, fdmem->size) < 0) {
        return ghr_errno(GHR_IPCFDMEM_UNMAPFAIL);
    }

    int fcntl_res = fcntl(fdmem->fd, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE);
    if (fcntl_res < 0) return ghr_errno(GHR_IPCFDMEM_SEAL);

    gh_result res = ipcfdmem_ctorfdo(fdmem, fdmem->fd, PROT_READ, fdmem->size, fdmem->occupied);
    if (ghr_iserr(res)) return res;
    return GHR_OK;
}


