/** @defgroup parser Permission parser
 * 
 * @brief Parser for the on-disk permission store format (GHPERM).
 *
 * @{
 */

#ifndef GHOST_PERMS_PARSER_H
#define GHOST_PERMS_PARSER_H

#include <stdio.h>
#include <string.h>
#include <ghost/alloc.h>
#include <ghost/byte_buffer.h>
#include <ghost/perms/request.h>
#include <ghost/strings.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief GHPERM token type. */
typedef enum {
    /** @brief Identifier, i.e. `foo` */
    GH_PERMTOKEN_IDENTIFIER,

    /** @brief String, i.e.  `"string with \"spaces and quotes\""` */
    GH_PERMTOKEN_STRING,

    /** @brief Left curly bracket - `{` */
    GH_PERMTOKEN_LBRACE,

    /** @brief Right curly bracket - `}` */
    GH_PERMTOKEN_RBRACE,

    /** @brief End of file */
    GH_PERMTOKEN_EOF
} gh_permtoken_type;

/** @brief User friendly identifier of the position of a character in a file. */
typedef struct {
    /** @brief Row; the first row is `1` */
    size_t row;

    /** @brief Column; the first column is `1`, but newline characters are considered to be in column `0` of the next line */
    size_t column;
} gh_permparser_loc;

/** @brief GHPERM token. */
typedef struct {
    /** @brief Type */
    gh_permtoken_type type;

    /** @brief Value */
    gh_conststr value;

    /** @brief Location of the start of the token */
    gh_permparser_loc loc;
} gh_permtoken;

typedef struct gh_permparser gh_permparser;

/** @brief Callback function type for a resource parser - `matches`.
 *
 * During parsing, the parser will iterate the list of registered resource parsers and call `matches` on each parser,
 * until one returns @ref GHR_OK.
 *
 * @param parser Parser instance.
 * @param group_id Null terminated string identifying the permission group.
 * @param resource_id Null terminated string identifying the permission resource.
 * @param userdata Arbitrary pointer provided by the user in @ref gh_permparser_registerresource.
 *
 * @return Must return @ref GHR_OK if the resource parser should be used to parse entries of this type. @n
 *         May return @ref GHR_PERMPARSER_NOMATCH specifically to indicate that the resource parser cannot handle this type. @n
 *         Any other result types will not be caught during parsing and will cause it to fail.
 */
typedef gh_result gh_permresourceparser_matches_func(gh_permparser * parser, gh_permrequest_id group_id, gh_permrequest_id resource_id, void * userdata);

/** @brief Callback function type for a resource parser - `new entry`.
 *
 * Once the resource parser has been selected, its `new entry` function will be called for each entry in the permission
 * store with the entry's key. The @p out_entry output parameter can be used to save an object that can then be read in the
 * @ref gh_permresourceparser_setfield_func callback.
 *
 * @param parser Parser instance.
 * @param key Key of the entry. For example, in the case of pathfs, @p key is the absolute file path.
 * @param userdata Arbitrary pointer provided by the user in @ref gh_permparser_registerresource.
 * @param[out] out_entry The location of this pointer may be made to hold an arbitrary pointer. The same pointer will be provided
 *                       through the `entry` parameter of @ref gh_permresourceparser_setfield_func.
 *
 * @return Any @ref gh_result.
 */
typedef gh_result gh_permresourceparser_newentry_func(gh_permparser * parser, gh_conststr key, void * userdata, void ** out_entry);

/** @brief Callback function type for a resource parser - `set field`
 *
 * Once the resource parser's `new entry` function has been called for an entry, the `set field` callback will be called for
 * each field inside the entry. The key is consumed and passed as the @p field parameter.
 * The callback is responsible for consuming tokens using the @p parser until what remains in the tokenizer queue is the
 * identifier token of the next field's key.
 *
 * @param parser Parser instance.
 * @param entry Arbitrary pointer provided by the user, the value of `*out_entry` after @ref gh_permresourceparser_newentry_func.
 * @param field Null terminated string identifying the permission entry field.
 * @param userdata Arbitrary pointer provided by the user in @ref gh_permparser_registerresource.
 *
 * @return Any @ref gh_result.
 */
typedef gh_result gh_permresourceparser_setfield_func(gh_permparser * parser, void * entry, gh_permrequest_id field, void * userdata);

/** @brief Generic permission resource parser. */
typedef struct {
    /** @brief Arbitrary pointer provided by the user. */
    void * userdata;

    /** @brief `matches` callback, see @ref gh_permresourceparser_matches_func. */
    gh_permresourceparser_matches_func * matches;

    /** @brief `new entry` callback, see @ref gh_permresourceparser_newentry_func. */
    gh_permresourceparser_newentry_func * newentry;

    /** @brief `set field` callback, see @ref gh_permresourceparser_setfield_func. */
    gh_permresourceparser_setfield_func * setfield;
} gh_permresourceparser;

/** @brief Structure representing a detailed parser error for use in diagnostics. */
typedef struct {
    /** @brief Location of the token that caused the error, or of the last token read before the error. */
    gh_permparser_loc loc;

    /** @brief Optionally a null terminated string literal describing the error in more details. */
    const char * detail;
} gh_permparser_error;

/** @brief Maximum amount of resource parsers attached to a permission parser. */
#define GH_PERMPARSER_MAXRESOURCEPARSERS 128

/** @brief Permission parser. */
struct gh_permparser {
    /** @brief Byte buffer used to store token values. */
    gh_bytebuffer buffer; // for token values

    /** @brief Buffer containing the entire GHPERM file. May be memory mapped. */
    const char * data; // input data
    
    /** @brief If true, @ref gh_permparser.data is a pointer to a block of memory returned by `mmap(2)`.  */
    bool is_mmapped;

    /** @brief Size of @ref gh_permparser.data. */
    size_t size;

    /** @brief Index of the current character. May be larger than @ref gh_permparser.size, which indicates that EOF has been reached. */
    size_t idx;

    /** @brief Current position of the parser within the file. */
    gh_permparser_loc loc;

    /** @brief Scratch space to store a peeked token. Do not access this field. Use @ref gh_permparser_peektoken */
    gh_permtoken peek_token;

    /** @brief Number of registered resource parsers. */
    size_t resource_parsers_count;

    /** @brief Array of resource parsers. */
    gh_permresourceparser resource_parsers[GH_PERMPARSER_MAXRESOURCEPARSERS];

    /** @brief Details of the last parser error. Only meaningful after an error return from a `gh_permparser_*` function. */
    gh_permparser_error error;
};

/** @brief Construct a permission parser instance from a buffer in memory.
 *
 * @param parser Parser instance.
 * @param alloc Instance of an allocator.
 * @param buffer Pointer to the start of the buffer.
 * @param size Size of the buffer.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permparser_ctorbuffer(gh_permparser * parser, gh_alloc * alloc, const char * buffer, size_t size);

/** @brief Construct a permission parser instance from the contents of a file
 *         descriptor.
 *
 * @param parser Parser instance.
 * @param alloc Instance of an allocator.
 * @param fd File descriptor.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permparser_ctorfd(gh_permparser * parser, gh_alloc * alloc, int fd);

/** @brief Destroy a permission parser instance.
 *
 * @param parser Parser instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permparser_dtor(gh_permparser * parser);

/** @brief Read the next token.
 *
 * @param parser Parser instance.
 * @param[out] out_token Will hold the newly read token.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permparser_nexttoken(gh_permparser * parser, gh_permtoken * out_token);

/** @brief Peek the next token.
 *
 * Subsequent calls to this function will return the same token.
 * Calling @ref gh_permparser_nexttoken will return the token that was last returned by this function before continuing to actually read further into the file.
 *
 * @param parser Parser instance.
 * @param[out] out_token Will hold the peeked token.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permparser_peektoken(gh_permparser * parser, gh_permtoken * out_token);

/** @brief Register a resource parser.
 *
 * See: @ref gh_permresourceparser res_parser.
 *
 * @param parser Parser instance.
 * @param res_parser Structure containing callbacks and userdata implementing the
 *                   resource parser.
 *
 * @return @ref GHR_OK on success, or @n
 *         @ref GHR_PERMPARSER_RESOURCEPARSERLIMIT
 */
gh_result gh_permparser_registerresource(gh_permparser * parser, gh_permresourceparser res_parser);

/** @brief Parse the whole file, calling resource parsers as necessary.
 *
 * @param parser Parser instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_permparser_parse(gh_permparser * parser);

/** @brief Read the next token's value, ensuring that it is an identifier token.
 
 * @param parser Parser instance.
 * @param[out] out_str Will contain the contents of the identifier token if the function succeeds.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 *  
 */
gh_result gh_permparser_nextidentifier(gh_permparser * parser, gh_conststr * out_str);

/** @brief Read the next token's value, ensuring that it is a string token.
 
 * @param parser Parser instance.
 * @param[out] out_str Will contain the contents of the string token if the function succeeds.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 *  
 */
gh_result gh_permparser_nextstring(gh_permparser * parser, gh_conststr * out_str);

/** @brief Read the next token's value, ensuring that it is a string token.
 
 * @param parser Parser instance.
 * @param detail Null terminated string of indefinite lifetime (string literal) describing the error.
 *
 * @return Always @ref GHR_PERMPARSER_RESOURCEPARSEFAIL.
 *         Return the result of this function up the stack from a @ref gh_permresourceparser callback.
 *  
 */
gh_result gh_permparser_resourceerror(gh_permparser * parser, const char * detail);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
