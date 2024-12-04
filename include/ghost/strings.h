/** @defgroup strings Strings
 *
 * @brief Internal string operation helper functions and string fat pointer structure.
 *
 * @{
 */

#ifndef GHOST_STRINGS_H
#define GHOST_STRINGS_H

#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char * buffer;
    size_t size;
} gh_conststr;

typedef struct {
    char * buffer;
    size_t size;
    size_t capacity;
} gh_str;

#define GH_STR_FORMAT "%.*s"
#define GH_STR_ARG(s) (int)s.size, s.buffer

#define gh_conststr_fromlit(str) (gh_conststr_fromc((str), sizeof(str)/sizeof(char) - 1))
gh_conststr gh_conststr_fromc(const char * buf, size_t size);
gh_conststr gh_conststr_fromz(const char * buf);

__attribute__((always_inline))
static inline bool gh_conststr_eqlsrs(gh_conststr lhs, gh_conststr rhs) {
    return lhs.size == rhs.size && strncmp(lhs.buffer, rhs.buffer, lhs.size) == 0;
}

__attribute__((always_inline))
static inline bool gh_conststr_eqs(gh_conststr lhs, gh_conststr rhs) { return gh_conststr_eqlsrs(lhs, rhs); }

__attribute__((always_inline))
static inline bool gh_conststr_eqlcrs(const char * lhs, size_t lhs_size, gh_conststr rhs) {
    return lhs_size == rhs.size && strncmp(lhs, rhs.buffer, lhs_size) == 0;
}

__attribute__((always_inline))
static inline bool gh_conststr_eqlsrc(gh_conststr lhs, const char * rhs, size_t rhs_size) {
    return lhs.size == rhs_size && strncmp(lhs.buffer, rhs, lhs.size) == 0;
}

__attribute__((always_inline))
static inline bool gh_conststr_eqc(gh_conststr lhs, const char * rhs, size_t rhs_size) { return gh_conststr_eqlsrc(lhs, rhs, rhs_size); }

__attribute__((always_inline))
static inline bool gh_conststr_eqlcrc(const char * lhs, size_t lhs_size, const char * rhs, size_t rhs_size) {
    return lhs_size == rhs_size && strncmp(lhs, rhs, lhs_size) == 0;
}

__attribute__((always_inline))
static inline bool gh_conststr_eqlzrs(const char * lhs, gh_conststr rhs) {
    return strlen(lhs) == rhs.size && strncmp(lhs, rhs.buffer, rhs.size) == 0;
}

__attribute__((always_inline))
static inline bool gh_conststr_eqlsrz(gh_conststr lhs, const char * rhs) {
    return lhs.size == strlen(rhs) && strncmp(lhs.buffer, rhs, lhs.size) == 0;
}

__attribute__((always_inline))
static inline bool gh_conststr_eqz(gh_conststr lhs, const char * rhs) { return gh_conststr_eqlsrz(lhs, rhs); }

__attribute__((always_inline))
static inline bool gh_conststr_eqlzrc(const char * lhs, const char * rhs, size_t rhs_size) {
    return strlen(lhs) == rhs_size && strncmp(lhs, rhs, rhs_size) == 0;
}

__attribute__((always_inline))
static inline bool gh_conststr_eqlcrz(const char * lhs, size_t lhs_size, const char * rhs) {
    return lhs_size == strlen(rhs) && strncmp(lhs, rhs, lhs_size) == 0;
}

__attribute__((always_inline))
static inline bool gh_conststr_eqlzrz(const char * lhs, const char * rhs) {
    size_t lhs_size = strlen(lhs);
    return lhs_size == strlen(rhs) && strncmp(lhs, rhs, lhs_size) == 0;
}

__attribute__((always_inline))
static inline bool gh_conststr_fitsinc(gh_conststr s, size_t buffer_size) {
    return s.size <= buffer_size;
}

__attribute__((always_inline))
static inline bool gh_conststr_fitsinz(gh_conststr s, size_t buffer_size) {
    return s.size < buffer_size;
}

__attribute__((always_inline))
static inline bool gh_conststr_copyc(gh_conststr s, char * c, size_t c_size) {
    if (!gh_conststr_fitsinc(s, c_size)) return false;

    strncpy(c, s.buffer, s.size);
    return true;
}

__attribute__((always_inline))
static inline bool gh_conststr_copyz(gh_conststr s, char * c, size_t c_size) {
    if (!gh_conststr_fitsinz(s, c_size)) return false;

    strncpy(c, s.buffer, s.size);
    c[s.size] = '\0';
    return true;
}

#define gh_str_fromlit(str) (gh_str_fromc((str), sizeof(str)/sizeof(char) - 1, 0))
gh_str gh_str_fromc(char * buf, size_t size, size_t capacity);
gh_str gh_str_fromz(char * buf, size_t capacity);
bool gh_str_endswiths(gh_str str, gh_str s);
bool gh_str_endswithc(gh_str str, const char * s, size_t size);
bool gh_str_endswithz(gh_str str, const char * s);
void gh_str_trimrightn(gh_str * str, size_t n);
void gh_str_trimrightchar(gh_str * str, char c);
void gh_str_insertnull(gh_str * str);
bool gh_str_rpos(gh_str str, char c, size_t * out_pos);
bool gh_str_appendc(gh_str * str, const char * s, size_t size, bool allow_trunc);
bool gh_str_appendz(gh_str * str, const char * s, bool allow_trunc);
#define gh_str_endswithlit(str, lit) gh_str_endswith((str), (lit), sizeof(lit)/sizeof(char) - 1)

__attribute__((always_inline))
static inline bool gh_str_eqlsrs(gh_str lhs, gh_str rhs) {
    return lhs.size == rhs.size && strncmp(lhs.buffer, rhs.buffer, lhs.size) == 0;
}

__attribute__((always_inline))
static inline bool gh_str_eqs(gh_str lhs, gh_str rhs) { return gh_str_eqlsrs(lhs, rhs); }

__attribute__((always_inline))
static inline bool gh_str_eqlcrs(const char * lhs, size_t lhs_size, gh_str rhs) {
    return lhs_size == rhs.size && strncmp(lhs, rhs.buffer, lhs_size) == 0;
}

__attribute__((always_inline))
static inline bool gh_str_eqlsrc(gh_str lhs, const char * rhs, size_t rhs_size) {
    return lhs.size == rhs_size && strncmp(lhs.buffer, rhs, lhs.size) == 0;
}

__attribute__((always_inline))
static inline bool gh_str_eqc(gh_str lhs, const char * rhs, size_t rhs_size) { return gh_str_eqlsrc(lhs, rhs, rhs_size); }

__attribute__((always_inline))
static inline bool gh_str_eqlcrc(const char * lhs, size_t lhs_size, const char * rhs, size_t rhs_size) {
    return lhs_size == rhs_size && strncmp(lhs, rhs, lhs_size) == 0;
}

__attribute__((always_inline))
static inline bool gh_str_eqlzrs(const char * lhs, gh_str rhs) {
    return strlen(lhs) == rhs.size && strncmp(lhs, rhs.buffer, rhs.size) == 0;
}

__attribute__((always_inline))
static inline bool gh_str_eqlsrz(gh_str lhs, const char * rhs) {
    return lhs.size == strlen(rhs) && strncmp(lhs.buffer, rhs, lhs.size) == 0;
}

__attribute__((always_inline))
static inline bool gh_str_eqz(gh_str lhs, const char * rhs) { return gh_str_eqlsrz(lhs, rhs); }

__attribute__((always_inline))
static inline bool gh_str_eqlzrc(const char * lhs, const char * rhs, size_t rhs_size) {
    return strlen(lhs) == rhs_size && strncmp(lhs, rhs, rhs_size) == 0;
}

__attribute__((always_inline))
static inline bool gh_str_eqlcrz(const char * lhs, size_t lhs_size, const char * rhs) {
    return lhs_size == strlen(rhs) && strncmp(lhs, rhs, lhs_size) == 0;
}

__attribute__((always_inline))
static inline bool gh_str_eqlzrz(const char * lhs, const char * rhs) {
    size_t lhs_size = strlen(lhs);
    return lhs_size == strlen(rhs) && strncmp(lhs, rhs, lhs_size) == 0;
}

__attribute__((always_inline))
static inline bool gh_str_fitsinc(gh_str s, size_t buffer_size) {
    return s.size <= buffer_size;
}

__attribute__((always_inline))
static inline bool gh_str_fitsinz(gh_str s, size_t buffer_size) {
    return s.size < buffer_size;
}

__attribute__((always_inline))
static inline bool gh_str_copyc(gh_str s, char * c, size_t c_size) {
    if (!gh_str_fitsinc(s, c_size)) return false;

    strncpy(c, s.buffer, s.size);
    return true;
}

__attribute__((always_inline))
static inline bool gh_str_copyz(gh_str s, char * c, size_t c_size) {
    if (!gh_str_fitsinz(s, c_size)) return false;

    strncpy(c, s.buffer, s.size);
    c[s.size] = '\0';
    return true;
}

#ifdef __cplusplus
}
#endif

#endif

/** @} */
