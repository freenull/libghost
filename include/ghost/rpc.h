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

/** @brief RPC argument in trusted address space. */
typedef struct {
    /** @brief Pointer to argument data. */
    void * ptr;
    /** @brief Size of argument data. */
    size_t size;
} gh_rpcarg;

typedef struct gh_rpcfunction gh_rpcfunction;

/** @brief RPC call frame. */
typedef struct {
    /** @brief Current RPC function structure. */
    gh_rpcfunction * function;

    /** @brief Current thread. */
    gh_thread * thread;

    /** @brief File descriptor to return or -1. */
    int fd;

    /** @brief Result to return. */
    gh_result result;

    /** @brief Number of arguments. */
    size_t arg_count;
    /** @brief Arguments in trusted address space. */
    gh_rpcarg args[GH_IPCMSG_FUNCTIONCALL_MAXARGS];
    /** @brief Return value buffer in trusted address space. */
    gh_rpcarg return_arg;

    /** @brief Buffer used to hold remote arguments in trusted address space. */
    void * buffer;
    /** @brief Size of buffer used to hold remote arguments in trusted address space. */
    size_t buffer_size;
} gh_rpcframe;

typedef struct gh_rpc gh_rpc;

/** @brief RPC function callback. 
 *
 * @param rpc RPC instance.
 * @param frame RPC call frame. See: @ref gh_rpcframe.
 */
typedef void gh_rpcfunction_func(gh_rpc * rpc, gh_rpcframe * frame);

/** @brief RPC function thread safety mode. */
typedef enum {
    /** @brief Parallel calls to this function are not thread safe. */
    GH_RPCFUNCTION_THREADUNSAFELOCAL,
    /** @brief Parallel calls to all functions with this mode, including this one, are not thread safe. */
    GH_RPCFUNCTION_THREADUNSAFEGLOBAL,
    /** @brief Parallel calls to this function are safe. */
    GH_RPCFUNCTION_THREADSAFE
} gh_rpcfunction_threadsafety;

/** @brief Maximum size (with null terminator) of RPC function name. */
#define GH_RPCFUNCTION_MAXNAME GH_IPCMSG_FUNCTIONCALL_MAXNAME

struct gh_rpcfunction {
    gh_rpcfunction_threadsafety thread_safety;
    pthread_mutex_t mutex;

    char name[GH_RPCFUNCTION_MAXNAME];
    gh_rpcfunction_func * func;
};

#define GH_RPC_INITIALCAPACITY 128
#define GH_RPC_MAXCAPACITY GH_DYNAMICARRAY_NOMAXCAPACITY

/** @brief RPC registrar. */
struct gh_rpc {
    /** @brief Allocator. */
    gh_alloc * alloc;
    /** @brief Buffer of RPC functions. */
    gh_rpcfunction * buffer;
    /** @brief Size of buffer of RPC functions (registered function count). */
    size_t size;
    /** @brief Capacity of buffer of RPC functions. */
    size_t capacity;

    /** @brief Count of threads that reference this RPC registrar. */
    GH_ATOMIC_SIZE_T thread_refcount;

    /** @brief Mutex for RPC functions with thread safety mode @ref GH_RPCFUNCTION_THREADUNSAFEGLOBAL. */
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

/** @brief Construct a new RPC registrar.
 *
 * @param rpc      Pointer to unconstructed memory that will hold the new instance.
 * @param alloc    Allocator.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_rpc_ctor(gh_rpc * rpc, gh_alloc * alloc);

/** @brief Destroy an RPC registrar.
 *
 * @param rpc   Pointer to a constructed RPC registrar.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_rpc_dtor(gh_rpc * rpc);

/** @brief Register a new RPC function.
 *
 * @param rpc                 RPC registrar.
 * @param name                Name of the new RPC function.
 * @param func                Callback.
 * @param thread_safety       Thread safety mode.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_rpc_register(gh_rpc * rpc, const char * name, gh_rpcfunction_func * func, gh_rpcfunction_threadsafety thread_safety);

gh_result gh_rpc_newframe(gh_rpc * rpc, const char * name, gh_thread * thread, size_t arg_count, gh_rpcarg * args, gh_rpcarg return_arg, gh_rpcframe * out_frame);
gh_result gh_rpc_newframefrommsg(gh_rpc * rpc, gh_thread * thread, gh_ipcmsg_functioncall * msg, gh_rpcframe * out_frame);
gh_result gh_rpc_callframe(gh_rpc * rpc, gh_rpcframe * frame);
gh_result gh_rpc_respondtomsg(gh_rpc * rpc, gh_ipcmsg_functioncall * funccall_msg, gh_rpcframe * frame);
gh_result gh_rpc_respondmissing(gh_rpc * rpc, gh_ipc * ipc);
gh_result gh_rpc_disposeframe(gh_rpc * rpc, gh_rpcframe * frame);

/** @brief Retrieve RPC call frame argument.
 *
 * @param frame        RPC frame.
 * @param index        Argument index (0-15).
 * @param size         Size of argument data.
 * @param[out] out_ptr Will hold pointer to argument data.
 *
 * @return True if succeeded, otherwise false.
 */
bool gh_rpcframe_argv(gh_rpcframe * frame, size_t index, size_t size, void ** out_ptr);

/** @brief Retrieve RPC call frame argument.
 *
 * @note Argument data size is determined automatically by `sizeof(**(out_ptr))`.
 *
 * @param frame        RPC frame.
 * @param index        Argument index (0-15).
 * @param[out] out_ptr Will hold pointer to argument data.
 *
 * @return True if succeeded, otherwise false.
 */
#define gh_rpcframe_arg(frame, index, out_ptr) gh_rpcframe_argv((frame), (index), sizeof(**(out_ptr)), (void**)(out_ptr))

/** @brief Retrieve RPC call frame argument as a buffer of bytes.
 *
 * @param frame        RPC frame.
 * @param index        Argument index (0-15).
 * @param[out] out_ptr Will hold pointer to argument data.
 * @param[out] out_size Will hold the size of the buffer.
 *
 * @return True if succeeded, otherwise false.
 */
bool gh_rpcframe_argbufv(gh_rpcframe * frame, size_t index, void ** out_ptr, size_t * out_size);

/** @brief Retrieve RPC call frame argument as a buffer of bytes.
 *
 * @param frame        RPC frame.
 * @param index        Argument index (0-15).
 * @param[out] out_ptr Will hold pointer to argument data.
 * @param[out] out_size Will hold the size of the buffer.
 *
 * @return True if succeeded, otherwise false.
 */
#define gh_rpcframe_argbuf(frame, index, out_ptr, out_size) gh_rpcframe_argbufv((frame), (index), (void**)(out_ptr), (out_size))

/** @brief Set RPC call frame return value.
 *
 * @param frame        RPC frame.
 * @param ptr          Pointer to data to return.
 * @param size         Size of data to return.
 *
 * @return True if succeeded, otherwise false.
 */
bool gh_rpcframe_setreturnv(gh_rpcframe * frame, void * ptr, size_t size);

/** @brief Set RPC call frame return value.
 *
 * @param frame        RPC frame.
 * @param ptr          Pointer to data to return.
 * @param size         Size of data to return.
 *
 * @return True if succeeded, otherwise false.
 */
#define gh_rpcframe_setreturn(frame, ptr, size) gh_rpcframe_setreturnv((frame), (void*)(ptr), size)

/** @brief Set RPC call frame return value.
 *
 * @note Size is determined automatically by `sizeof(*(ptr))`.
 *
 * @param frame        RPC frame.
 * @param ptr          Pointer to data to return.
 *
 * @return True if succeeded, otherwise false.
 */
#define gh_rpcframe_setreturntyped(frame, ptr) gh_rpcframe_setreturnv((frame), (void*)(ptr), sizeof(*(ptr)))

/** @brief Set RPC call frame return value as buffer of bytes.
 *
 * @note Size of buffer is determined automatically by `sizeof(*(ptr)) * size`.
 *
 * @param frame        RPC frame.
 * @param ptr          Pointer to buffer to return.
 * @param size         Size (count) of buffer in bytes.
 *
 * @return True if succeeded, otherwise false.
 */
#define gh_rpcframe_setreturnbuftyped(frame, ptr, size) gh_rpcframe_setreturnv((frame), (void*)(ptr), sizeof(*(ptr)) * (size))

/** @brief Set return type and return instantly from caller.
 *
 * @param frame        RPC frame.
 * @param ptr          Pointer to data to return.
 * @param size         Size of data to return.
 *
 * @return True if succeeded, otherwise false.
 */
#define gh_rpcframe_returnhere(frame, ptr, size) { gh_rpcframe_setreturn(frame, ptr, size); return; }

/** @brief Set return type and return instantly from caller.
 *
 * @note Size is determined automatically by `sizeof(*(ptr))`.
 *
 * @param frame        RPC frame.
 * @param ptr          Pointer to data to return.
 *
 * @return True if succeeded, otherwise false.
 */
#define gh_rpcframe_returntypedhere(frame, ptr) { gh_rpcframe_setreturntyped(frame, ptr); return; }

/** @brief Set return type as buffer of bytes and return instantly from caller.
 *
 * @note Size of buffer is determined automatically by `sizeof(*(ptr)) * size`.
 *
 * @param frame        RPC frame.
 * @param ptr          Pointer to buffer to return.
 * @param size         Size (count) of buffer in bytes.
 *
 * @return True if succeeded, otherwise false.
 */
#define gh_rpcframe_returnbuftypedhere(frame, ptr, size) { gh_rpcframe_setreturnbuftyped(frame, ptr, size); return; }

/** @brief Set RPC call frame result code.
 *
 * @param frame     RPC frame.
 * @param result    Result code.
 */
void gh_rpcframe_setresult(gh_rpcframe * frame, gh_result result);

/** @brief Set RPC call frame result code and instantly return from caller.
 *
 * @param frame     RPC frame.
 * @param result    Result code.
 */
#define gh_rpcframe_failhere(frame, result) { gh_rpcframe_setresult(frame, result); return; }

/** @brief Set RPC call frame result code to argument error (`GHR_RPCF_ARG*`) and instantly return from caller.
 *
 * @param frame     RPC frame.
 * @param argnum    Argument index (0-15).
 */
#define gh_rpcframe_failarghere(frame, argnum) { gh_rpcframe_setresult(frame, GHR_RPCF_ARG ## argnum); return; }

/** @brief Set RPC call's returned file descriptor.
 *
 * @param frame     RPC frame.
 * @param fd        File descriptor.
 */
void gh_rpcframe_setreturnfd(gh_rpcframe * frame, int fd);

/** @brief Set RPC call's returned file descriptor and instantly return from caller.
 *
 * @param frame     RPC frame.
 * @param fd        File descriptor.
 */
#define gh_rpcframe_returnfdhere(frame, fd) { gh_rpcframe_setreturnfd(frame, fd); return; }

#ifdef __cplusplus
}
#endif

#endif

/** @} */
