#include <libgen.h>
#include <ghost/perms/prompt.h>
#include <ghost/rpc.h>
#include <ghost/sandbox.h>
#include <ghost/thread.h>
#include <unistd.h>
#include "plugin.h"
#include "ghost/perms/perms.h"

static bool prep_plugin_make(prep_plugin * plugin, prep_plugin_system * sys, const char * path) {
    gh_threadoptions options = {0};

    size_t path_len = strlen(path);
    char basename_buf[path_len + 1];
    strcpy(basename_buf, path);
    basename(basename_buf);
    
    options.sandbox = &sys->sandbox;
    options.rpc = &sys->rpc;
    options.prompter = sys->prompter;
    options.default_timeout_ms = DEFAULT_TIMEOUT_MS;
    strncpy(options.name, basename_buf, GH_THREAD_MAXNAME - 1);
    options.name[GH_THREAD_MAXNAME - 1] = '\0';
    strncpy(options.safe_id, path, GH_THREAD_MAXSAFEID - 1);
    options.safe_id[GH_THREAD_MAXSAFEID - 1] = '\0';

    gh_result res = gh_thread_ctor(&plugin->thread, options);
    if (ghr_iserr(res)) {
        fprintf(stderr, "libghost error: ");
        ghr_fputs(stderr, res);
        return false;
    }

    plugin->thread.perms.exec.default_mode = GH_PERMEXEC_PROMPT;

    size_t ghperm_path_len = path_len + strlen(".ghperm");
    if (ghperm_path_len >= GH_THREAD_MAXSAFEID + 32) {
        fprintf(stderr, "warning: ghperm path '%s.ghperm' is too long, will not save permissions for this plugin\n", path);
    } else {
        strncpy(plugin->ghperm_path, path, path_len);
        strcpy(plugin->ghperm_path + path_len, ".ghperm");

        int ghperm_fd = open(plugin->ghperm_path, O_RDONLY);
        if (ghperm_fd >= 0) {
            gh_permparser_error error = {0};
            res = gh_perms_readfd(&plugin->thread.perms, ghperm_fd, &error);
            if (ghr_iserr(res)) {
                fprintf(stderr, "libghost permission parser error: ");
                ghr_fputs(stderr, res);
                if (error.detail != NULL) {
                    fprintf(stderr, "[%zu:%zud] %s\n", error.loc.row, error.loc.column, error.detail);
                }
                return false;
            }

            if (close(ghperm_fd) < 0) {
                fprintf(stderr, "error: failed closing saved ghperm file");
                return false;
            }
        }
    }


    plugin->macro_count = 0;

    return true;
}

static bool prep_plugin_destroy(prep_plugin * plugin) {
    int ghperm_fd = open(plugin->ghperm_path, O_CREAT | O_WRONLY, 0600);
    if (ghperm_fd >= 0) {
        gh_result res = gh_perms_write(&plugin->thread.perms, ghperm_fd);
        if (ghr_iserr(res)) {
            if (ftruncate(ghperm_fd, 0) < 0) {
                fprintf(stderr, "error: failed truncating saved ghperm file '%s' while recovering from another error (next line): %s\n", plugin->ghperm_path, strerror(errno));
                ghr_fputs(stderr, res);
                return false;
            }

            fprintf(stderr, "libghost permission writer error ('%s'): ", plugin->ghperm_path);
            ghr_fputs(stderr, res);
            return false;
        }

        if (close(ghperm_fd) < 0) {
            fprintf(stderr, "error: failed closing ghperm file '%s'", plugin->ghperm_path);
            return false;
        }
    } else {
        fprintf(stderr, "warning: failed opening ghperm file, permissions will not be saved\n");
    }

    gh_result thread_res;
    gh_result res = gh_thread_dtor(&plugin->thread, &thread_res);
    if (ghr_iserr(res)) goto libghost_error;
    if (ghr_iserr(thread_res)) {
        res = thread_res;
        goto libghost_error;
    }

    return true;

libghost_error:
    fprintf(stderr, "libghost error: ");
    ghr_fputs(stderr, res);
    return false;
}

static bool prep_plugin_regmacro(prep_plugin * plugin, const char * macro_name, size_t macro_name_len) {
    if (macro_name_len > MAX_MACRO_NAME_LEN) {
        fprintf(stderr, "plugin '%s' attempted to register macro with name '%.*s', which is too long (the limit is %zu bytes)", plugin->thread.safe_id, (int)macro_name_len, macro_name, (size_t)MAX_MACRO_NAME_LEN);
        return false;
    }

    if (plugin->macro_count >= MAX_MACROS_PER_PLUGIN) {
        fprintf(stderr, "plugin '%s' attempted to register macro '%.*s', but the limit of macros per plugin has been reached (the limit is %zu macros)", plugin->thread.safe_id, (int)macro_name_len, macro_name, (size_t)MAX_MACROS_PER_PLUGIN);
        return false;
    }

    strncpy(plugin->macros[plugin->macro_count].name, macro_name, macro_name_len);
    plugin->macros[plugin->macro_count].name[macro_name_len] = '\0';

    plugin->macro_count += 1;

    return true;
}

static void rpc_regmacro(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * name_buf;
    size_t name_size;
    if (!gh_rpcframe_argbuf(frame, 0, &name_buf, &name_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }

    if (name_size > INT_MAX) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EFBIG));
    }
    name_buf[name_size - 1] = '\0';

    prep_plugin_system * sys = (prep_plugin_system*) frame->thread->userdata;

    prep_plugin * plugin = prep_plugin_system_findplugin(sys, frame->thread->safe_id);
    if (plugin == NULL) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, ENOENT));
    }

    if (!prep_plugin_regmacro(plugin, name_buf, name_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }
}

bool prep_plugin_system_make(prep_plugin_system * sys) {
    *sys = (prep_plugin_system) {0};

    sys->alloc = gh_alloc_default();

    gh_result res = gh_sandbox_ctor(&sys->sandbox, (gh_sandboxoptions){
        .memory_limit_bytes = MEMORY_LIMIT_BYTES,
        .name = "prep sandbox",
        .functioncall_frame_limit_bytes = FRAME_LIMIT_BYTES,
    });
    if (ghr_iserr(res)) goto libghost_error;

    res = gh_rpc_ctor(&sys->rpc, &sys->alloc);
    if (ghr_iserr(res)) goto libghost_error;

    res = gh_std_registerinrpc(&sys->rpc);
    if (ghr_iserr(res)) goto libghost_error;

    res = gh_rpc_register(&sys->rpc, "regmacro", rpc_regmacro, GH_RPCFUNCTION_THREADUNSAFELOCAL);

    sys->prompter = gh_permprompter_simpletui_stderr(STDIN_FILENO);

    return true;

libghost_error:
    fprintf(stderr, "libghost error: ");
    ghr_fputs(stderr, res);
    return false;
}

bool prep_plugin_system_destroy(prep_plugin_system * sys) {
    for (size_t i = 0; i < sys->plugin_count; i++) {
        if (!prep_plugin_destroy(sys->plugins + i)) return false;
    }

    gh_result res = gh_rpc_dtor(&sys->rpc);
    if (ghr_iserr(res)) goto libghost_error;

    gh_result sandbox_res;
    res = gh_sandbox_dtor(&sys->sandbox, &sandbox_res);
    if (ghr_iserr(res)) goto libghost_error;
    if (ghr_iserr(sandbox_res)) {
        res = sandbox_res;
        goto libghost_error;
    }

    return true;

libghost_error:
    fprintf(stderr, "libghost error: ");
    ghr_fputs(stderr, res);
    return false;
}

prep_plugin * prep_plugin_system_findplugin(prep_plugin_system * sys, const char * safe_id) {
    for (size_t i = 0; i < sys->plugin_count; i++) {
        prep_plugin * plugin = sys->plugins + i;

        if (strcmp(plugin->thread.safe_id, safe_id) == 0) return plugin;
    }

    return NULL;
}

bool prep_plugin_system_loadplugin(prep_plugin_system * sys, const char * path) {
    prep_plugin * plugin = sys->plugins + sys->plugin_count;
    if (!prep_plugin_make(plugin, sys, path)) return false;
    plugin->thread.userdata = (void*)sys;
    sys->plugin_count += 1;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "error: failed opening plugin file '%s': %s\n", path, strerror(errno));
        return false;
    }

    gh_result res = gh_thread_setstring(&plugin->thread, "pluginname", plugin->thread.safe_id);
    if (ghr_iserr(res)) goto libghost_error;

    const char * init =
        "ghost = require('ghost')\n"
        "function macro(name, func)\n"
        "    name = tostring(name)\n"
        "    ghost.callbacks[name] = function(param)\n"
        "        func(param)\n"
        "        io.flush()\n"
        "    end\n"
        "    ghost.call('regmacro', nil, name)\n"
        "end\n"
        "function write(text)\n"
        "    io.write(tostring(text))\n"
        "end\n"
        "function trim(s)\n"
        "   return s:gsub('^%s*(.-)%s*$', '%1')\n"
        "end\n"
    ;

    gh_threadnotif_script status;
    res = gh_thread_runstringsync(&plugin->thread, init, strlen(init), &status);
    if (ghr_iserr(res)) goto libghost_error;
    if (ghr_iserr(status.result)) {
        res = status.result;
        goto libghost_error;
    }


    res = gh_thread_runfilesync(&plugin->thread, fd, &status);
    if (ghr_iserr(res)) goto libghost_error;

    if (ghr_iserr(status.result)) {
        fprintf(stderr, "libghost error while loading plugin '%s': ", path);
        ghr_fputs(stderr, status.result);
        if (status.error_msg[0] != '\0') {
            fprintf(stderr, "%s\n", status.error_msg);
        }
        return false;
    }

    return true;

libghost_error:
    fprintf(stderr, "libghost error: ");
    ghr_fputs(stderr, res);
    return false;
}

bool prep_plugin_system_executemacro(prep_plugin_system * sys, const char * macro_name, char * param, size_t param_len) {
    gh_result res;

    for (size_t i = 0; i < sys->plugin_count; i++) {
        prep_plugin * plugin = sys->plugins + i;

        for (size_t j = 0; j < plugin->macro_count; j++) {
            prep_plugin_macro * macro = plugin->macros + j;
            
            if (strcmp(macro->name, macro_name) == 0) {
                gh_thread_callframe frame;
                res = gh_thread_callframe_ctor(&frame);
                if (ghr_iserr(res)) goto libghost_error;

                res = gh_thread_callframe_lstring(&frame, param_len, param);
                if (ghr_iserr(res)) goto cleanup_callframe;

                gh_threadnotif_script status;
                res = gh_thread_call(&plugin->thread, macro_name, &frame, &status);
                if (ghr_iserr(res)) goto cleanup_callframe;
                if (ghr_iserr(status.result)) {
                    fprintf(stderr, "libghost error while running macro '%s' in plugin '%s': ", macro_name, plugin->thread.safe_id);
                    ghr_fputs(stderr, status.result);
                    if (status.error_msg[0] != '\0') {
                        fprintf(stderr, "%s\n", status.error_msg);
                    }
                    goto cleanup_callframe;
                }

                res = gh_thread_callframe_dtor(&frame);
                if (ghr_iserr(res)) goto libghost_error;

                return true;

cleanup_callframe:
                ghr_assert(gh_thread_callframe_dtor(&frame));
                goto libghost_error;


            }
        }
    }

    fprintf(stderr, "error: no macro with the name '%s' defined\n", macro_name);
    return false;

libghost_error:
    fprintf(stderr, "libghost error: ");
    ghr_fputs(stderr, res);
    return false;
}
