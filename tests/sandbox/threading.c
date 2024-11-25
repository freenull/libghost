#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <ghost/alloc.h>
#include <ghost/ipc.h>
#include <ghost/sandbox.h>
#include <ghost/thread.h>
#include <ghost/rpc.h>
#include <pthread.h>

static pthread_mutex_t global_banner_mutex = PTHREAD_MUTEX_INITIALIZER;
#define GLOBAL_BANNER_MAXSIZE 256
static char global_banner[GLOBAL_BANNER_MAXSIZE];

static pthread_barrier_t threads_barrier;
#define THREADS_COUNT 5
static gh_thread threads[THREADS_COUNT];
static pthread_t pthreads[THREADS_COUNT];

static void func_printbanner(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;
    (void)frame;

    /* pthread_mutex_lock(&global_banner_mutex); */
    printf("[%d] PRINT BANNER: %.*s\n", frame->thread->pid, GLOBAL_BANNER_MAXSIZE, global_banner);
    /* pthread_mutex_unlock(&global_banner_mutex); */
}

static void func_setbanner(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * str_buf;
    size_t str_size;
    if (!gh_rpcframe_argbuf(frame, 0, &str_buf, &str_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }

    if (str_size > GLOBAL_BANNER_MAXSIZE) gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EFBIG));

    /* pthread_mutex_lock(&global_banner_mutex); */
    printf("[%d] SET BANNER: %.*s\n", frame->thread->pid, (int)str_size, str_buf);
    memcpy(global_banner, str_buf, str_size);
    /* pthread_mutex_unlock(&global_banner_mutex); */
}


static void * thread_callback(void * thread_voidp) {
    gh_thread * thread = (gh_thread *)thread_voidp;

    pthread_barrier_wait(&threads_barrier);

    char s[] =
        "local ghost = require('ghost')\n"
        "local ffi = require('ffi')\n"
        "ffi.cdef [[ typedef int32_t pid_t; pid_t getpid(void); ]]\n"
        "ghost.call('setbanner', nil, tostring(ffi.C.getpid()) .. ' was here!')\n"
        "ghost.call('printbanner', nil)\n"
        ;

    ghr_assert(gh_thread_runstringsync(thread, s, strlen(s), NULL));

    return NULL;
}

int main(void) {
    gh_sandbox sandbox;

    pthread_barrier_init(&threads_barrier, NULL, 5);

    gh_sandboxoptions options = {0};
    strcpy(options.name, "ghost-test-sandbox");
    options.memory_limit_bytes = GH_SANDBOX_NOLIMIT;
    options.functioncall_frame_limit_bytes = GH_SANDBOX_NOLIMIT;

    gh_alloc alloc = gh_alloc_default();

    ghr_assert(gh_sandbox_ctor(&sandbox, options));


    gh_rpc rpc;
    ghr_assert(gh_rpc_ctor(&rpc, &alloc, gh_permprompter_simpletui(STDIN_FILENO)));

    ghr_assert(gh_rpc_register(&rpc, "printbanner", func_printbanner, GH_RPCFUNCTION_THREADUNSAFEGLOBAL));
    ghr_assert(gh_rpc_register(&rpc, "setbanner", func_setbanner, GH_RPCFUNCTION_THREADUNSAFEGLOBAL));

    for (size_t i = 0; i < THREADS_COUNT; i++) {
        char name[strlen("thread") + 2];
        snprintf(name, strlen("thread") + 2, "thread%d", (int)i);
        name[strlen("thread") + 1] = '\0';

        ghr_assert(gh_sandbox_newthread(&sandbox, &rpc, name, name, GH_IPC_NOTIMEOUT, threads + i));
        assert(pthread_create(pthreads + i, NULL, thread_callback, threads + i) == 0);
    }

    for (size_t i = 0; i < THREADS_COUNT; i++) {
        void * ret;
        assert(pthread_join(pthreads[i], &ret) == 0);
    }
    
    for (size_t i = 0; i < THREADS_COUNT; i++) {
        gh_thread_dtor(threads + i, NULL);
    }

    ghr_assert(gh_rpc_dtor(&rpc));

    ghr_assert(gh_sandbox_dtor(&sandbox, NULL));


    return 0;
}
