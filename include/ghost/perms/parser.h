#ifndef GHOST_PERMS_PARSER_H
#define GHOST_PERMS_PARSER_H

#include <stdio.h>
#include <string.h>
#include <ghost/alloc.h>
#include <ghost/byte_buffer.h>
#include <ghost/perms/actreq.h>
#include <ghost/strings.h>

typedef enum {
    GH_PERMTOKEN_IDENTIFIER,
    GH_PERMTOKEN_STRING,
    GH_PERMTOKEN_LBRACE,
    GH_PERMTOKEN_RBRACE,
    GH_PERMTOKEN_EOF
} gh_permtoken_type;

typedef struct {
    size_t row;
    size_t column;
} gh_permparser_loc;

typedef struct {
    gh_permtoken_type type;
    gh_str value;
    gh_permparser_loc loc;
} gh_permtoken;

typedef struct gh_permparser gh_permparser;

typedef gh_result gh_permresourceparser_matches_func(gh_permparser * parser, gh_permrequest_id group_id, gh_permrequest_id resource_id, void * userdata);
typedef gh_result gh_permresourceparser_newentry_func(gh_permparser * parser, gh_str value, void * userdata, void ** out_entry);
typedef gh_result gh_permresourceparser_setfield_func(gh_permparser * parser, void * entry, gh_permrequest_id field, void * userdata);

typedef struct {
    void * userdata;
    gh_permresourceparser_matches_func * matches;
    gh_permresourceparser_newentry_func * newentry;
    gh_permresourceparser_setfield_func * setfield;
} gh_permresourceparser;

typedef struct {
    gh_permparser_loc loc;
    const char * detail;
} gh_permparser_error;

#define GH_PERMPARSER_BUFSIZE 4096
#define GH_PERMPARSER_MAXRESOURCEPARSERS 128
struct gh_permparser {
    gh_bytebuffer buffer; // for token values

    const char * data; // input data
    bool is_mmapped;
    size_t size;
    size_t idx;

    gh_permparser_loc loc;
    gh_permtoken peek_token;

    size_t resource_parsers_count;
    gh_permresourceparser resource_parsers[GH_PERMPARSER_MAXRESOURCEPARSERS];

    gh_permparser_error error;
};

gh_result gh_permparser_ctorbuffer(gh_permparser * parser, gh_alloc * alloc, const char * buffer, size_t size);
gh_result gh_permparser_ctorfd(gh_permparser * parser, gh_alloc * alloc, int fd);
gh_result gh_permparser_dtor(gh_permparser * parser);
gh_result gh_permparser_nexttoken(gh_permparser * parser, gh_permtoken * out_token);
gh_result gh_permparser_peektoken(gh_permparser * parser, gh_permtoken * out_token);

typedef struct {
    gh_permrequest_id group;
    gh_permrequest_id resource;
} gh_permparser_section;

gh_result gh_permparser_registerresource(gh_permparser * parser, gh_permresourceparser res_parser);
gh_result gh_permparser_parse(gh_permparser * parser);
gh_result gh_permparser_nextidentifier(gh_permparser * parser, gh_str * out_str);
gh_result gh_permparser_nextstring(gh_permparser * parser, gh_str * out_str);
gh_result gh_permparser_resourceerror(gh_permparser * parser, const char * detail);

#endif
