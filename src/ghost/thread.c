#define _GNU_SOURCE
#include <time.h>
#include <sys/poll.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <ghost/result.h>
#include <ghost/sandbox.h>
#include <ghost/thread.h>
#include <ghost/rpc.h>
#include <ghost/ipc.h>
#include <ghost/perms/perms.h>
#include <ghost/perms/prompt.h>

gh_result gh_thread_ctor(gh_thread * thread, gh_threadoptions options) {
    gh_ipc direct_ipc;
    int direct_peerfd;

    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    res = gh_perms_ctor(&thread->perms, options.rpc->alloc, options.prompter);
    if (ghr_iserr(res)) return res;

    res = gh_ipc_ctor(&direct_ipc, &direct_peerfd);
    if (ghr_iserr(res)) goto fail_ipc;

    gh_ipcmsg_newsubjail newsubjail_msg;
    memset(&newsubjail_msg, 0, sizeof(gh_ipcmsg_newsubjail));
    newsubjail_msg.type = GH_IPCMSG_NEWSUBJAIL;
    newsubjail_msg.sockfd = direct_peerfd;
    res = gh_ipc_send(&options.sandbox->ipc, (gh_ipcmsg*)&newsubjail_msg, sizeof(gh_ipcmsg_newsubjail));
    if (ghr_iserr(res)) goto fail_send;

    char recvmsg_buf[GH_IPCMSG_MAXSIZE];
    gh_ipcmsg * recvmsg = (gh_ipcmsg *)recvmsg_buf;
    res = gh_ipc_recv(&direct_ipc, recvmsg, GH_SANDBOX_MAXWAITSUBJAILMS);
    if (ghr_iserr(res)) goto fail_recv;
    if (recvmsg->type != GH_IPCMSG_SUBJAILALIVE) {
        res = GHR_SANDBOX_EXPECTEDSUBJAILALIVE;
        goto fail_recv;
    }

    gh_ipcmsg_subjailalive * subjailalive_msg = (gh_ipcmsg_subjailalive * )recvmsg;
    pid_t subjail_pid = subjailalive_msg->pid;
    
    int close_res = close(direct_peerfd);
    if (close_res < 0) {
        res = ghr_errno(GHR_SANDBOX_THREADCLOSESOCKFAIL);
        goto fail_close;
    }

    gh_ipcmsg_hello hello_msg;
    memset(&hello_msg, 0, sizeof(gh_ipcmsg_hello));
    hello_msg.type = GH_IPCMSG_HELLO;
    hello_msg.pid = getpid();
    res = gh_ipc_send(&direct_ipc, (gh_ipcmsg*)&hello_msg, sizeof(gh_ipcmsg_hello));
    if (ghr_iserr(res)) goto fail_hello;

    thread->ipc = direct_ipc;
    thread->pid = subjail_pid;
    memcpy(thread->name, options.name, GH_THREAD_MAXNAME);
    thread->sandbox = options.sandbox;

    memcpy(thread->safe_id, options.safe_id, GH_THREAD_MAXSAFEID);

    thread->userdata = NULL;

    thread->rpc = options.rpc;
    gh_rpc_incthreadrefcount(options.rpc);

    thread->default_timeout_ms = options.default_timeout_ms;

    return res;

fail_hello:
fail_close:
    if (kill(subjail_pid, SIGKILL) < 0) res = ghr_errno(GHR_SANDBOX_THREADRECOVERYKILLFAIL);
fail_recv:
fail_send:
    inner_res = gh_ipc_dtor(&direct_ipc);
    if (ghr_iserr(inner_res)) res = inner_res;

fail_ipc:
    return res;
}

static gh_result thread_wait(gh_thread * thread, int timeout_ms) {
    int pidfd = (int)syscall(SYS_pidfd_open, thread->pid, 0);
    if (pidfd < 0) return ghr_errno(GHR_SANDBOX_PIDFD);

    struct pollfd pollfd[2] = {
        {
            .fd = pidfd,
            .events = POLLHUP | POLLIN,
            .revents = 0
        },

        {
            .fd = thread->ipc.sockfd,
            .events = POLLHUP | POLLIN,
            .revents = 0
        }
    };
    nfds_t pollfd_count = 2;

    gh_result res = GHR_OK;

    struct timespec start_time;
    if (clock_gettime(CLOCK_MONOTONIC, &start_time) < 0) {
        // clock is not going to work
        // we fallback to just polling the pidfd
        // the subjail process will crash if it attempts to run RPC functions

        pollfd_count = 1;
        res = ghr_errno(GHR_THREAD_WAITCLOCK);
    }

    while (true) {
        int pollres = poll(pollfd, pollfd_count, timeout_ms);
        if (pollres < 0) return ghr_errno(GHR_SANDBOX_PIDFDPOLL);

        if (pollres == 0) {
            if (syscall(SYS_pidfd_send_signal, pidfd, SIGKILL, NULL, 0) < 0) return ghr_errno(GHR_SANDBOX_PIDFDKILL);
            return GHR_THREAD_FORCEKILL;
        } else {
            if (pollfd[0].revents & (POLLIN | POLLHUP | POLLERR)) {
                return res;
            }

            struct timespec poll_end_time;
            if (pollfd_count >= 2) {
                if (clock_gettime(CLOCK_MONOTONIC, &poll_end_time) < 0) {
                    pollfd_count = 1;
                    res = ghr_errno(GHR_THREAD_WAITCLOCK);
                } else {
                    int64_t msec_diff = (poll_end_time.tv_sec - start_time.tv_sec) * 1000 + (poll_end_time.tv_nsec - start_time.tv_nsec) / 1000000;
                    timeout_ms -= (int)msec_diff;

                    if (timeout_ms < 0) timeout_ms = 0;

                    start_time = poll_end_time;
                }
            }

            if (pollfd_count >= 2 && (pollfd[1].revents & (POLLIN | POLLHUP | POLLERR))) {
                gh_threadnotif notif;
                gh_result inner_res = gh_thread_process(thread, &notif);
                if (ghr_is(inner_res, GHR_IPC_PEERSHUTDOWN)) {
                    pollfd_count = 1;
                    inner_res = GHR_OK;
                }
                if (ghr_iserr(inner_res)) res = inner_res;

                // When we give the script an alloted time to shutdown before we force kill it,
                // we do NOT want to count the time that it takes **us** to process messages from
                // the thread.
                // For example, if a destructor on a Lua object calls a function that ends up
                // triggering a permission prompt, we absolutely do not want to kill the process
                // just because the user took a little bit too long to answer.
                // For this reason, we reset the start time after processing the message here,
                // so that the difference calculated between the time pre-poll and post-poll (that
                // determines the poll timeout_ms argument) will not include the time spent in this
                // branch.
                if (clock_gettime(CLOCK_MONOTONIC, &poll_end_time) < 0) {
                    pollfd_count = 1;
                    res = ghr_errno(GHR_THREAD_WAITCLOCK);
                } else {
                    start_time = poll_end_time;
                }
            }
        }
    }
}

static gh_result thread_requestquit(gh_thread * thread) {
    gh_ipcmsg_quit quit_msg;
    memset(&quit_msg, 0, sizeof(gh_ipcmsg_quit));
    quit_msg.type = GH_IPCMSG_QUIT;
    gh_result res = gh_ipc_send(&thread->ipc, (gh_ipcmsg*)&quit_msg, sizeof(gh_ipcmsg_quit));

    // if peer is already dead
    if (ghr_is(res, GHR_IPC_PEERSHUTDOWN)) return GHR_OK;
    if (ghr_iserr(res)) return res;

    res = thread_wait(thread, GH_SANDBOX_TIMETOQUITMS);
    if (ghr_iserr(res)) return res;

    return res;
}


gh_result gh_thread_dtor(gh_thread * thread, gh_result * out_subjailresult) {
    if (thread->pid == 0) return GHR_OK;

    gh_rpc_decthreadrefcount(thread->rpc);

    gh_result quit_res = thread_requestquit(thread);
    if (out_subjailresult != NULL) *out_subjailresult = quit_res;

    gh_result res = gh_perms_dtor(&thread->perms);
    if (ghr_iserr(res)) return res;

    res = gh_ipc_dtor(&thread->ipc);
    if (ghr_iserr(res)) return res;
    
    return GHR_OK;
}

gh_result gh_thread_attachuserdata(gh_thread * thread, void * userdata) {
    thread->userdata = userdata;
    return GHR_OK;
}

static gh_result thread_handlemsg_functioncall(gh_thread * thread, gh_ipcmsg_functioncall * msg) {
    gh_rpc * rpc = gh_thread_rpc(thread);
    gh_rpcframe frame;
    gh_result res = gh_rpc_newframefrommsg(rpc, thread, msg, &frame);
    if (ghr_is(res, GHR_RPC_MISSINGFUNC)) {
        gh_result inner_res = gh_rpc_respondmissing(rpc, &thread->ipc);
        if (ghr_iserr(inner_res)) return inner_res;
    }
    if (ghr_iserr(res)) return res;

    res = gh_rpc_callframe(rpc, &frame);
    if (ghr_iserr(res)) return res;

    res = gh_rpc_respondtomsg(rpc, msg, &frame);
    if (ghr_iserr(res)) return res;

    res = gh_rpc_disposeframe(rpc, &frame);
    if (ghr_iserr(res)) return res;

    return res;
}

static gh_result thread_handlemsg(gh_thread * thread, gh_ipcmsg * msg, gh_threadnotif * notif) {
    (void)thread;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
    // RATIONALE: Only a select few messages should ever be received by threads for security reasons.

    switch(msg->type) {
    case GH_IPCMSG_FUNCTIONCALL:
        if (notif != NULL) notif->type = GH_THREADNOTIF_FUNCTIONCALLED;
        gh_ipcmsg_functioncall * call_msg = (gh_ipcmsg_functioncall *)msg;
        if (notif != NULL) {
            strncpy(notif->function.name, call_msg->name, GH_IPCMSG_FUNCTIONCALL_MAXNAME);
            notif->function.name[GH_IPCMSG_FUNCTIONCALL_MAXNAME - 1] = '\0';
        }
        gh_result res = thread_handlemsg_functioncall(thread, call_msg);
        if (ghr_is(res, GHR_RPC_MISSINGFUNC)) {
            if (notif != NULL) notif->function.missing = true;
            return GHR_OK;
        }

        return res;

    case GH_IPCMSG_LUARESULT:
        if (notif != NULL) {
            notif->type = GH_THREADNOTIF_SCRIPTRESULT;
            gh_ipcmsg_luaresult * result_msg = (gh_ipcmsg_luaresult *)msg;
            notif->script.result = result_msg->result;
            notif->script.id = result_msg->script_id;
            strncpy(notif->script.error_msg, result_msg->error_msg, GH_THREADNOTIF_SCRIPT_ERRORMSGMAX);
            notif->script.error_msg[GH_THREADNOTIF_SCRIPT_ERRORMSGMAX - 1] = '\0';
            notif->script.call_return_ptr = result_msg->return_ptr;
            printf("RECV return_ptr %zx\n", notif->script.call_return_ptr);
        }
        return GHR_OK;

    default:
        return GHR_THREAD_UNKNOWNMESSAGE;
    }
#pragma GCC diagnostic pop

/*     return GHR_OK; */
}

gh_result gh_thread_process(gh_thread * thread, gh_threadnotif * notif) {
    GH_IPCMSG_BUFFER(msg_buf);

    gh_ipcmsg * msg = (gh_ipcmsg *)msg_buf;

    gh_result res = gh_ipc_recv(&thread->ipc, msg, thread->default_timeout_ms);
    if (ghr_iserr(res)) return res;

    return thread_handlemsg(thread, msg, notif);
}

inline gh_rpc * gh_thread_rpc(gh_thread * thread) {
    return thread->rpc;
}

gh_result gh_thread_runstring(gh_thread * thread, const char * s, size_t s_len, int * script_id) {
    if (s_len > GH_IPCMSG_LUASTRING_MAXSIZE - 1) {
        return GHR_THREAD_LARGESTRING;
    }

    gh_ipcmsg_luastring msg = {0};
    msg.type = GH_IPCMSG_LUASTRING;
    strncpy(msg.content, s, s_len);
    msg.content[s_len] = '\0';

    gh_result res = gh_ipc_send(&thread->ipc, (gh_ipcmsg*)&msg, sizeof(gh_ipcmsg_luastring));
    if (ghr_iserr(res)) return res;

    GH_IPCMSG_BUFFER(response_msgbuf);
    res = gh_ipc_recv(&thread->ipc, (gh_ipcmsg *)response_msgbuf, GH_THREAD_LUAINFO_TIMEOUTMS);
    if (ghr_iserr(res)) return res;
    if (((gh_ipcmsg *)response_msgbuf)->type != GH_IPCMSG_LUAINFO) return GHR_THREAD_EXPECTEDLUAINFO;

    if (script_id != NULL) *script_id = ((gh_ipcmsg_luainfo *)response_msgbuf)->script_id;
    return GHR_OK;
}

static gh_result thread_syncscript(gh_thread * thread, int script_id, gh_threadnotif_script * out_status) {
    if (out_status != NULL) *out_status = (gh_threadnotif_script) {0};
    gh_result res = GHR_OK;

    while (true) {
        gh_threadnotif notif = {0};
        res = gh_thread_process(thread, &notif);
        if (ghr_iserr(res)) return res;

        if (notif.type == GH_THREADNOTIF_SCRIPTRESULT && notif.script.id == script_id) {
            if (out_status != NULL) *out_status = notif.script;
            break;
        }
    }

    return res;
}

gh_result gh_thread_runstringsync(gh_thread * thread, const char * s, size_t s_len, gh_threadnotif_script * out_status) {
    int script_id = -1;
    gh_result res = gh_thread_runstring(thread, s, s_len, &script_id);
    if (ghr_iserr(res)) return res;

    return thread_syncscript(thread, script_id, out_status);
}

gh_result gh_thread_runfile(gh_thread * thread, int fd, int * script_id) {
    gh_ipcmsg_luafile msg = {0};
    msg.type = GH_IPCMSG_LUAFILE;
    msg.fd = fd;
    strncpy(msg.chunk_name, thread->safe_id, GH_IPCMSG_LUAFILE_CHUNKNAMEMAX);
    msg.chunk_name[GH_IPCMSG_LUAFILE_CHUNKNAMEMAX - 1] = '\0';

    gh_result res = gh_ipc_send(&thread->ipc, (gh_ipcmsg*)&msg, sizeof(gh_ipcmsg_luafile));
    if (ghr_iserr(res)) return res;

    GH_IPCMSG_BUFFER(response_msgbuf);
    res = gh_ipc_recv(&thread->ipc, (gh_ipcmsg *)response_msgbuf, GH_THREAD_LUAINFO_TIMEOUTMS);
    if (ghr_iserr(res)) return res;
    if (((gh_ipcmsg *)response_msgbuf)->type != GH_IPCMSG_LUAINFO) return GHR_THREAD_EXPECTEDLUAINFO;

    if (script_id != NULL) *script_id = ((gh_ipcmsg_luainfo *)response_msgbuf)->script_id;
    return GHR_OK;
}

gh_result gh_thread_runfilesync(gh_thread * thread, int fd, gh_threadnotif_script * out_status) {
    int script_id = -1;
    gh_result res = gh_thread_runfile(thread, fd, &script_id);
    if (ghr_iserr(res)) return res;

    return thread_syncscript(thread, script_id, out_status);
}

static gh_result thread_sethostvariable(gh_thread * thread, const char * name, const int table_index, gh_ipcmsg_luahostvariable * msg, int * out_script_id) {
    if (strlen(name) >= GH_IPCMSG_LUAHOSTVARIABLE_NAMEMAX - 1) {
        return GHR_THREAD_LARGEHOSTVARNAME;
    }
    strcpy(msg->name, name);

    msg->table_index = table_index;

    gh_result res = gh_ipc_send(&thread->ipc, (gh_ipcmsg*)msg, sizeof(gh_ipcmsg_luahostvariable));
    if (ghr_iserr(res)) return res;

    GH_IPCMSG_BUFFER(response_msgbuf);
    res = gh_ipc_recv(&thread->ipc, (gh_ipcmsg *)response_msgbuf, GH_THREAD_LUAINFO_TIMEOUTMS);
    if (ghr_iserr(res)) return res;
    if (((gh_ipcmsg *)response_msgbuf)->type != GH_IPCMSG_LUAINFO) return GHR_THREAD_EXPECTEDLUAINFO;

    if (out_script_id != NULL) *out_script_id = ((gh_ipcmsg_luainfo *)response_msgbuf)->script_id;
    return GHR_OK;
}

gh_result gh_thread_setint(gh_thread * thread, const char * name, int value) {
    gh_ipcmsg_luahostvariable msg = {
        .type = GH_IPCMSG_LUAHOSTVARIABLE,
        .datatype = GH_IPCMSG_LUAHOSTVARIABLE_INT,
    };
    msg.t_integer = value;

    int script_id = -1;
    gh_result res = thread_sethostvariable(thread, name, 0, &msg, &script_id);
    if (ghr_iserr(res)) return res;
    return thread_syncscript(thread, script_id, NULL);
}


gh_result gh_thread_setdouble(gh_thread * thread, const char * name, double value) {
    gh_ipcmsg_luahostvariable msg = {
        .type = GH_IPCMSG_LUAHOSTVARIABLE,
        .datatype = GH_IPCMSG_LUAHOSTVARIABLE_DOUBLE,
    };
    msg.t_double = value;

    int script_id = -1;
    gh_result res = thread_sethostvariable(thread, name, 0, &msg, &script_id);
    if (ghr_iserr(res)) return res;
    return thread_syncscript(thread, script_id, NULL);
}

static gh_result thread_setlstring_table(gh_thread * thread, const char * name, const char * string, size_t len, int table_index) {
    if (len >= GH_IPCMSG_LUAHOSTVARIABLE_STRINGMAX - 1) {
        return GHR_THREAD_LARGEHOSTVARSTRING;
    }

    gh_ipcmsg_luahostvariable msg = {
        .type = GH_IPCMSG_LUAHOSTVARIABLE,
        .datatype = GH_IPCMSG_LUAHOSTVARIABLE_STRING,
    };
    msg.t_string.len = len;
    strncpy(msg.t_string.buffer, string, len);

    int script_id = -1;
    gh_result res = thread_sethostvariable(thread, name, table_index, &msg, &script_id);
    if (ghr_iserr(res)) return res;
    return thread_syncscript(thread, script_id, NULL);
}

gh_result gh_thread_setlstring(gh_thread * thread, const char * name, const char * string, size_t len) {
    return thread_setlstring_table(thread, name, string, len, 0);
}

gh_result gh_thread_setstring(gh_thread * thread, const char * name, const char * string) {
    return gh_thread_setlstring(thread, name, string, strlen(string));
}

gh_result gh_thread_setstringtable(gh_thread * thread, const char * name, const char * const * strings, int count) {
    gh_result res = GHR_OK;
    for (int i = 0; i < count; i++) {
        res = thread_setlstring_table(thread, name, strings[i], strlen(strings[i]), i + 1);
    }
    return res;
}

gh_result gh_thread_callframe_ctor(gh_thread_callframe * frame) {
    *frame = (gh_thread_callframe) {0};

    gh_result res = gh_fdmem_ctor(&frame->fdmem);
    if (ghr_iserr(res)) return res;

    frame->param_count = 0;
    frame->returned = false;

    return res;
}

gh_result gh_thread_callframe_int(gh_thread_callframe * frame, int value) {
    static const size_t size = sizeof(gh_variant);

    gh_variant * variant;
    gh_result res = gh_fdmem_new(&frame->fdmem, size, (void**)&variant);
    if (ghr_iserr(res)) return res;

    variant->type = GH_VARIANT_INT;
    variant->t_int = value;

    if (frame->param_count + 1 > GH_IPCMSG_LUACALL_MAXPARAMS) {
        return GHR_THREAD_TOOMANYARGS;
    }

    gh_fdmem_ptr virt_ptr = gh_fdmem_virtptr(&frame->fdmem, (void*)variant, size);
    if (virt_ptr == 0) return GHR_THREAD_FDMEMNULL;

    frame->param_ptrs[frame->param_count] = virt_ptr;
    frame->param_count += 1;

    return GHR_OK;
}

gh_result gh_thread_callframe_double(gh_thread_callframe * frame, double value) {
    static const size_t size = sizeof(gh_variant);

    gh_variant * variant;
    gh_result res = gh_fdmem_new(&frame->fdmem, size, (void**)&variant);
    if (ghr_iserr(res)) return res;

    variant->type = GH_VARIANT_DOUBLE;
    variant->t_double = value;

    if (frame->param_count + 1 > GH_IPCMSG_LUACALL_MAXPARAMS) {
        return GHR_THREAD_TOOMANYARGS;
    }

    gh_fdmem_ptr virt_ptr = gh_fdmem_virtptr(&frame->fdmem, (void*)variant, size);
    if (virt_ptr == 0) return GHR_THREAD_FDMEMNULL;

    frame->param_ptrs[frame->param_count] = virt_ptr;
    frame->param_count += 1;

    return GHR_OK;
}

gh_result gh_thread_callframe_lstring(gh_thread_callframe * frame, size_t size, const char * string) {
    const size_t variant_size = sizeof(gh_variant) + size;

    gh_variant * variant;
    gh_result res = gh_fdmem_new(&frame->fdmem, variant_size, (void**)&variant);
    if (ghr_iserr(res)) return res;

    variant->type = GH_VARIANT_STRING;
    variant->t_string_len = size;
    strncpy(variant->t_string_data, string, size);

    if (frame->param_count + 1 > GH_IPCMSG_LUACALL_MAXPARAMS) {
        return GHR_THREAD_TOOMANYARGS;
    }

    gh_fdmem_ptr virt_ptr = gh_fdmem_virtptr(&frame->fdmem, (void*)variant, variant_size);
    if (virt_ptr == 0) return GHR_THREAD_FDMEMNULL;

    frame->param_ptrs[frame->param_count] = virt_ptr;
    frame->param_count += 1;

    return GHR_OK;
}

gh_result gh_thread_callframe_string(gh_thread_callframe * frame, const char * string)  {
    return gh_thread_callframe_lstring(frame, strlen(string), string);
}

bool gh_thread_callframe_getint(gh_thread_callframe * frame, int * out_int) {
    if (!frame->returned || frame->return_value == NULL) return false;

    if (frame->return_value->type != GH_VARIANT_INT) return false;
    *out_int = frame->return_value->t_int;
    return true;
}

bool gh_thread_callframe_getdouble(gh_thread_callframe * frame, double * out_double) {
    if (!frame->returned || frame->return_value == NULL) return false;

    if (frame->return_value->type != GH_VARIANT_DOUBLE) return false;
    *out_double = frame->return_value->t_double;
    return true;
}

bool gh_thread_callframe_getlstring(gh_thread_callframe * frame, size_t * out_size, const char ** out_string) {
    if (!frame->returned || frame->return_value == NULL) return false;

    if (frame->return_value->type != GH_VARIANT_STRING) return false;
    *out_size = frame->return_value->t_string_len;
    *out_string = frame->return_value->t_string_data;
    return true;
}

bool gh_thread_callframe_getstring(gh_thread_callframe * frame, const char ** out_string) {
    if (!frame->returned || frame->return_value == NULL) return false;

    if (frame->return_value->type != GH_VARIANT_STRING) return false;

    if (frame->return_value->t_string_data[frame->return_value->t_string_len - 1] == '\0') {
        // embedded null byte
        *out_string = frame->return_value->t_string_data;
    } else {
        gh_fdmem_ptr byte_past_len_virtptr = gh_fdmem_virtptr(&frame->fdmem, frame->return_value->t_string_data + frame->return_value->t_string_len, 1);
        if (byte_past_len_virtptr != 0) {
            // there is memory past the string data, check if it's a null byte (it's safe to deref here according to the condition)
            if (frame->return_value->t_string_data[frame->return_value->t_string_len] == '\0') {
                *out_string = frame->return_value->t_string_data;
                return true;
            }
        }
    }

    return false;
}

gh_result gh_thread_callframe_loadreturnvalue(gh_thread_callframe * frame, gh_fdmem_ptr return_value_ptr) {
    gh_result res = gh_fdmem_sync(&frame->fdmem);
    if (ghr_iserr(res)) return res;

    res = gh_fdmem_seal(&frame->fdmem);
    if (ghr_iserr(res)) return res;

    frame->returned = true;

    gh_variant * return_value = (gh_variant *)gh_fdmem_realptr(&frame->fdmem, return_value_ptr, sizeof(gh_variant));
    if (return_value != NULL) {
        frame->return_value = return_value;
    }

    return GHR_OK;
}

gh_result gh_thread_callframe_dtor(gh_thread_callframe * frame) {
    return gh_fdmem_dtor(&frame->fdmem);
}

gh_result gh_thread_call(gh_thread * thread, const char * name, gh_thread_callframe * frame) {
    size_t name_len = strlen(name);
    if (name_len > GH_IPCMSG_LUACALL_NAMEMAX - 1) return GHR_THREAD_CALLNAMEMAX;

    gh_ipcmsg_luacall msg = {
        .type = GH_IPCMSG_LUACALL,
        .ipcfdmem_fd = frame->fdmem.fd
    };

    memcpy(msg.params, frame->param_ptrs, sizeof(gh_fdmem_ptr) * GH_IPCMSG_LUACALL_MAXPARAMS);


    strncpy(msg.name, name, name_len + 1);

    msg.ipcfdmem_occupied = frame->fdmem.occupied;

    gh_result res = gh_ipc_send(&thread->ipc, (gh_ipcmsg*)&msg, sizeof(gh_ipcmsg_luacall));
    if (ghr_iserr(res)) return res;

    GH_IPCMSG_BUFFER(response_msgbuf);
    res = gh_ipc_recv(&thread->ipc, (gh_ipcmsg *)response_msgbuf, GH_THREAD_LUAINFO_TIMEOUTMS);
    if (ghr_iserr(res)) return res;
    if (((gh_ipcmsg *)response_msgbuf)->type != GH_IPCMSG_LUAINFO) return GHR_THREAD_EXPECTEDLUAINFO;

    int script_id = ((gh_ipcmsg_luainfo *)response_msgbuf)->script_id;
    gh_threadnotif_script script_result = {0};
    res = thread_syncscript(thread, script_id, &script_result);
    if (ghr_iserr(res)) return res;

    return gh_thread_callframe_loadreturnvalue(frame, script_result.call_return_ptr);
}
