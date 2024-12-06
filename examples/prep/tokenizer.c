#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "tokenizer.h"

prep_tokenizer prep_tokenizer_new(char * buf, size_t buf_size) {
    return (prep_tokenizer) {
        .buf = buf,
        .buf_size = buf_size,

        .cur_idx = 0,
        .cur_col = 1,
        .cur_row = 1
    };
}

static char curchar(prep_tokenizer * tokenizer) {
    if (tokenizer->cur_idx >= tokenizer->buf_size) return '\0';
    return tokenizer->buf[tokenizer->cur_idx];
}

static void nextchar(prep_tokenizer * tokenizer) {
    if (tokenizer->cur_idx >= tokenizer->buf_size) return;

    tokenizer->cur_idx += 1;
    if (curchar(tokenizer) == '\n') {
        tokenizer->cur_col = 0;
        tokenizer->cur_row += 1;
    } else {
        tokenizer->cur_col += 1;
    }
}

static prep_token * tokenptr(prep_tokenizer * tokenizer, prep_token token) {
    tokenizer->_token = token;
    return &tokenizer->_token;
}

__attribute__((always_inline))
static inline prep_token * immtoken(prep_tokenizer * tokenizer, prep_token_type type) {
    prep_token * token = tokenptr(tokenizer, (prep_token) {
        .type = type,
        .value = type == PREP_TOKEN_EOF ? "\0" : (tokenizer->buf + tokenizer->cur_idx),
        .value_len = type == PREP_TOKEN_EOF ? 0 : 1,
        .row = tokenizer->cur_row,
        .col = tokenizer->cur_col,
    });
    nextchar(tokenizer);
    return token;
}

static bool iswhitespace(char c) {
    return c == '\n' || c == '\t' || c == ' ';
}

static prep_token * eatwhitespace(prep_tokenizer * tokenizer) {
    char * start = tokenizer->buf + tokenizer->cur_idx;
    size_t len = 0;

    size_t start_col = tokenizer->cur_col;
    size_t start_row = tokenizer->cur_row;

    while (true) {
        char c = curchar(tokenizer);
        if (c == '\0') break;
        if (!iswhitespace(c)) break;

        len += 1;
        nextchar(tokenizer);
    }

    return tokenptr(tokenizer, (prep_token) {
        .type = PREP_TOKEN_WHITESPACE,
        .value = start,
        .value_len = len,
        .col = start_col,
        .row = start_row
    });
}

static bool isspecial(char c) {
    return c == '#' || c == '(' || c == ')';
}

static prep_token * eattext(prep_tokenizer * tokenizer) {
    char * start = tokenizer->buf + tokenizer->cur_idx;
    size_t len = 0;

    size_t start_col = tokenizer->cur_col;
    size_t start_row = tokenizer->cur_row;

    while (true) {
        char c = curchar(tokenizer);
        if (c == '\0') break;
        if (iswhitespace(c)) break;
        if (isspecial(c)) break;

        len += 1;
        nextchar(tokenizer);
    }

    return tokenptr(tokenizer, (prep_token) {
        .type = PREP_TOKEN_TEXT,
        .value = start,
        .value_len = len,
        .col = start_col,
        .row = start_row
    });
}

prep_token * prep_tokenizer_nexttoken(prep_tokenizer * tokenizer) {
    char c = curchar(tokenizer);
    if (c == '\0') return immtoken(tokenizer, PREP_TOKEN_EOF);
    if (c == '#') return immtoken(tokenizer, PREP_TOKEN_MACROMARKER);
    if (c == '(') return immtoken(tokenizer, PREP_TOKEN_LPAREN);
    if (c == ')') return immtoken(tokenizer, PREP_TOKEN_RPAREN);
    if (iswhitespace(c)) {
        return eatwhitespace(tokenizer);
    }
    return eattext(tokenizer);
}

prep_token_buffer prep_token_buffer_new(void) {
    return (prep_token_buffer) {
        .buffer = NULL,
        .capacity = 256,
        .size = 0
    };
}

bool prep_token_buffer_append(prep_token_buffer * buffer, prep_token token) {
    if (buffer->buffer == NULL || buffer->size + 1 > buffer->capacity) {
        size_t new_capacity = buffer->buffer == NULL ? buffer->capacity : buffer->capacity * 2;

        prep_token * new_ptr = realloc(buffer->buffer, new_capacity * sizeof(prep_token));
        if (new_ptr == NULL) return false;
        buffer->capacity = new_capacity;
        buffer->buffer = new_ptr;
    }

    memcpy(buffer->buffer + buffer->size, &token, sizeof(prep_token));
    buffer->size += 1;
    return true;
}

void prep_token_buffer_free(prep_token_buffer * buffer) {
    if (buffer->buffer != NULL) free(buffer->buffer);
}
