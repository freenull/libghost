#define _GNU_SOURCE
#include <signal.h>
#include <string.h>
#include <ghost/result.h>
#include <ghost/thread.h>
#include <ghost/rpc.h>
#include <ghost/ipc.h>

gh_result gh_sandbox_newthread(gh_sandbox * sandbox, gh_alloc * alloc, const char * name, gh_thread * out_thread) {
    gh_ipc direct_ipc;
    int direct_peerfd;

    gh_result res = GHR_OK;
    gh_result inner_res = GHR_OK;

    res = gh_ipc_ctor(&direct_ipc, &direct_peerfd);
    if (ghr_iserr(res)) goto fail_ipc;

    gh_ipcmsg_newsubjail newsubjail_msg;
    memset(&newsubjail_msg, 0, sizeof(gh_ipcmsg_newsubjail));
    newsubjail_msg.type = GH_IPCMSG_NEWSUBJAIL;
    newsubjail_msg.sockfd = direct_peerfd;
    res = gh_ipc_send(&sandbox->ipc, (gh_ipcmsg*)&newsubjail_msg, sizeof(gh_ipcmsg_newsubjail));
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

    out_thread->ipc = direct_ipc;
    out_thread->pid = subjail_pid;
    strncpy(out_thread->name, name, GH_THREAD_MAXNAME);
    out_thread->name[GH_THREAD_MAXNAME - 1] = '\0';
    out_thread->sandbox = sandbox;

    res = gh_rpc_ctor(&out_thread->rpc, alloc);
    if (ghr_iserr(res)) goto fail_rpc_ctor;

    return res;

fail_rpc_ctor:
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

gh_result gh_thread_dtor(gh_thread * thread) {
    gh_result res = gh_rpc_dtor(&thread->rpc);
    if (ghr_iserr(res)) return res;

    res = gh_ipc_dtor(&thread->ipc);
    if (ghr_iserr(res)) return res;
    
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
    // RATIONALE: TODO TEMPORARY
    switch(msg->type) {
    case GH_IPCMSG_FUNCTIONCALL:
        if (notif != NULL) notif->type = GH_THREADNOTIF_FUNCTIONCALLED;
        gh_ipcmsg_functioncall * call_msg = (gh_ipcmsg_functioncall *)msg;
        if (notif != NULL) notif->function.name = call_msg->name;
        gh_result res = thread_handlemsg_functioncall(thread, call_msg);
        if (ghr_is(res, GHR_RPC_MISSINGFUNC)) {
            notif->function.missing = true;
            return GHR_OK;
        }

        return res;

    case GH_IPCMSG_LUARESULT:
        if (notif != NULL) {
            notif->type = GH_THREADNOTIF_SCRIPTRESULT;
            gh_ipcmsg_luaresult * result_msg = (gh_ipcmsg_luaresult *)msg;
            notif->script.result = result_msg->result;
            notif->script.id = result_msg->script_id;
            notif->script.error_msg = result_msg->error_msg;
        }
        return GHR_OK;

    default:
        gh_thread_forcekill(thread);
        return GHR_THREAD_UNKNOWNMESSAGE;
    }
#pragma GCC diagnostic pop

/*     return GHR_OK; */
}

gh_result gh_thread_process(gh_thread * thread, gh_threadnotif * notif) {
    GH_IPCMSG_BUFFER(msg_buf);

    gh_ipcmsg * msg = (gh_ipcmsg *)msg_buf;

    gh_result res = gh_ipc_recv(&thread->ipc, msg, GH_IPC_NOTIMEOUT);
    if (ghr_iserr(res)) return res;

    return thread_handlemsg(thread, msg, notif);
}

gh_result gh_thread_requestquit(gh_thread * thread) {
    gh_ipcmsg_quit quit_msg;
    memset(&quit_msg, 0, sizeof(gh_ipcmsg_quit));
    quit_msg.type = GH_IPCMSG_QUIT;
    return gh_ipc_send(&thread->ipc, (gh_ipcmsg*)&quit_msg, sizeof(gh_ipcmsg_quit));
}

gh_result gh_thread_forcekill(gh_thread * thread) {
    int kill_res = kill(thread->pid, SIGKILL);
    if (kill_res < 0) return ghr_errno(GHR_SANDBOX_KILLFAIL);
    return GHR_OK;
}

inline gh_rpc * gh_thread_rpc(gh_thread * thread) {
    return &thread->rpc;
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

    if (script_id != NULL) *script_id = ((gh_ipcmsg_luainfo *)response_msgbuf)->script_id;
    return GHR_OK;
}
