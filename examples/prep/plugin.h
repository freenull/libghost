#include <ghost/ghost.h>

#define MEMORY_LIMIT_BYTES (1024 * 1024 * 10)
#define FRAME_LIMIT_BYTES (1024 * 1024 * 10)
#define MAX_PLUGINS 32
#define DEFAULT_TIMEOUT_MS 3000

#define MAX_MACROS_PER_PLUGIN 128
#define MAX_MACRO_NAME_LEN 255

typedef struct {
    char name[MAX_MACRO_NAME_LEN + 1];
} prep_plugin_macro;

typedef struct {
    gh_thread thread;
    char ghperm_path[GH_THREAD_MAXSAFEID + 32];

    size_t macro_count;
    prep_plugin_macro macros[MAX_MACROS_PER_PLUGIN];
} prep_plugin;

typedef struct {
    gh_alloc alloc;
    gh_sandbox sandbox;
    gh_rpc rpc;
    gh_permprompter prompter;

    size_t plugin_count;
    prep_plugin plugins[MAX_PLUGINS];
} prep_plugin_system;

bool prep_plugin_system_make(prep_plugin_system * sys);
bool prep_plugin_system_destroy(prep_plugin_system * sys);
prep_plugin * prep_plugin_system_findplugin(prep_plugin_system * sys, const char * safe_id);
bool prep_plugin_system_loadplugin(prep_plugin_system * sys, const char * path);
bool prep_plugin_system_executemacro(prep_plugin_system * sys, const char * macro_name, char * param, size_t param_len);
