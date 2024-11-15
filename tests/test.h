#define _GNU_SOURCE
#include <stdio.h>
#define assert(expr) do { \
        int assert__result = (expr); \
        if (!assert__result) { \
            fprintf(stderr, "Assertion failed: %s", # expr); \
            raise(SIGABRT); \
        } \
    } while(0)
