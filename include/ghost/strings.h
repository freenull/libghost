#ifndef GHOST_STRINGS_H
#define GHOST_STRINGS_H

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    char * buffer;
    size_t size;
    size_t capacity;
} gh_str;

gh_str gh_str_from(char * buf, size_t size, size_t capacity);
gh_str gh_str_fromc(char * buf, size_t capacity);
bool gh_str_endswith(gh_str * str, const char * s, size_t size);
bool gh_str_endswithc(gh_str * str, const char * s);
void gh_str_trimrightn(gh_str * str, size_t n);
void gh_str_trimrightchar(gh_str * str, char c);
void gh_str_insertnull(gh_str * str);
bool gh_str_rpos(gh_str * str, char c, size_t * out_pos);
#define gh_str_endswithlit(str, lit) gh_str_endswith((str), (lit), sizeof(lit)/sizeof(char) - 1)

#endif
