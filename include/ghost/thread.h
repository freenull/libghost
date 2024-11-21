#ifndef GHOST_THREAD_H
#define GHOST_THREAD_H

#include <stdlib.h>
#include <ghost/result.h>
#include <ghost/ipc.h>
#include <ghost/sandbox.h>
#include <ghost/rpc.h>
#include <ghost/alloc.h>

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
    gh_rpc rpc;
    void * userdata;
};

#ifndef GH_TYPEDEF_THREAD
typedef struct gh_thread gh_thread;
#define GH_TYPEDEF_THREAD
#endif

#define GH_THREAD_LUAINFO_TIMEOUTMS 1000

gh_result gh_sandbox_newthread(gh_sandbox * sandbox, gh_alloc * alloc, const char * name, const char * safe_id, gh_thread * out_thread);
gh_result gh_thread_dtor(gh_thread * thread);
gh_result gh_thread_attachuserdata(gh_thread * thread, void * userdata);
gh_result gh_thread_process(gh_thread * thread, gh_threadnotif * notif);
gh_result gh_thread_requestquit(gh_thread * thread);
gh_result gh_thread_forcekill(gh_thread * thread);
gh_rpc * gh_thread_rpc(gh_thread * thread);

gh_result gh_thread_runstring(gh_thread * thread, const char * s, size_t s_len, int * script_id);
gh_result gh_thread_runstringsync(gh_thread * thread, const char * s, size_t s_len, gh_threadnotif_script * out_status);

gh_result gh_thread_runfile(gh_thread * thread, int fd, int * script_id);
gh_result gh_thread_runfilesync(gh_thread * thread, int fd, gh_threadnotif_script * out_status);

#endif
