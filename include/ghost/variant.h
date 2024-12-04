/** @defgroup variant Variant
 *
 * @brief Subset of the Lua object model for interoperation between libghost threads and corresponding subjail processes.
 *
 * @{
 */


#ifndef GHOST_VARIANT_H
#define GHOST_VARIANT_H

#include <ghost/result.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GH_VARIANT_NIL,
    GH_VARIANT_INT,
    GH_VARIANT_DOUBLE,
    GH_VARIANT_STRING,
} gh_variant_type;

typedef struct {
    gh_variant_type type;
    union {
        int t_int;
        double t_double;
        size_t t_string_len;
    };
    char t_string_data[];
} gh_variant;

__attribute__((always_inline))
static inline size_t gh_variant_size(gh_variant * var) {
    if (var->type == GH_VARIANT_STRING) {
        return sizeof(gh_variant) + var->t_string_len;
    } else {
        return sizeof(gh_variant);
    }
}

#ifdef __cplusplus
}
#endif

#endif

/** @} */
