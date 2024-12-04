/** @defgroup rpc RPC
 *
 * @brief Object responsible for the registration and execution of remote procedure calls (calls into the trusted host process from the untrusted subjail).
 *
 * @{
 */

#ifndef GHOST_RPC_H
#define GHOST_RPC_H

#include <stdatomic.h>
#include <ghost/ipc.h>
#include <ghost/dynamic_array.h>
#include <ghost/perms/prompt.h>
#include <pthread.h>

#ifdef __cplusplus
#include <atomic>
#define GH_ATOMIC_OP(op) std::atomic_ ## op
#define GH_ATOMIC_SIZE_T std::atomic<size_t>
extern "C" {
#else
#define GH_ATOMIC_OP(op) atomic_ ## op
#define GH_ATOMIC_SIZE_T atomic_size_t
#endif

#ifndef GH_TYPEDEF_THREAD
typedef struct gh_thread gh_thread;
#define GH_TYPEDEF_THREAD
#endif

typedef struct {
    void * ptr;
    size_t size;
} gh_rpcarg;

typedef struct gh_rpcfunction gh_rpcfunction;

typedef struct {
    gh_rpcfunction * function;
    gh_thread * thread;
    int fd;
    gh_result result;

    size_t arg_count;
    gh_rpcarg args[GH_IPCMSG_FUNCTIONCALL_MAXARGS];
    gh_rpcarg return_arg;

    void * buffer;
    size_t buffer_size;
} gh_rpcframe;

typedef struct gh_rpc gh_rpc;

typedef void gh_rpcfunction_func(gh_rpc * rpc, gh_rpcframe * frame);

typedef enum {
    GH_RPCFUNCTION_THREADUNSAFELOCAL,
    GH_RPCFUNCTION_THREADUNSAFEGLOBAL,
    GH_RPCFUNCTION_THREADSAFE
} gh_rpcfunction_threadsafety;

#define GH_RPCFUNCTION_MAXNAME GH_IPCMSG_FUNCTIONCALL_MAXNAME

struct gh_rpcfunction {
    gh_rpcfunction_threadsafety thread_safety;
    pthread_mutex_t mutex;

    char name[GH_RPCFUNCTION_MAXNAME];
    gh_rpcfunction_func * func;
};

#define GH_RPC_INITIALCAPACITY 128
#define GH_RPC_MAXCAPACITY GH_DYNAMICARRAY_NOMAXCAPACITY

struct gh_rpc {
    gh_alloc * alloc;
    gh_rpcfunction * buffer;
    size_t size;
    size_t capacity;
    GH_ATOMIC_SIZE_T thread_refcount;
    pthread_mutex_t global_mutex;
};

__attribute__((always_inline))
static inline void gh_rpc_incthreadrefcount(gh_rpc * rpc) {
    GH_ATOMIC_OP(fetch_add)(&rpc->thread_refcount, 1);
}

__attribute__((always_inline))
static inline void gh_rpc_decthreadrefcount(gh_rpc * rpc) {
    GH_ATOMIC_OP(fetch_sub)(&rpc->thread_refcount, 1);
}

__attribute__((always_inline))
static inline bool gh_rpc_isinuse(gh_rpc * rpc) {
    return GH_ATOMIC_OP(load)(&rpc->thread_refcount) > 0;
}

gh_result gh_rpc_ctor(gh_rpc * rpc, gh_alloc * alloc);
gh_result gh_rpc_dtor(gh_rpc * rpc);
gh_result gh_rpc_register(gh_rpc * rpc, const char * name, gh_rpcfunction_func * func, gh_rpcfunction_threadsafety thread_safety);
gh_result gh_rpc_newframe(gh_rpc * rpc, const char * name, gh_thread * thread, size_t arg_count, gh_rpcarg * args, gh_rpcarg return_arg, gh_rpcframe * out_frame);
gh_result gh_rpc_newframefrommsg(gh_rpc * rpc, gh_thread * thread, gh_ipcmsg_functioncall * msg, gh_rpcframe * out_frame);
gh_result gh_rpc_callframe(gh_rpc * rpc, gh_rpcframe * frame);
gh_result gh_rpc_respondtomsg(gh_rpc * rpc, gh_ipcmsg_functioncall * funccall_msg, gh_rpcframe * frame);
gh_result gh_rpc_respondmissing(gh_rpc * rpc, gh_ipc * ipc);
gh_result gh_rpc_disposeframe(gh_rpc * rpc, gh_rpcframe * frame);

bool gh_rpcframe_argv(gh_rpcframe * frame, size_t index, size_t size, void ** out_ptr);
#define gh_rpcframe_arg(frame, index, out_ptr) gh_rpcframe_argv((frame), (index), sizeof(**(out_ptr)), (void**)(out_ptr))
bool gh_rpcframe_argbufv(gh_rpcframe * frame, size_t index, void ** out_ptr, size_t * out_size);
#define gh_rpcframe_argbuf(frame, index, out_ptr, out_size) gh_rpcframe_argbufv((frame), (index), (void**)(out_ptr), (out_size))
bool gh_rpcframe_setreturnv(gh_rpcframe * frame, void * ptr, size_t size);
#define gh_rpcframe_setreturn(frame, ptr, size) gh_rpcframe_setreturnv((frame), (void*)(ptr), size)
#define gh_rpcframe_setreturntyped(frame, ptr) gh_rpcframe_setreturnv((frame), (void*)(ptr), sizeof(*(ptr)))
#define gh_rpcframe_setreturnbuftyped(frame, ptr, size) gh_rpcframe_setreturnv((frame), (void*)(ptr), sizeof(*(ptr)) * (size))

#define gh_rpcframe_returnhere(frame, ptr, size) { gh_rpcframe_setreturn(frame, ptr, size); return; }
#define gh_rpcframe_returntypedhere(frame, ptr) { gh_rpcframe_setreturntyped(frame, ptr); return; }
#define gh_rpcframe_returnbuftypedhere(frame, ptr, size) { gh_rpcframe_setreturnbuftyped(frame, ptr, size); return; }

void gh_rpcframe_setresult(gh_rpcframe * frame, gh_result result);
#define gh_rpcframe_failhere(frame, result) { gh_rpcframe_setresult(frame, result); return; }
#define gh_rpcframe_failarghere(frame, argnum) { gh_rpcframe_setresult(frame, GHR_RPCF_ARG ## argnum); return; }

void gh_rpcframe_setreturnfd(gh_rpcframe * frame, int fd);
#define gh_rpcframe_returnfdhere(frame, fd) { gh_rpcframe_setreturnfd(frame, fd); return; }

#ifdef __cplusplus
}
#endif

#endif

/** @} */
