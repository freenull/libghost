#ifndef GHOST_THREAD_H
#define GHOST_THREAD_H

#include <stdlib.h>
#include <ghost/result.h>
#include <ghost/ipc.h>
#include <ghost/sandbox.h>
#include <ghost/rpc.h>
#include <ghost/alloc.h>

#define GH_THREAD_MAXNAME 256

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

gh_result gh_sandbox_newthread(gh_sandbox * sandbox, gh_alloc * alloc, const char * name, gh_thread * out_thread);
gh_result gh_thread_dtor(gh_thread * thread);
gh_result gh_thread_process(gh_thread * thread);
gh_result gh_thread_requestquit(gh_thread * thread);
gh_result gh_thread_forcekill(gh_thread * thread);
gh_rpc * gh_thread_rpc(gh_thread * thread);

// will be one day replaced by executing lua code
gh_result gh_thread_runtestcase(gh_thread * thread, int index);

#endif
