/** @defgroup thread Thread
 *
 * @brief Library representation of a live subjail process with an installed RPC handler and permission store. Not to be confused with OS threads, although by definition the subjail process executes in parallel.
 *
 * @{
 */

#ifndef GHOST_THREAD_H
#define GHOST_THREAD_H

#include <stdlib.h>
#include <stdarg.h>
#include <ghost/result.h>
#include <ghost/ipc.h>
#include <ghost/fdmem.h>
#include <ghost/sandbox.h>
#include <ghost/rpc.h>
#include <ghost/alloc.h>
#include <ghost/perms/perms.h>
#include <ghost/variant.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GH_THREAD_MAXNAME 256
#define GH_THREAD_MAXSAFEID 512

typedef enum {
    GH_THREADNOTIF_FUNCTIONCALLED,
    GH_THREADNOTIF_SCRIPTRESULT
} gh_threadnotif_type;

#define GH_THREADNOTIF_SCRIPT_ERRORMSGMAX 1024

typedef struct {
    gh_result result;
    int id;
    char error_msg[GH_THREADNOTIF_SCRIPT_ERRORMSGMAX];

    gh_fdmem_ptr call_return_ptr;
} gh_threadnotif_script;

typedef struct {
    char name[GH_IPCMSG_FUNCTIONCALL_MAXNAME];
    bool missing;
} gh_threadnotif_function;

typedef struct {
    gh_threadnotif_type type;
    union {
        gh_threadnotif_script script;
        gh_threadnotif_function function;
    };
} gh_threadnotif;

struct gh_thread {
    gh_sandbox * sandbox;
    char name[GH_THREAD_MAXNAME];
    char safe_id[GH_THREAD_MAXSAFEID];
    pid_t pid;
    gh_ipc ipc;
    gh_rpc * rpc;
    gh_perms perms;
    int default_timeout_ms;
    void * userdata;
};

#ifndef GH_TYPEDEF_THREAD
typedef struct gh_thread gh_thread;
#define GH_TYPEDEF_THREAD
#endif

#define GH_THREAD_LUAINFO_TIMEOUTMS 1000

typedef struct {
    gh_sandbox * sandbox;
    gh_rpc * rpc;
    gh_permprompter prompter;
    char name[GH_THREAD_MAXNAME];
    char safe_id[GH_THREAD_MAXSAFEID];
    int default_timeout_ms;
} gh_threadoptions;

gh_result gh_thread_ctor(gh_thread * thread, gh_threadoptions options);
gh_result gh_thread_dtor(gh_thread * thread, gh_result * out_subjailresult);
gh_result gh_thread_attachuserdata(gh_thread * thread, void * userdata);
gh_result gh_thread_process(gh_thread * thread, gh_threadnotif * notif);
gh_rpc * gh_thread_rpc(gh_thread * thread);

gh_result gh_thread_runstring(gh_thread * thread, const char * s, size_t s_len, int * script_id);
gh_result gh_thread_runstringsync(gh_thread * thread, const char * s, size_t s_len, gh_threadnotif_script * out_status);

gh_result gh_thread_runfile(gh_thread * thread, int fd, int * script_id);
gh_result gh_thread_runfilesync(gh_thread * thread, int fd, gh_threadnotif_script * out_status);

gh_result gh_thread_setint(gh_thread * thread, const char * name, int value);
gh_result gh_thread_setdouble(gh_thread * thread, const char * name, double value);
gh_result gh_thread_setlstring(gh_thread * thread, const char * name, const char * string, size_t len);
gh_result gh_thread_setstring(gh_thread * thread, const char * name, const char * string);
gh_result gh_thread_setstringtable(gh_thread * thread, const char * name, const char * const * strings, int count);

typedef struct {
    size_t param_count;
    gh_fdmem fdmem;
    gh_fdmem_ptr param_ptrs[GH_IPCMSG_LUACALL_MAXPARAMS];

    bool returned;
    gh_variant * return_value;
} gh_thread_callframe;

gh_result gh_thread_callframe_ctor(gh_thread_callframe * frame);
gh_result gh_thread_callframe_int(gh_thread_callframe * frame, int value);
gh_result gh_thread_callframe_double(gh_thread_callframe * frame, double value);
gh_result gh_thread_callframe_lstring(gh_thread_callframe * frame, size_t size, const char * string);
gh_result gh_thread_callframe_string(gh_thread_callframe * frame, const char * string);
bool gh_thread_callframe_getint(gh_thread_callframe * frame, int * out_int);
bool gh_thread_callframe_getdouble(gh_thread_callframe * frame, double * out_double);
bool gh_thread_callframe_getlstring(gh_thread_callframe * frame, size_t * out_size, const char ** out_string);
bool gh_thread_callframe_getstring(gh_thread_callframe * frame, const char ** out_string);
gh_result gh_thread_callframe_loadreturnvalue(gh_thread_callframe * frame, gh_fdmem_ptr return_value_ptr);
gh_result gh_thread_callframe_dtor(gh_thread_callframe * frame);

gh_result gh_thread_call(gh_thread * thread, const char * name, gh_thread_callframe * frame, gh_threadnotif_script * out_script_result);


#ifdef __cplusplus
}
#endif

#endif

/** @} */
