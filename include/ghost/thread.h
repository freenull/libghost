#ifndef GHOST_THREAD_H
#define GHOST_THREAD_H

#include <stdlib.h>
#include <ghost/result.h>
#include <ghost/ipc.h>
#include <ghost/sandbox.h>
#include <ghost/rpc.h>
#include <ghost/alloc.h>

#define GH_THREAD_MAXNAME 256

typedef enum {
    GH_THREADNOTIF_FUNCTIONCALLED,
    GH_THREADNOTIF_SCRIPTRESULT
} gh_threadnotif_type;

typedef struct {
    gh_threadnotif_type type;
    union {
        struct {
            gh_result result;
            int id;
            const char * error_msg;
        } script;
        struct {
            const char * name;
            bool missing;
        } function;
    };
} gh_threadnotif;

struct gh_thread {
    gh_sandbox * sandbox;
    char name[GH_THREAD_MAXNAME];
    pid_t pid;
    gh_ipc ipc;
    gh_rpc rpc;
};

#ifndef GH_TYPEDEF_THREAD
typedef struct gh_thread gh_thread;
#define GH_TYPEDEF_THREAD
#endif

#define GH_THREAD_LUAINFO_TIMEOUTMS 1000

gh_result gh_sandbox_newthread(gh_sandbox * sandbox, gh_alloc * alloc, const char * name, gh_thread * out_thread);
gh_result gh_thread_dtor(gh_thread * thread);
gh_result gh_thread_process(gh_thread * thread, gh_threadnotif * notif);
gh_result gh_thread_requestquit(gh_thread * thread);
gh_result gh_thread_forcekill(gh_thread * thread);
gh_rpc * gh_thread_rpc(gh_thread * thread);

gh_result gh_thread_runstring(gh_thread * thread, const char * s, size_t s_len, int * script_id);

#endif
