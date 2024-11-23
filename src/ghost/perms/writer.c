#define _GNU_SOURCE
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <ghost/perms/writer.h>

gh_permwriter gh_permwriter_new(int fd) {
    return (gh_permwriter) { .fd = fd, .indent = 0, .layer = GH_PERMWRITER_GROUPRESOURCE };
}

#define GH_PERMWRITER_WRITEORRETURNERR(fd, buf, size) do { \
        ssize_t write_res = write((fd), (buf), (size)); \
        if (write_res < 0) return ghr_errno(GHR_PERMWRITER_WRITEFAILED); \
        if ((size_t)write_res != (size)) return GHR_PERMWRITER_WRITETRUNCATED; \
    } while (0)

static gh_result permwriter_printindent(gh_permwriter * writer) {
    for (int i = 0; i < writer->indent; i++) {
        GH_PERMWRITER_WRITEORRETURNERR(writer->fd, "    ", 4);
    }

    return GHR_OK;
}

gh_result gh_permwriter_beginresource(gh_permwriter * writer, const char * group, const char * resource) {
    size_t group_len = strlen(group);
    size_t resource_len = strlen(resource);

    permwriter_printindent(writer);

    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, group, group_len);
    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, " ", 1);
    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, resource, resource_len);
    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, " {\n", 3);

    writer->indent += 1;
    writer->layer = GH_PERMWRITER_ENTRY;

    return GHR_OK;
}

gh_result gh_permwriter_endresource(gh_permwriter * writer) {
    if (writer->indent > 0) writer->indent -= 1;

    permwriter_printindent(writer);

    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, "}\n", 2);

    return GHR_OK;
}

static gh_result permwriter_writeescaped(gh_permwriter * writer, const char * s, size_t s_len) {
    for (size_t i = 0; i < s_len; i++) {
        GH_PERMWRITER_WRITEORRETURNERR(writer->fd, s + i, 1);
    }

    return GHR_OK;
}

gh_result gh_permwriter_beginentry(gh_permwriter * writer, const char * entry, size_t entry_len) {
    permwriter_printindent(writer);

    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, "\"", 1);
    permwriter_writeescaped(writer, entry, entry_len);
    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, "\" {\n", 4);

    writer->indent += 1;
    writer->layer = GH_PERMWRITER_ENTRY;

    return GHR_OK;
}

gh_result gh_permwriter_endentry(gh_permwriter * writer) {
    if (writer->layer != GH_PERMWRITER_ENTRY) {
        GH_PERMWRITER_WRITEORRETURNERR(writer->fd, "\n", 1);
    }

    writer->layer = GH_PERMWRITER_GROUPRESOURCE;

    if (writer->indent > 0) writer->indent -= 1;
    permwriter_printindent(writer);
    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, "}\n", 2);

    return GHR_OK;
}

gh_result gh_permwriter_field(gh_permwriter * writer, const char * key) {
    if (writer->layer != GH_PERMWRITER_ENTRY) GH_PERMWRITER_WRITEORRETURNERR(writer->fd, "\n", 1);
    permwriter_printindent(writer);
    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, key, strlen(key));
    writer->layer = GH_PERMWRITER_FIELDARGS;

    return GHR_OK;
}

gh_result gh_permwriter_fieldargstring(gh_permwriter * writer, const char * value, size_t value_len) {
    if (writer->layer == GH_PERMWRITER_FIELDARGS) {
        GH_PERMWRITER_WRITEORRETURNERR(writer->fd, " ", 1);
    }
    writer->layer = GH_PERMWRITER_FIELDARGS;

    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, "\"", 1);
    permwriter_writeescaped(writer, value, value_len);
    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, "\"", 1);

    return GHR_OK;
}

gh_result gh_permwriter_fieldargident(gh_permwriter * writer, const char * value, size_t value_len) {
    if (writer->layer == GH_PERMWRITER_FIELDARGS) {
        GH_PERMWRITER_WRITEORRETURNERR(writer->fd, " ", 1);
    }
    writer->layer = GH_PERMWRITER_FIELDARGS;

    GH_PERMWRITER_WRITEORRETURNERR(writer->fd, value, value_len);

    return GHR_OK;
}
