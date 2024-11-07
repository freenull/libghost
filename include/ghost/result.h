#ifndef GHOST_RESULT_H
#define GHOST_RESULT_H

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <ghost/generated/gh_error.h>

/** @brief Result/error type for libghost API.
 *
 * @par The result type includes 16 higher bits of optional errno and 16 lower bits of libghost context.
 *      Success is represented by 0 (GHR_OK).
 *
 * @par Since the standard does not enforce a maximum errno value, errno equal to or above the uint16_t
 *      max value is stored as `(uint16_t)0xFFFF` (representing "errno beyond storable range").
 *      Errno values are always positive as per the ISO C standard.
 *
 * @par @ref GHR_JAIL_NONZEROEXIT and @ref GHR_JAIL_KILLEDSIG are special cases.
 *      The 16 higher bits of the former contain the exit code, **not** the errno.
 *      The 16 higher bits of the latter contain the signal number, **not** the errno.
 *      Use @ref ghr_exitcode and @ref ghr_signalno respectively to extract the attached data.
 *
 * @par A result may include only the libghost context.
 *      A result *shall not* consist of just the errno value.
 *      The latter is theoretically possible, but will produce confusing error messages.
 */
typedef uint32_t gh_result;

#define _ghr_normalizeerrno(value) ((value >= 0xFFFF) ? 0xFFFF : (value))
#define ghr_errno(ctx) ((gh_result)((_ghr_normalizeerrno(errno) << 16) | (ctx)))
#define ghr_errnoval(ctx, value) ((gh_result)((_ghr_normalizeerrno(value) << 16) | (ctx)))

#define ghr_frag_errno(value) ((int)(((value) >> 16) & 0xFFFF))
#define ghr_frag_context(value) ((gh_error)((value) & 0xFFFF))

#define ghr_isok(value) ((value) == GHR_OK)
#define ghr_iserr(value) ((value) != GHR_OK)

#define ghr_fail(value) \
    do { \
        fprintf(stderr, "[%s:%d] Fatal error: ", __FILE__, __LINE__); \
        ghr_fputs(stderr, value); \
        exit(3); \
    } while (0)

#define ghr_assert(value) \
    do { \
        gh_result ghr_assert_value__generated = (value); \
        if (ghr_assert_value__generated != GHR_OK) { \
            fprintf(stderr, "[%s:%d] Assertion failed (error result): '" #value "': ", __FILE__, __LINE__); \
            ghr_fputs(stderr, ghr_assert_value__generated); \
            exit(3); \
        } \
    } while (0)

#define ghr_context_has_exitcode(ctx)((ctx) == GHR_JAIL_NONZEROEXIT)
#define ghr_context_has_signalno(ctx)((ctx) == GHR_JAIL_KILLEDSIG)

#define ghr_exitcode(value) ({ \
        gh_result ghr_result_value__generated = (value); \
        \
        (ghr_context_has_exitcode(ghr_frag_context(ghr_result_value__generated))) \
        ? ghr_frag_errno(value) \
        : -1; \
    })

#define ghr_signalno(value) ({ \
        gh_result ghr_result_value__generated = (value); \
        \
        (ghr_context_has_signalno(ghr_frag_context(ghr_result_value__generated))) \
        ? ghr_frag_errno(value) \
        : -1; \
    })

#define ghr_is(value, ctx) (ghr_frag_context(value) == (ctx))

const char * ghr_context(gh_result value);
const char * ghr_error(gh_result value);
void ghr_fprintf(FILE * file, gh_result value);
void ghr_fputs(FILE * file, gh_result value);

#endif
