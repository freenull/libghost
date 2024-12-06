#ifndef PREP_TOKENIZER_H
#define PREP_TOKENIZER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PREP_TOKEN_WHITESPACE,
    PREP_TOKEN_TEXT,
    PREP_TOKEN_MACROMARKER,
    PREP_TOKEN_LPAREN,
    PREP_TOKEN_RPAREN,
    PREP_TOKEN_EOF
} prep_token_type;

typedef struct {
    prep_token_type type;
    char * value;
    size_t value_len;
    size_t row;
    size_t col;
} prep_token;

typedef struct {
    char * buf;
    size_t buf_size;

    size_t cur_idx;
    size_t cur_col;
    size_t cur_row;

    prep_token _token;
} prep_tokenizer;

prep_tokenizer prep_tokenizer_new(char * buf, size_t buf_size);
prep_token * prep_tokenizer_nexttoken(prep_tokenizer * tokenizer);

typedef struct {
    prep_token * buffer;
    size_t capacity;
    size_t size;
} prep_token_buffer;

prep_token_buffer prep_token_buffer_new(void);
bool prep_token_buffer_append(prep_token_buffer * buffer, prep_token token);
void prep_token_buffer_free(prep_token_buffer * buffer);

#endif
