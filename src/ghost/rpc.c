#define _GNU_SOURCE
#include <string.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <ghost/result.h>
#include <ghost/thread.h>
#include <ghost/ipc.h>
#include <ghost/rpc.h>
#include <ghost/alloc.h>
#include <ghost/dynamic_array.h>

static gh_result rpc_dtorelement_func(gh_dynamicarray da, void * elem, void * userdata) {
    (void)da;
    (void)userdata;

    gh_rpcfunction * func = (gh_rpcfunction *)elem;

    if (func->thread_safety == GH_RPCFUNCTION_THREADUNSAFELOCAL) {
        int pthread_res = pthread_mutex_destroy(&func->mutex);
        if (pthread_res != 0) return ghr_errnoval(GHR_RPC_MUTEXDESTROY, pthread_res);
    }
    return GHR_OK;
}

static const gh_dynamicarrayoptions rpc_daopts = {
    .initial_capacity = GH_RPC_INITIALCAPACITY,
    .max_capacity = GH_RPCFUNCTION_MAXNAME,
    .element_size = sizeof(gh_rpcfunction),
   
    .dtorelement_func = rpc_dtorelement_func,
    .userdata = NULL
};

gh_result gh_rpc_ctor(gh_rpc * rpc, gh_alloc * alloc) {
    rpc->alloc = alloc;
    atomic_store(&rpc->thread_refcount, 0);

    int pthread_res = pthread_mutex_init(&rpc->global_mutex, NULL);
    if (pthread_res != 0) return ghr_errnoval(GHR_RPC_GLOBALMUTEXINIT, pthread_res);

    return gh_dynamicarray_ctor(GH_DYNAMICARRAY(rpc), &rpc_daopts);
}

gh_result gh_rpc_dtor(gh_rpc * rpc) {
    int pthread_res = pthread_mutex_destroy(&rpc->global_mutex);
    if (pthread_res != 0) return ghr_errnoval(GHR_RPC_GLOBALMUTEXDESTROY, pthread_res);

    return gh_dynamicarray_dtor(GH_DYNAMICARRAY(rpc), &rpc_daopts);
}

gh_result gh_rpc_register(gh_rpc * rpc, const char * name, gh_rpcfunction_func * func, gh_rpcfunction_threadsafety thread_safety) {
    if (gh_rpc_isinuse(rpc)) return GHR_RPC_INUSE;

    gh_rpcfunction function = {0};
    strncpy(function.name, name, GH_RPCFUNCTION_MAXNAME - 1);
    function.name[GH_RPCFUNCTION_MAXNAME - 1] = '\0';
    function.func = func;

    function.thread_safety = thread_safety;

    gh_result res = gh_dynamicarray_append(GH_DYNAMICARRAY(rpc), &rpc_daopts, &function);
    if (ghr_iserr(res)) return res;

    if (thread_safety == GH_RPCFUNCTION_THREADUNSAFELOCAL) {
        gh_rpcfunction * function_entry = rpc->buffer + (rpc->size - 1);
        int pthread_res = pthread_mutex_init(&function_entry->mutex, NULL);
        if (pthread_res != 0) return ghr_errnoval(GHR_RPC_MUTEXINIT, pthread_res);
    }

    return GHR_OK;
}

gh_result gh_rpc_newframe(gh_rpc * rpc, const char * name, gh_thread * thread, size_t arg_count, gh_rpcarg * args, gh_rpcarg return_arg, gh_rpcframe * out_frame) {
    gh_rpcfunction * func = NULL;
    for (size_t i = 0; i < rpc->size; i++) {
        func = rpc->buffer + i;
        if (strcmp(func->name, name) == 0) break;
    }

    if (func == NULL) return GHR_RPC_MISSINGFUNC;

    gh_rpcframe frame = {0};
    frame.function = func;
    frame.thread = thread;
    frame.fd = -1;
    frame.result = GHR_RPC_UNEXECUTED;

    frame.arg_count = arg_count;
    memcpy(frame.args, args, GH_IPCMSG_FUNCTIONCALL_MAXARGS * sizeof(gh_rpcarg));
    frame.return_arg = return_arg;

    frame.buffer = NULL;
    frame.buffer_size = 0;
    
    *out_frame = frame;
    return GHR_OK;
}

gh_result gh_rpc_newframefrommsg(gh_rpc * rpc, gh_thread * thread, gh_ipcmsg_functioncall * msg, gh_rpcframe * out_frame) {
    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    gh_rpcfunction * func = NULL;
    for (size_t i = 0; i < rpc->size; i++) {
        func = rpc->buffer + i;
        if (strcmp(func->name, msg->name) == 0) break;
        func = NULL;
    }

    if (func == NULL) return GHR_RPC_MISSINGFUNC;

    gh_rpcframe frame = {0};

    frame.function = func;
    frame.thread = thread;
    frame.fd = -1;
    frame.result = GHR_RPC_UNEXECUTED;

    frame.buffer = NULL;
    frame.buffer_size = 0;

    size_t arg_count = msg->arg_count;
    if (arg_count > GH_IPCMSG_FUNCTIONCALL_MAXARGS) arg_count = 16;
    frame.arg_count = arg_count;

    size_t remote_iovec_count = 0;
    struct iovec remote_iovec[GH_IPCMSG_FUNCTIONCALL_MAXARGS + 1];

    size_t frame_buffer_size = 0;
    for (size_t i = 0; i < arg_count; i++) {
        frame_buffer_size += msg->args[i].size;

        remote_iovec_count += 1;
        remote_iovec[i].iov_base = (void*)msg->args[i].addr;
        remote_iovec[i].iov_len = msg->args[i].size;
    }

    if (msg->return_arg.addr != (uintptr_t)NULL && msg->return_arg.size != 0) {
        frame_buffer_size += msg->return_arg.size;
    }

    if (thread->sandbox->options.functioncall_frame_limit_bytes != GH_SANDBOX_NOLIMIT &&
        frame_buffer_size > thread->sandbox->options.functioncall_frame_limit_bytes) return GHR_RPC_LARGEFRAME;

    void * frame_buffer = NULL;

    if (frame_buffer_size > 0) {
        res = gh_alloc_new(rpc->alloc, (void**)&frame_buffer, frame_buffer_size);
        if (ghr_iserr(res)) return res;

        struct iovec local_iovec = { .iov_base = frame_buffer, .iov_len = frame_buffer_size - msg->return_arg.size };

        size_t frame_buffer_offset = 0;
        for (size_t i = 0; i < arg_count; i++) {
            frame.args[i].ptr = (char*)frame_buffer + frame_buffer_offset;
            frame.args[i].size = msg->args[i].size;
            frame_buffer_offset += msg->args[i].size;
        }

        if (msg->return_arg.addr != (uintptr_t)NULL && msg->return_arg.size != 0) {
            frame.return_arg.ptr = (char*)frame_buffer + frame_buffer_offset;
            frame.return_arg.size = msg->return_arg.size;
            frame_buffer_offset += msg->return_arg.size;
        } else {
            frame.return_arg.ptr = NULL;
            frame.return_arg.size = 0;
        }

        frame.buffer = frame_buffer;
        frame.buffer_size = frame_buffer_size;

        ssize_t readv_res = process_vm_readv(thread->pid, &local_iovec, 1, remote_iovec, remote_iovec_count, 0);
        if (readv_res < 0) {
            res = ghr_errno(GHR_RPC_ARGCOPYFAIL);
            goto fail_readv;
        }
    }
    
    *out_frame = frame;
    return res;

fail_readv:
    inner_res = gh_alloc_new(rpc->alloc, (void**)&frame_buffer, frame_buffer_size);
    if (ghr_iserr(inner_res)) res = inner_res;

    return res;
}

gh_result gh_rpc_callframe(gh_rpc * rpc, gh_rpcframe * frame) {
    if (frame->function == NULL) return GHR_RPC_FRAMEDISPOSED;
    if (frame->result != GHR_RPC_UNEXECUTED) return GHR_RPC_EXECUTED;

    if (frame->return_arg.size != 0 && frame->return_arg.ptr != NULL) {
        memset(frame->return_arg.ptr, 0, frame->return_arg.size);
    }

    if (frame->function->thread_safety == GH_RPCFUNCTION_THREADUNSAFELOCAL) {
        int pthread_res = pthread_mutex_lock(&frame->function->mutex);
        if (pthread_res != 0) return ghr_errnoval(GHR_RPC_MUTEXLOCK, pthread_res);
    } else if (frame->function->thread_safety == GH_RPCFUNCTION_THREADUNSAFEGLOBAL) {
        int pthread_res = pthread_mutex_lock(&rpc->global_mutex);
        if (pthread_res != 0) return ghr_errnoval(GHR_RPC_GLOBALMUTEXLOCK, pthread_res);
    }

    frame->result = GHR_OK;
    frame->function->func(rpc, frame);

    if (frame->function->thread_safety == GH_RPCFUNCTION_THREADUNSAFELOCAL) {
        int pthread_res = pthread_mutex_unlock(&frame->function->mutex);
        if (pthread_res != 0) return ghr_errnoval(GHR_RPC_MUTEXUNLOCK, pthread_res);
    } else if (frame->function->thread_safety == GH_RPCFUNCTION_THREADUNSAFEGLOBAL) {
        int pthread_res = pthread_mutex_unlock(&rpc->global_mutex);
        if (pthread_res != 0) return ghr_errnoval(GHR_RPC_GLOBALMUTEXUNLOCK, pthread_res);
    }

    return GHR_OK;
}

gh_result gh_rpc_respondtomsg(gh_rpc * rpc, gh_ipcmsg_functioncall * funccall_msg, gh_rpcframe * frame) {
    (void)rpc;

    if (frame->function == NULL) return GHR_RPC_FRAMEDISPOSED;
    if (frame->result == GHR_RPC_UNEXECUTED) return GHR_RPC_UNEXECUTED;

    if (ghr_isok(frame->result) && frame->return_arg.size != 0 && frame->return_arg.ptr != NULL) {
        struct iovec local_iovec = { .iov_base = frame->return_arg.ptr, .iov_len = frame->return_arg.size };
        struct iovec remote_iovec = { .iov_base = (void*)funccall_msg->return_arg.addr, .iov_len = frame->return_arg.size };
        ssize_t writev_res = process_vm_writev(frame->thread->pid, &local_iovec, 1, &remote_iovec, 1, 0);
        if (writev_res < 0) return ghr_errno(GHR_RPC_RETURNCOPYFAIL);
    }

    gh_ipcmsg_functionreturn ret_msg = {0};
    ret_msg.type = GH_IPCMSG_FUNCTIONRETURN;
    ret_msg.result = frame->result;
    ret_msg.fd = frame->fd;
    if (!ghr_isok(frame->result)) ret_msg.fd = -1;

    gh_result res = gh_ipc_send(&frame->thread->ipc, (gh_ipcmsg*)&ret_msg, sizeof(gh_ipcmsg_functionreturn));
    if (ghr_iserr(res)) {
        // If we failed here, the remote process will be stuck waiting for a LUARETURN
        // The reason for the failure may concern *specifically* the file descriptor
        // If it does, we try to send a message again without the FD.
        if (ghr_is(res, GHR_IPC_SENDMSGFAIL) && ghr_frag_errno(res) == EBADF) {
            ret_msg.result = GHR_RPC_INVALIDFD;
            ret_msg.fd = -1;
            frame->fd = -1; // prevent trying to close the invalid FD
            res = gh_ipc_send(&frame->thread->ipc, (gh_ipcmsg*)&ret_msg, sizeof(gh_ipcmsg_functionreturn));
        }

        if (ghr_iserr(res)) return res;
    }

    if (frame->fd >= 0) {
        int close_res = close(frame->fd);
        if (close_res < 0) return ghr_errno(GHR_RPC_CLOSEFD);
    }

    return GHR_OK;
}

gh_result gh_rpc_respondmissing(gh_rpc * rpc, gh_ipc * ipc) {
    (void)rpc;

    gh_ipcmsg_functionreturn ret_msg = {0};
    ret_msg.type = GH_IPCMSG_FUNCTIONRETURN;
    ret_msg.result = GHR_RPC_MISSINGFUNC;
    ret_msg.fd = -1;

    gh_result res = gh_ipc_send(ipc, (gh_ipcmsg*)&ret_msg, sizeof(gh_ipcmsg_functionreturn));
    if (ghr_iserr(res)) return res;
    return GHR_OK;
}

gh_result gh_rpc_disposeframe(gh_rpc * rpc, gh_rpcframe * frame) {
    if (frame->buffer != NULL) {
        gh_result res = gh_alloc_delete(rpc->alloc, &frame->buffer, frame->buffer_size);
        if (ghr_iserr(res)) return res;
    }

    frame->function = NULL;

    return GHR_OK;
}

bool gh_rpcframe_argv(gh_rpcframe * frame, size_t index, size_t size, void ** out_ptr) {
    if (frame->args[index].size != size) return false;
    if (index >= frame->arg_count) {
        if (out_ptr != NULL) *out_ptr = NULL;
        return false;
    }

    if (out_ptr != NULL) *out_ptr = frame->args[index].ptr;
    return true;
}

bool gh_rpcframe_argbufv(gh_rpcframe * frame, size_t index, void ** out_ptr, size_t * out_size) {
    if (index >= frame->arg_count) {
        if (out_ptr != NULL) *out_ptr = NULL;
        if (out_size != NULL) *out_size = 0;
        return false;
    }

    if (out_ptr != NULL) *out_ptr = frame->args[index].ptr;
    if (out_size != NULL) *out_size = frame->args[index].size;
    return true;
}

bool gh_rpcframe_setreturnv(gh_rpcframe * frame, void * ptr, size_t size) {
    if (frame->return_arg.size == 0) return false;
    if (frame->return_arg.ptr == NULL) return false;
    if (frame->return_arg.size < size) {
        frame->result = GHR_RPC_RETURNSIZE;
        return false;
    }

    memcpy(frame->return_arg.ptr, ptr, size);
    return true;
}

void gh_rpcframe_setresult(gh_rpcframe * frame, gh_result result) {
    frame->result = result;
}

void gh_rpcframe_setreturnfd(gh_rpcframe * frame, int fd) {
    frame->fd = fd;
}
