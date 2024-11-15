#include <assert.h>
#include <string.h>
#include <ghost/strings.h>

gh_str gh_str_from(char * buf, size_t size, size_t capacity) {
    return (gh_str) {
        .buffer = buf,
        .size = size,
        .capacity = capacity
    };
}

gh_str gh_str_fromc(char * buf, size_t capacity) {
    size_t size = strlen(buf);
    if (size > capacity) size = capacity;
    return gh_str_from(buf, size, capacity);
}

bool gh_str_endswith(gh_str * str, const char * s, size_t size) {
    if (size > str->size) return false;
    return strncmp(str->buffer + (str->size - size), s, size) == 0;
}

bool gh_str_endswithc(gh_str * str, const char * s) {
    return gh_str_endswith(str, s, strlen(s));
}

void gh_str_trimrightn(gh_str * str, size_t n) {
    if (str->size >= n) str->size -= n;
}

void gh_str_trimrightchar(gh_str * str, char c) {
    size_t initial_size = str->size;
    for (size_t i = 0; i < initial_size; i++) {
        size_t j = initial_size - i - 1;

        if (str->buffer[j] == c) str->size -= 1;
    }
}

void gh_str_insertnull(gh_str * str) {
    assert(str->size < str->capacity);

    str->buffer[str->size] = '\0';
    str->size += 1;
}

bool gh_str_rpos(gh_str * str, char c, size_t * out_pos) {
    for (size_t i = 0; i < str->size; i++) {
        size_t j = str->size - i - 1;

        if (str->buffer[j] == c) {
            *out_pos = j;
            return true;
        }
    }

    return false;
}
