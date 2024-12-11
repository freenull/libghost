/** @defgroup prompt Permission prompter
 *
 * @brief Generic interface for a gadget that asks the user to accept or reject permission requests.
 *
 * @{
 */

#ifndef GHOST_PERMS_PROMPT_H
#define GHOST_PERMS_PROMPT_H

#include <stddef.h>
#include <stdbool.h>
#include <ghost/result.h>
#include <ghost/perms/request.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Enumeration of all possible responses of a prompter. */
typedef enum {
    /** @brief The user accepted the request, but only once. */
    GH_PERMRESPONSE_ACCEPT,
    /** @brief The user rejected the request, but only once. */
    GH_PERMRESPONSE_REJECT,
    /** @brief The user accepted the request. The policy should be changed to
     *         accept identical requests from this point onwards.
     */
    GH_PERMRESPONSE_ACCEPTREMEMBER,
    /** @brief The user rejected the request. The policy should be changed to
     *         reject identical requests from this point onwards.
     */
    GH_PERMRESPONSE_REJECTREMEMBER,
} gh_permresponse;

/** @brief Prompter implementation callback.
 *
 * @param req       Request data. See @ref gh_permrequest.
 * @param userdata  Arbitrary userdata provided in @ref gh_permprompter.
 * @param[out] out_response Shall be set to the prompter's response.
 *
 * @return Arbitrary result code. @ref GHR_OK indicate success. @n
 *         A result other than @ref GHR_OK implicitly indicates a response of
 *         @ref GH_PERMRESPONSE_REJECT. There is no need to set @p out_response
 *         in that case. @n
 *         A pair of special result codes @ref GHR_PERMPROMPT_UNSUPPORTEDGROUP and
 *         @ref GHR_PERMPROMPT_UNSUPPORTEDRESOURCE indicate that the prompter does
 *         not support a particular group or resource type. This should be avoided,
 *         however returning these error codes will involve the fallback system.
 */
typedef gh_result gh_permprompter_func(const gh_permrequest * req, void * userdata, gh_permresponse * out_response);

typedef struct gh_permprompter gh_permprompter;

/** @brief Permission prompter. */
struct gh_permprompter {
    /** @brief Callback. See @ref gh_permprompter_func */
    gh_permprompter_func * func;

    /** @brief Fallback prompter. May be set to `NULL`.
     *
     * If the main callback indicates a lack of support for a particular
     * permission group or resource type, handling of the request is attempted
     * using the prompter specified by this field.
     */
    gh_permprompter * fallback;
    
    /** @brief Arbitrary userdata enabling stateful prompters. */
    void * userdata;
};

/** @brief Create a new prompter.
 *
 * @param func     Callback. See: @ref gh_permprompter_func.
 * @param userdata Arbitrary userdata.
 *
 * @return New permission prompter without fallback.
 */
gh_permprompter gh_permprompter_new(gh_permprompter_func * func, void * userdata);

/** @brief Execute a permission request using a prompter.
 *
 * @param prompter     Permission prompter to use.
 * @param req          Request data. See: @ref gh_permrequest.
 * @param[out] out_response Will hold the response. See: @ref gh_permresponse.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permprompter_request(gh_permprompter * prompter, const gh_permrequest * req, gh_permresponse * out_response);

#define GH_PERMPROMPTER_STDERRFLAG (((uintptr_t)1 << ((sizeof(uintptr_t) * 8) - 1))) 

/** @brief Create a new simpletui prompter.
 *
 * @note This prompter uses the terminal standard output
 *       stream to display an interactive prompt.
 *
 * @param fd File descriptor to use as the input. You may pass `STDIN_FILENO`
 *           here to use standard input.
 *
 * @return New permission prompter without fallback.
 */
gh_permprompter gh_permprompter_simpletui(int fd);

/** @brief Create a new simpletui prompter with output to standard error.
 *
 * @note This prompter uses the terminal standard error
 *       stream to display an interactive prompt.
 *
 * @param fd File descriptor to use as the input. You may pass `STDIN_FILENO`
 *           here to use standard input.
 *
 * @return New permission prompter without fallback.
 */
gh_permprompter gh_permprompter_simpletui_stderr(int fd);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
