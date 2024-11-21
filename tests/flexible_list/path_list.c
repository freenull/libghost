#include <ghost/alloc.h>
#include <ghost/variable_list.h>
#include <string.h>

typedef struct {
    bool deleted;
    size_t path_len;
    char path[];
} va_path;

int main(void) {
    gh_alloc alloc = gh_alloc_default();

    gh_flexiblelistoptions opts = {
        .initial_capacity_bytes = 1,
        .max_capacity_bytes = GH_FLEXIBLELIST_NOMAXCAPACITY,

        .element_header_size = sizeof(va_path),
        .element_va_size_field_type = GH_FLEXIBLELISTOPTIONS_SIZETSIZE,
        .element_va_size_field_offset = offsetof(va_path, path_len),
        .element_va_size_suffix = 1,

        .has_delete_flag = false,
        .delete_flag_field_offset = 0,

        .dtorelement_func = NULL,
        .userdata = NULL
    };

    int * buffer;
    size_t size_bytes;
    size_t capacity_bytes;

    gh_flexiblelist vl = {
        .alloc = &alloc,
        .buffer = (void**)&buffer,
        .size_bytes = &size_bytes,
        .capacity_bytes = &capacity_bytes
    };

    ghr_assert(gh_flexiblelist_ctor(vl, &opts));

    const char * path = "/tmp/foo.txt";
    va_path * vapath;
    ghr_assert(gh_flexiblelist_appendalloc(vl, &opts, strlen(path) + 1, (void**)&vapath));
    strcpy(vapath->path, path);
    vapath->deleted = false;

    va_path * local_path = malloc(sizeof(va_path) + sizeof("/tmp/foo.txt"));
    strcpy(local_path->path, path);
    local_path->path_len = strlen(path);
    local_path->deleted = false;

    ghr_assert(gh_flexiblelist_append(vl, &opts, local_path));
    ghr_assert(gh_flexiblelist_append(vl, &opts, local_path));
    ghr_assert(gh_flexiblelist_append(vl, &opts, local_path));

    free(local_path);

    local_path = malloc(sizeof(va_path) + sizeof("/tmp/longername.txt"));
    strcpy(local_path->path, "/tmp/longername.txt");
    local_path->path_len = strlen("/tmp/longername.txt");
    local_path->deleted = false;

    ghr_assert(gh_flexiblelist_append(vl, &opts, local_path));
    ghr_assert(gh_flexiblelist_append(vl, &opts, local_path));
    ghr_assert(gh_flexiblelist_append(vl, &opts, local_path));
    ghr_assert(gh_flexiblelist_append(vl, &opts, local_path));

    free(local_path);

    assert(*vl.size_bytes == 260);
    assert(*vl.capacity_bytes == 512);

    va_path * elem_idx3 = NULL;

    va_path * it = NULL;
    ghr_assert(gh_flexiblelist_next(vl, &opts, it, (void**)&it));
    size_t idx = 0;
    while(it != NULL) {
        printf("[%p, deleted: %s] %zu: %.*s\n", (void*)it, it->deleted ? "true " : "false", idx, (int)it->path_len, it->path);

        if (idx < 4) {
            assert(strncmp(it->path, "/tmp/foo.txt", it->path_len + 1) == 0);
        } else {
            assert(strncmp(it->path, "/tmp/longername.txt", it->path_len + 1) == 0);
        }

        if (idx == 3) elem_idx3 = it;

        gh_result res = gh_flexiblelist_next(vl, &opts, it, (void**)&it);
        if (ghr_is(res, GHR_FLEXIBLELIST_NONEXT)) {
            printf("end\n");
            res = GHR_OK;
        }
        ghr_assert(res);
        idx += 1;
    }

    assert(idx == 8);

    assert(elem_idx3 != NULL);
    ghr_asserterr(GHR_FLEXIBLELIST_NODELETEFLAG, gh_flexiblelist_remove(vl, &opts, elem_idx3));

    // adding deletion support to the flexiblelist
    opts.has_delete_flag = true;
    opts.delete_flag_field_offset = offsetof(va_path, deleted);

    ghr_assert(gh_flexiblelist_remove(vl, &opts, elem_idx3));

    idx = 0;
    it = NULL;
    ghr_assert(gh_flexiblelist_next(vl, &opts, NULL, (void**)&it));
    while(it != NULL) {
        printf("[%p, deleted: %s] %zu: %.*s\n", (void*)it, it->deleted ? "true " : "false", idx, (int)it->path_len, it->path);

        if (idx < 3) {
            assert(strncmp(it->path, "/tmp/foo.txt", it->path_len + 1) == 0);
        } else {
            assert(strncmp(it->path, "/tmp/longername.txt", it->path_len + 1) == 0);
        }

        gh_result res = gh_flexiblelist_next(vl, &opts, it, (void**)&it);
        if (ghr_is(res, GHR_FLEXIBLELIST_NONEXT)) {
            printf("end\n");
            res = GHR_OK;
        }
        ghr_assert(res);
        idx += 1;
    }

    assert(idx == 7);

    idx = 0;
    it = NULL;
    ghr_assert(gh_flexiblelist_nextwithdeleted(vl, &opts, NULL, (void**)&it));
    while(it != NULL) {
        printf("[%p, deleted: %s] %zu: %.*s\n", (void*)it, it->deleted ? "true " : "false", idx, (int)it->path_len, it->path);

        if (idx < 4) {
            assert(strncmp(it->path, "/tmp/foo.txt", it->path_len + 1) == 0);
        } else {
            assert(strncmp(it->path, "/tmp/longername.txt", it->path_len + 1) == 0);
        }

        if (idx == 3) {
            assert(it->deleted);
        }

        gh_result res = gh_flexiblelist_nextwithdeleted(vl, &opts, it, (void**)&it);
        if (ghr_is(res, GHR_FLEXIBLELIST_NONEXT)) {
            printf("end\n");
            res = GHR_OK;
        }
        ghr_assert(res);
        idx += 1;
    }

    assert(idx == 8);

    ghr_assert(gh_flexiblelist_dtor(vl, &opts));

    assert(*vl.capacity_bytes == 0);
    assert(*vl.size_bytes == 0);
    assert(*vl.buffer == NULL);
}
