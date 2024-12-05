#include <assert.h>
#include <string.h>
#include <ghost/strings.h>

gh_conststr gh_conststr_fromc(const char * buf, size_t size) {
    return (gh_conststr) { .buffer = buf, .size = size };
}
gh_conststr gh_conststr_fromz(const char * buf) {
    return (gh_conststr) { .buffer = buf, .size = strlen(buf) };
}

gh_str gh_str_fromc(char * buf, size_t size, size_t capacity) {
    return (gh_str) {
        .buffer = buf,
        .size = size,
        .capacity = capacity
    };
}

gh_str gh_str_fromz(char * buf, size_t capacity) {
    size_t size = strlen(buf);
    if (size > capacity) size = capacity;
    return gh_str_fromc(buf, size, capacity);
}

bool gh_str_endswiths(gh_str str, gh_str s) {
    if (s.size > str.size) return false;
    return strncmp(str.buffer + (str.size - s.size), s.buffer, s.size) == 0;
}

bool gh_str_endswithc(gh_str str, const char * s, size_t size) {
    if (size > str.size) return false;
    return strncmp(str.buffer + (str.size - size), s, size) == 0;
}

bool gh_str_endswithz(gh_str str, const char * s) {
    return gh_str_endswithc(str, s, strlen(s));
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

bool gh_str_insertnull(gh_str * str) {
    if (str->size >= str->capacity) return false;

    str->buffer[str->size] = '\0';
    str->size += 1;
    return true;
}

bool gh_str_rpos(gh_str str, char c, size_t * out_pos) {
    for (size_t i = 0; i < str.size; i++) {
        size_t j = str.size - i - 1;

        if (str.buffer[j] == c) {
            *out_pos = j;
            return true;
        }
    }

    return false;
}

bool gh_str_appendchar(gh_str * str, char c) {
    size_t remaining = str->capacity - str->size;
    if (remaining < 1) return false;

    str->buffer[str->size] = c;
    str->size += 1;

    return true;
}

bool gh_str_appendc(gh_str * str, const char * s, size_t size, bool allow_trunc) {
    size_t remaining = str->capacity - str->size;
    bool trunc = false;
    if (remaining < size) {
        size = remaining;

        if (allow_trunc) trunc = true;
        else return false;
    }

    strncpy(str->buffer + str->size, s, size);
    str->size += size;
    return !trunc;
}

bool gh_str_appendz(gh_str * str, const char * s, bool allow_trunc) {
    return gh_str_appendc(str, s, strlen(s), allow_trunc);
}
