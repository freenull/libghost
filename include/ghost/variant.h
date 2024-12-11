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

/** @brief Variant type. */
typedef enum {
    /** @brief Lua `nil`. */
    GH_VARIANT_NIL,

    /** @brief Lua `number`, but limited to integers. */
    GH_VARIANT_INT,

    /** @brief Lua `number`. */
    GH_VARIANT_DOUBLE,

    /** @brief Lua `string`. */
    GH_VARIANT_STRING,
} gh_variant_type;

/** @brief Variant object. */
typedef struct {
    /** @brief Type of the variant. Determines valid union fields. */
    gh_variant_type type;
    union {
        /** @brief Integer value. */
        int t_int;
        /** @brief Double value. */
        double t_double;
        /** @brief Length of string. */
        size_t t_string_len;
    };
    /** @brief String data. */
    char t_string_data[];
} gh_variant;

/** @brief Retrieve true size of variant (including string data if applicable).
 *
 * @param var Pointer to variant.
 *
 * @return Size of variant.
 */
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
