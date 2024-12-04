#define _GNU_SOURCE
#include <sys/mman.h>
#include <ghost/sandbox.h>
#include <ghost/result.h>
#include <ghost/perms/parser.h>
#include <ghost/perms/prompt.h>

__attribute__((always_inline))
static inline char permparser_curchar(gh_permparser * parser) {
    if (parser->idx >= parser->size) return '\0';
    return parser->data[parser->idx];
}

static char permparser_nextchar(gh_permparser * parser) {
    if (parser->idx >= parser->size) return '\0';
    parser->idx += 1;
    parser->loc.column += 1;
    
    char c = permparser_curchar(parser);
    if (c == '\n') {
        parser->loc.row += 1;
        parser->loc.column = 0;
    }

    if (c == '#') {
        while (c != '\n' && c != '\0') {
            c = permparser_nextchar(parser);
        }
        permparser_nextchar(parser);
    }

    return c;
}

__attribute__((always_inline))
static inline bool permparser_chariswhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

__attribute__((always_inline))
static inline bool permparser_charisalphanumeric(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')  || (c >= 'A' && c <= 'Z') || c == '_';
}

__attribute__((always_inline))
static inline void permparser_skipwhitespace(gh_permparser * parser) {
    char c = permparser_curchar(parser);
    while (permparser_chariswhitespace(c)) {
        c = permparser_nextchar(parser);
    }
}

gh_result gh_permparser_ctorbuffer(gh_permparser * parser, gh_alloc * alloc, const char * buffer, size_t size) {
    *parser = (gh_permparser) {0};

    gh_result res = gh_bytebuffer_ctor(&parser->buffer, alloc);
    if (ghr_iserr(res)) return res;

    parser->data = buffer;
    parser->size = size;
    parser->idx = 0;
    parser->is_mmapped = false;

    parser->loc.row = 1;
    parser->loc.column = 1;

    parser->peek_token = (gh_permtoken) { .type = GH_PERMTOKEN_EOF };

    parser->resource_parsers_count = 0;

    permparser_skipwhitespace(parser);
    return GHR_OK;
}

gh_result gh_permparser_ctorfd(gh_permparser * parser, gh_alloc * alloc, int fd) {
    *parser = (gh_permparser) {0};

    if (lseek(fd, 0, SEEK_SET) < 0) return ghr_errno(GHR_PERMPARSER_FAILEDSEEK);
    off_t lseek_res = lseek(fd, 0, SEEK_END);
    if (lseek_res < 0) return ghr_errno(GHR_PERMPARSER_FAILEDSEEK);

    size_t file_size = (size_t)lseek_res;

    void * map = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) return ghr_errno(GHR_PERMPARSER_FAILEDMMAP);

    madvise(map, file_size, MADV_SEQUENTIAL | MADV_WILLNEED);
    // it's okay if madvise fails - we may just tokenize slower
    // but no need to fail

    gh_result res = gh_bytebuffer_ctor(&parser->buffer, alloc);
    if (ghr_iserr(res)) return res;
    parser->data = map;
    parser->size = file_size;
    parser->idx = 0;
    parser->is_mmapped = true;

    parser->loc.row = 1;
    parser->loc.column = 0;

    parser->peek_token = (gh_permtoken) {0};
    parser->peek_token.type = GH_PERMTOKEN_EOF;

    parser->resource_parsers_count = 0;

    permparser_skipwhitespace(parser);
    return GHR_OK;
}

gh_result gh_permparser_dtor(gh_permparser * parser) {
    if (parser->is_mmapped) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
        // RATIONALE: This buffer is only going to be affected if it's a pointer to an mmap'd file,
        // which is handled transparently by the appropriate ctor function. As far as the parser code
        // is concerned, this data cannot be modified.
        int munmap_res = munmap((char*)parser->data, parser->size);
#pragma clang diagnostic pop
        if (munmap_res < 0) return ghr_errno(GHR_PERMPARSER_FAILEDMUNMAP);
    }

    return gh_bytebuffer_dtor(&parser->buffer);
}

static gh_result permparser_readidentifier(gh_permparser * parser, gh_permtoken * out_token) {
    char c = permparser_curchar(parser);
    gh_result res;

    gh_permparser_loc start_loc = parser->loc;
    char * start = parser->buffer.buffer + parser->buffer.size;
    size_t len = 0;

    while(permparser_charisalphanumeric(c)) {
        res = gh_bytebuffer_append(&parser->buffer, &c, 1);
        if (ghr_iserr(res)) return res;
        len += 1;
        
        c = permparser_nextchar(parser);

    }

    res = gh_bytebuffer_append(&parser->buffer, "\0", 1);
    if (ghr_iserr(res)) return res;

    *out_token = (gh_permtoken) {
        .type = GH_PERMTOKEN_IDENTIFIER,
        .value = gh_conststr_fromc(start, len),
        .loc = start_loc
    };

    return GHR_OK;
}

static void permparser_seterror(gh_permparser * parser, gh_permparser_loc error_loc) {
    parser->error.loc = error_loc;
    parser->error.detail = NULL;
}

static gh_result permparser_readstring(gh_permparser * parser, gh_permtoken * out_token) {
    gh_permparser_loc start_loc = parser->loc;
    char c = permparser_nextchar(parser); // skip first "
    gh_result res;

    char * start = parser->buffer.buffer + parser->buffer.size;

    bool escape = false;

    while (c != '\0') {
        if (escape) {
            res = gh_bytebuffer_append(&parser->buffer, &c, 1);
            if (ghr_iserr(res)) return res;
        } else if (!escape) {
            if (c == '\\') {
                escape = true;
                continue;
            }

            if (c == '"') break;

            res = gh_bytebuffer_append(&parser->buffer, &c, 1);
            if (ghr_iserr(res)) return res;
        }

        
        c = permparser_nextchar(parser);
    }

    if (c == '\0') {
        permparser_seterror(parser, parser->loc);
        return GHR_PERMPARSER_UNTERMINATEDSTRING;
    }
    permparser_nextchar(parser); // skip last quote

    res = gh_bytebuffer_append(&parser->buffer, "\0", 1);
    if (ghr_iserr(res)) return res;

    // this expression can never evaluate to less than 1, because we did _nextchar
    // on a known non-EOF state before
    size_t size = (size_t)(parser->buffer.buffer + parser->buffer.size - start) - 1;
    *out_token = (gh_permtoken) {
        .type = GH_PERMTOKEN_STRING,
        .value = gh_conststr_fromc(start, size),
        .loc = start_loc
    };

    return GHR_OK;
}

gh_result gh_permparser_nexttoken(gh_permparser * parser, gh_permtoken * out_token) {
    if (parser->peek_token.type != GH_PERMTOKEN_EOF) {
        *out_token = parser->peek_token;
        parser->peek_token = (gh_permtoken) { .type = GH_PERMTOKEN_EOF };
        return GHR_OK;
    }

    char c = permparser_curchar(parser);

    gh_result res = GHR_OK;

    if (c == '\0') {
        *out_token = (gh_permtoken){
            .type = GH_PERMTOKEN_EOF,
            .value = gh_conststr_fromlit("\0"),
            .loc = parser->loc,
        };
    } else if (c == '{') {
        gh_permparser_loc loc = parser->loc;
        permparser_nextchar(parser);
        *out_token = (gh_permtoken){
            .type = GH_PERMTOKEN_LBRACE,
            .value = gh_conststr_fromlit("{"),
            .loc = loc
        };
    } else if (c == '}') {
        gh_permparser_loc loc = parser->loc;
        permparser_nextchar(parser);
        *out_token = (gh_permtoken){
            .type = GH_PERMTOKEN_RBRACE,
            .value = gh_conststr_fromlit("}"),
            .loc = loc
        };
    } else if (c == '"') {
        res = permparser_readstring(parser, out_token);
    } else if (permparser_charisalphanumeric(c)) {
        res = permparser_readidentifier(parser, out_token);
    } else {
        permparser_seterror(parser, parser->loc);
        return GHR_PERMPARSER_UNEXPECTEDTOKEN;
    }
    permparser_skipwhitespace(parser);
    return res;
}

gh_result gh_permparser_peektoken(gh_permparser * parser, gh_permtoken * out_token) {
    gh_result res = GHR_OK;

    if (parser->peek_token.type == GH_PERMTOKEN_EOF) {
        res = gh_permparser_nexttoken(parser, &parser->peek_token);
        if (ghr_iserr(res)) return res;
    }

    *out_token = parser->peek_token;
    return res;
}

gh_result gh_permparser_registerresource(gh_permparser * parser, gh_permresourceparser res_parser) {
    if (parser->resource_parsers_count >= GH_PERMPARSER_MAXRESOURCEPARSERS) {
        return GHR_PERMPARSER_RESOURCEPARSERLIMIT;
    }

    size_t idx = parser->resource_parsers_count;
    parser->resource_parsers_count += 1;

    gh_permresourceparser * resource_parser = parser->resource_parsers + idx;
    *resource_parser = res_parser;

    return GHR_OK;
}

static gh_result permparser_findresourceparser(gh_permparser * parser, gh_permrequest_id group_id, gh_permrequest_id resource_id, gh_permresourceparser ** out_res_parser) {
    gh_result res = GHR_OK;
    for (size_t i = 0; i < parser->resource_parsers_count; i++) {
        // just in case the callback returns an unexpected error, we set the
        // error loc to something reasonable
        permparser_seterror(parser, parser->loc);

        gh_permresourceparser * resource_parser = parser->resource_parsers + i;
        res = resource_parser->matches(parser, group_id, resource_id, resource_parser->userdata);
        if (ghr_iserr(res) && !ghr_is(res, GHR_PERMPARSER_NOMATCH)) return res;

        if (ghr_isok(res)) {
            *out_res_parser = resource_parser;
            return GHR_OK;
        }
    }

    return GHR_PERMPARSER_UNKNOWNRESOURCE;
}

static gh_result permparser_parsenextresource(gh_permparser * parser, bool * out_eof) {
    gh_result res = GHR_OK;
    gh_permtoken token;

    *out_eof = false;

    res = gh_permparser_nexttoken(parser, &token);
    if (ghr_iserr(res)) return res;

    if (token.type == GH_PERMTOKEN_EOF) {
        *out_eof = true;
        return GHR_OK;
    }

    if (token.type != GH_PERMTOKEN_IDENTIFIER) {
        permparser_seterror(parser, token.loc);
        return GHR_PERMPARSER_EXPECTEDGROUPID;
    }

    gh_permrequest_id group_id;
    if (!gh_conststr_copyz(token.value, group_id, GH_PERMREQUEST_IDMAX)) {
        permparser_seterror(parser, token.loc);
        return GHR_PERMPARSER_LARGEGROUPID;
    }

    res = gh_permparser_nexttoken(parser, &token);
    if (ghr_iserr(res)) return res;

    if (token.type != GH_PERMTOKEN_IDENTIFIER) {
        permparser_seterror(parser, token.loc);
        return GHR_PERMPARSER_EXPECTEDRESOURCEID;
    }

    gh_permrequest_id resource_id;
    if (!gh_conststr_copyz(token.value, resource_id, GH_PERMREQUEST_IDMAX)) {
        permparser_seterror(parser, token.loc);
        return GHR_PERMPARSER_LARGERESOURCEID;
    }

    res = gh_permparser_nexttoken(parser, &token);
    if (ghr_iserr(res)) return res;

    if (token.type != GH_PERMTOKEN_LBRACE) {
        permparser_seterror(parser, token.loc);
        return GHR_PERMPARSER_EXPECTEDRESOURCEBEGIN;
    }

    gh_permresourceparser * res_parser;
    res = permparser_findresourceparser(parser, group_id, resource_id, &res_parser);
    if (ghr_iserr(res)) return res;

    while (true) {
        res = gh_permparser_peektoken(parser, &token);
        if (ghr_iserr(res)) return res;

        if (token.type == GH_PERMTOKEN_RBRACE) break;

        res = gh_permparser_nexttoken(parser, &token);
        if (ghr_iserr(res)) return res;

        if (token.type != GH_PERMTOKEN_STRING) {
            permparser_seterror(parser, token.loc);
            return GHR_PERMPARSER_EXPECTEDENTRY;
        }

        // just in case the callback returns an error, we set the
        // error loc to something reasonable
        permparser_seterror(parser, token.loc);

        void * entry = NULL;
        res = res_parser->newentry(parser, token.value, res_parser->userdata, &entry);
        if (ghr_iserr(res)) return res;

        res = gh_permparser_nexttoken(parser, &token);
        if (ghr_iserr(res)) return res;

        if (token.type != GH_PERMTOKEN_LBRACE) {
            permparser_seterror(parser, token.loc);
            return GHR_PERMPARSER_EXPECTEDENTRYBEGIN;
        }

        res = gh_permparser_nexttoken(parser, &token);
        if (ghr_iserr(res)) return res;

        while(token.type != GH_PERMTOKEN_RBRACE && token.type != GH_PERMTOKEN_EOF) {
            if (token.type != GH_PERMTOKEN_IDENTIFIER) {
                permparser_seterror(parser, token.loc);
                return GHR_PERMPARSER_EXPECTEDFIELD;
            }

            gh_permrequest_id field;
            if (!gh_conststr_copyz(token.value, field, GH_PERMREQUEST_IDMAX)) {
                permparser_seterror(parser, token.loc);
                return GHR_PERMPARSER_LARGEFIELD;
            }

            // just in case the callback returns an unexpected error, we set the
            // error loc to something reasonable
            permparser_seterror(parser, token.loc);

            res = res_parser->setfield(parser, entry, field, res_parser->userdata);
            if (ghr_iserr(res)) return res;

            res = gh_permparser_nexttoken(parser, &token);
            if (ghr_iserr(res)) return res;
        }

        res = gh_bytebuffer_clear(&parser->buffer);
        if (ghr_iserr(res)) return res;

        if (token.type != GH_PERMTOKEN_RBRACE) {
            permparser_seterror(parser, token.loc);
            return GHR_PERMPARSER_EXPECTEDENTRYEND;
        }
    }

    res = gh_permparser_nexttoken(parser, &token);
    if (ghr_iserr(res)) return res;

    if (token.type != GH_PERMTOKEN_RBRACE) {
            permparser_seterror(parser, token.loc);
        return GHR_PERMPARSER_EXPECTEDRESOURCEEND;
    }

    return GHR_OK;
}

gh_result gh_permparser_parse(gh_permparser * parser) {
    gh_result res = GHR_OK;

    while (true) {
        bool eof = false;
        res = permparser_parsenextresource(parser, &eof);
        if (ghr_iserr(res)) return res;

        if (eof) return GHR_OK;
    }
}

gh_result gh_permparser_nextidentifier(gh_permparser * parser, gh_conststr * out_str) {
    permparser_seterror(parser, parser->loc);

    gh_permtoken token;
    gh_result res = gh_permparser_peektoken(parser, &token);
    if (ghr_iserr(res)) return res;

    if (token.type != GH_PERMTOKEN_IDENTIFIER) {
        permparser_seterror(parser, token.loc);
        return GHR_PERMPARSER_EXPECTEDIDENTIFIER;
    }

    res = gh_permparser_nexttoken(parser, &token);
    if (ghr_iserr(res)) return res;

    *out_str = token.value;
    return GHR_OK;
}

gh_result gh_permparser_nextstring(gh_permparser * parser, gh_conststr * out_str) {
    permparser_seterror(parser, parser->loc);

    gh_permtoken token;
    gh_result res = gh_permparser_peektoken(parser, &token);
    if (ghr_iserr(res)) return res;

    if (token.type != GH_PERMTOKEN_STRING) {
        permparser_seterror(parser, token.loc);
        return GHR_PERMPARSER_EXPECTEDSTRING;
    }

    res = gh_permparser_nexttoken(parser, &token);
    if (ghr_iserr(res)) return res;

    *out_str = token.value;
    return GHR_OK;
}

gh_result gh_permparser_resourceerror(gh_permparser * parser, const char * detail) {
    parser->error.detail = detail;

    return GHR_PERMPARSER_RESOURCEPARSEFAIL;
}
