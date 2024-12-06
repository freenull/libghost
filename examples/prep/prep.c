#define _GNU_SOURCE
#include <sys/wait.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>
#include <ghost/perms/perms.h>
#include <ghost/perms/prompt.h>
#include <ghost/sandbox.h>
#include <ghost/thread.h>
#include <ghost/stdlib.h>
#include "tokenizer.h"
#include "plugin.h"

static prep_plugin_system plugin_system;

typedef struct {
    char * input_path;
    int plugin_count;
    char * plugin_paths[MAX_PLUGINS];
} cmdline_options;

static void print_usage(char * program_name, FILE * file) {
    fprintf(file, "usage: %s input [-p plugin.lua [-p plugin2.lua [...]]]\n", program_name);
}

static cmdline_options parse_options(int argc, char ** argv) {
    cmdline_options opts = {0};

    char * program_name = argv[0];

    if (argc < 2) {
        print_usage(program_name, stderr);
        exit(1);
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(program_name, stdout);
        exit(0);
    }

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            if (opts.input_path != NULL) {
                print_usage(program_name, stderr);
                exit(1);
            }

            opts.input_path = argv[i];
        } else if (strcmp(argv[i], "-p") == 0) {
            i += 1;
            if (i >= argc) {
                print_usage(program_name, stderr);
                exit(1);
            }

            if (opts.plugin_count >= MAX_PLUGINS) {
                fprintf(stderr, "error: exceeded maximum number of plugins: %d\n", MAX_PLUGINS);
                exit(1);
            }


            int plugin_idx = opts.plugin_count;
            opts.plugin_paths[plugin_idx] = argv[i];
            opts.plugin_count += 1;
        }
    }

    return opts;
}

static void fail_errno(const char * msg) {
    fprintf(stderr, "error: %s: %s\n", msg, strerror(errno));
    exit(1);
}

static void execute_macro(prep_plugin_system * sys, char * name, size_t name_len, char * param, size_t param_len) {
    char macro[name_len + 1];
    strncpy(macro, name, name_len);
    macro[name_len] = '\0';

    if (!prep_plugin_system_executemacro(sys, macro, param, param_len)) {
        exit(1);
    }
    
    fflush(stdout);
}

enum fsm_state_id {
    FSM_NORMAL,
    FSM_MACRONAME,
    FSM_MACROLPAREN,
    FSM_MACROPARAM
};

struct fsm_state {
    enum fsm_state_id id;

    prep_plugin_system * plugin_system;

    prep_token macro_name_token;
    char * macro_param_start;
    int param_level;
};

int process_normal(struct fsm_state * state, prep_token * token) {
    switch(token->type) {
    case PREP_TOKEN_MACROMARKER:
        state->id = FSM_MACRONAME;
        break;

    default:
        printf("%.*s", (int)token->value_len, token->value);
        fflush(stdout);
        break;
    }

    return 0;
}

int process_macroname(struct fsm_state * state, prep_token * token) {
    switch(token->type) {
    case PREP_TOKEN_MACROMARKER:
        printf("#");
        fflush(stdout);
        state->id = FSM_NORMAL;
        break;

    case PREP_TOKEN_TEXT:
        state->macro_name_token = *token;
        state->id = FSM_MACROLPAREN;
        break;

    default:
        printf("#");
        fflush(stdout);
        state->id = FSM_NORMAL;
        return -1;
    }

    return 0;
}

int process_macrolparen(struct fsm_state * state, prep_token * token) {
    switch(token->type) {
    case PREP_TOKEN_LPAREN:
        state->macro_param_start = token->value + 1;
        state->param_level += 1;
        state->id = FSM_MACROPARAM;
        break;

    default:
        printf("#");
        fflush(stdout);
        state->id = FSM_NORMAL;
        return -2;
    }

    return 0;
}

int process_macroparam(struct fsm_state * state, prep_token * token) {
    switch(token->type) {
    case PREP_TOKEN_LPAREN:
        state->param_level += 1;
        break;

    case PREP_TOKEN_RPAREN:
        state->param_level -= 1;
        if (state->param_level == 0) {
            execute_macro(
                state->plugin_system,
                state->macro_name_token.value,
                state->macro_name_token.value_len,
                state->macro_param_start,
                token->value - state->macro_param_start
            );

            state->param_level = 0;
            state->macro_name_token = (prep_token) { .type = PREP_TOKEN_EOF, .value = "", .value_len = 0 };
            state->macro_param_start = NULL;
            state->id = FSM_NORMAL;
        }
        break;

    case PREP_TOKEN_EOF:
        printf("#");
        fflush(stdout);
        state->id = FSM_NORMAL;
        return -3;

    default:
        break;
    }

    return 0;
}

void process(prep_token_buffer * tokens, prep_plugin_system * plugin_system) {
    prep_token_type back_type = PREP_TOKEN_EOF;
    struct fsm_state state = {
        .plugin_system = plugin_system,

        .id = FSM_NORMAL,
        .param_level = 0,
        
        .macro_name_token = { .type = PREP_TOKEN_EOF, .value = "", .value_len = 0 },
        .macro_param_start = NULL,
    };

    for (size_t i = 0; i < tokens->size; i++) {
        prep_token * token = tokens->buffer + i;

        switch(state.id) {
        case FSM_NORMAL:
            i += process_normal(&state, token);
            break;
        case FSM_MACRONAME:
            i += process_macroname(&state, token);
            break;
        case FSM_MACROLPAREN:
            i += process_macrolparen(&state, token);
            break;
        case FSM_MACROPARAM:
            i += process_macroparam(&state, token);
            break;
        }
    }
}

void preprocess(char * path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) fail_errno("failed opening input path");

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0) fail_errno("failed seeking input file");

    if (lseek(fd, 0, SEEK_SET) < 0) fail_errno("failed seeking input file back to start");

    char buffer[size];
    ssize_t nread = read(fd, buffer, size);
    if (nread < 0) fail_errno("failed reading input file");
    if (nread != size) {
        fprintf(stderr, "data read from input file was truncatedn");
        exit(1);
    }

    if (close(fd) < 0) fail_errno("failed closing input file");

    prep_tokenizer tokenizer = prep_tokenizer_new(buffer, size);
    prep_token_buffer buf = prep_token_buffer_new();

    while (true) {
        prep_token * token = prep_tokenizer_nexttoken(&tokenizer);

        if (!prep_token_buffer_append(&buf, *token)) fail_errno("failed collecting tokens");
        if (token->type == PREP_TOKEN_EOF) break;
    }

    process(&buf, &plugin_system);
}

int main(int argc, char ** argv) {
    cmdline_options options = parse_options(argc, argv);

    if (!prep_plugin_system_make(&plugin_system)) exit(1);

    for (size_t i = 0; i < options.plugin_count; i++) {
        fprintf(stderr, "loading plugin: %s\n", options.plugin_paths[i]);
        if (!prep_plugin_system_loadplugin(&plugin_system, options.plugin_paths[i])) {
            exit(1);
        }
    }

    fprintf(stderr, "preprocess: %s\n", options.input_path);
    fprintf(stderr, "plugins: %d\n", options.plugin_count);

    preprocess(options.input_path);

    if (!prep_plugin_system_destroy(&plugin_system)) exit(1);
    return 0;
}
