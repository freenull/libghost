#define _GNU_SOURCE
#include <fcntl.h>
#include <string.h>
#include <ghost/perms/writer.h>

gh_permwriter gh_permwriter_new(int fd) {
    return (gh_permwriter) { .fd = fd, .indent = 0, .layer = GH_PERMWRITER_GROUPRESOURCE };
}

static void permwriter_printindent(gh_permwriter * writer) {
    for (int i = 0; i < writer->indent; i++) {
        write(writer->fd, "    ", 4);
    }
}

void gh_permwriter_beginresource(gh_permwriter * writer, const char * group, const char * resource) {
    size_t group_len = strlen(group);
    size_t resource_len = strlen(resource);

    permwriter_printindent(writer);

    write(writer->fd, group, group_len);
    write(writer->fd, " ", 1);
    write(writer->fd, resource, resource_len);
    write(writer->fd, " {\n", 3);

    writer->indent += 1;
    writer->layer = GH_PERMWRITER_ENTRY;
}

void gh_permwriter_endresource(gh_permwriter * writer) {
    if (writer->indent > 0) writer->indent -= 1;

    permwriter_printindent(writer);

    write(writer->fd, "}\n", 2);
}

static void permwriter_writeescaped(gh_permwriter * writer, const char * s, size_t s_len) {
    for (size_t i = 0; i < s_len; i++) {
        write(writer->fd, s + i, 1);
    }
}

void gh_permwriter_beginentry(gh_permwriter * writer, const char * entry, size_t entry_len) {
    permwriter_printindent(writer);

    write(writer->fd, "\"", 1);
    permwriter_writeescaped(writer, entry, entry_len);
    write(writer->fd, "\" {\n", 4);

    writer->indent += 1;
    writer->layer = GH_PERMWRITER_ENTRY;
}

void gh_permwriter_endentry(gh_permwriter * writer) {
    if (writer->layer != GH_PERMWRITER_ENTRY) {
        write(writer->fd, "\n", 1);
    }

    writer->layer = GH_PERMWRITER_GROUPRESOURCE;

    if (writer->indent > 0) writer->indent -= 1;
    permwriter_printindent(writer);
    write(writer->fd, "}\n", 2);
}

void gh_permwriter_field(gh_permwriter * writer, const char * key) {
    if (writer->layer != GH_PERMWRITER_ENTRY) write(writer->fd, "\n", 1);
    permwriter_printindent(writer);
    write(writer->fd, key, strlen(key));
    writer->layer = GH_PERMWRITER_FIELDARGS;
}

void gh_permwriter_fieldargstring(gh_permwriter * writer, const char * value, size_t value_len) {
    if (writer->layer == GH_PERMWRITER_FIELDARGS) {
        write(writer->fd, " ", 1);
    }
    writer->layer = GH_PERMWRITER_FIELDARGS;

    write(writer->fd, "\"", 1);
    permwriter_writeescaped(writer, value, value_len);
    write(writer->fd, "\"", 1);
}

void gh_permwriter_fieldargident(gh_permwriter * writer, const char * value, size_t value_len) {
    if (writer->layer == GH_PERMWRITER_FIELDARGS) {
        write(writer->fd, " ", 1);
    }
    writer->layer = GH_PERMWRITER_FIELDARGS;

    write(writer->fd, value, value_len);
}
