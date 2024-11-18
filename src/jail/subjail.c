#include "ghost/result.h"

#define _GNU_SOURCE
#include <execinfo.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <sys/types.h>
#include <ghost/ipc.h>
#include <jail/jail.h>
#include <jail/subjail.h>
#include <jail/lua.h>

int gh_global_subjail_idx = -1;
int gh_global_script_idx = -1;
lua_State * L;

static gh_result lua_poperror(int r) {
    const char * s = lua_tostring(L, -1);
    lua_pop(L, 1);
    fprintf(stderr, "lua error: %s\n", s);
    return gh_lua2result(r);
}

static gh_result execute_lua_string(gh_ipc * ipc, const char * s) {
    gh_ipcmsg_luainfo msg = {0};
    msg.type = GH_IPCMSG_LUAINFO;
    gh_global_script_idx += 1;
    int script_idx = gh_global_script_idx;
    msg.script_id = gh_global_script_idx;

    gh_result res = gh_ipc_send(ipc, (gh_ipcmsg *)&msg, sizeof(gh_ipcmsg_luainfo));
    if (ghr_iserr(res)) return res;

    gh_ipcmsg_luaresult result_msg = {0};

    int r = luaL_loadbuffer(L, s, strlen(s), "string");
    if (r != 0) {
        res = gh_lua2result(r);
        goto result;
    }

    r = gh_lua_pcall(L, 0, 0);
    if (r != 0) {
        const char * err = lua_tostring(L, -1);
        strncpy(result_msg.error_msg, err, GH_IPCMSG_LUARESULT_ERRORMSGMAX - 1);
        result_msg.error_msg[GH_IPCMSG_LUARESULT_ERRORMSGMAX - 1] = '\0';
        lua_pop(L, 1);

        res = gh_lua2result(r);
    }

result: 
    result_msg.type = GH_IPCMSG_LUARESULT;
    result_msg.result = res;
    result_msg.script_id = script_idx;
    res = gh_ipc_send(ipc, (gh_ipcmsg *)&result_msg, sizeof(gh_ipcmsg_luaresult));
    if (ghr_iserr(res)) return res;

    return GHR_OK;
}

static bool message_recv(gh_ipc * ipc, gh_ipcmsg * msg) {
    (void)ipc;

    switch(msg->type) {
    case GH_IPCMSG_HELLO: ghr_fail(GHR_JAIL_MULTIHELLO); break;

    case GH_IPCMSG_QUIT:
        fprintf(stderr, "subjail %d: received request to exit\n", gh_global_subjail_idx);
        return true;

    case GH_IPCMSG_LUASTRING:
        fprintf(stderr, "subjail %d: running lua\n", gh_global_subjail_idx);
        ghr_assert(execute_lua_string(ipc, ((gh_ipcmsg_luastring *)msg)->content));
        fprintf(stderr, "subjail %d: finished running lua\n", gh_global_subjail_idx);

        return false;
        
    case GH_IPCMSG_LUAINFO: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);
    case GH_IPCMSG_LUARESULT: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);

    case GH_IPCMSG_SUBJAILALIVE: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);
    case GH_IPCMSG_NEWSUBJAIL: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);

    case GH_IPCMSG_FUNCTIONCALL: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);
    case GH_IPCMSG_FUNCTIONRETURN: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);

    default:
        fprintf(stderr, "subjail %d: received unknown message of type %d\n", gh_global_subjail_idx, (int)msg->type);
        ghr_fail(GHR_JAIL_UNKNOWNMESSAGE);
        break;
    }

    return false;
}

void gh_subjail_spawn(int sockfd, int parent_pid, gh_ipc * parent_ipc) {
    gh_global_subjail_idx += 1;

    pid_t pid = fork();

    if (pid == 0) {
        gh_ipc ipc;
        gh_ipc_ctorconnect(&ipc, sockfd);
        _exit(gh_subjail_main(&ipc, parent_pid, parent_ipc));
    }
}

static gh_result lua_init(gh_ipc * ipc) {
    luaL_openlibs(L);

    int r = 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    // RATIONALE: This is intentional to avoid accidental bugs when the format changes.

    // pushnumber is 53 bit int max (because it's a double)
    // there is no pushcdata in luajit, so this is the easiest way to
    // give the init script the gh_ipc pointer
    static const char * ipc_line_format = "IPC = require('ffi').cast('void *', 0x%"PRIxPTR"LL)";
    int ipc_line_len = snprintf(NULL, 0, ipc_line_format, (uintptr_t)ipc);
    if (ipc_line_len < 0) return GHR_JAIL_LUAINITFAIL;
    char ipc_line[ipc_line_len + 1];

    if (snprintf(ipc_line, (unsigned int)ipc_line_len + 1, ipc_line_format, (uintptr_t)ipc) < 0) return GHR_JAIL_LUAINITFAIL;

    r = luaL_loadbuffer(L, ipc_line, (size_t)ipc_line_len, "init(ipc_line)");
    if (r != 0) goto err;
#pragma GCC diagnostic pop

    r = gh_lua_pcall(L, 0, 0);
    if (r != 0) goto err;

    r = luaL_loadbuffer(L, gh_luainit_script_data, gh_luainit_script_data_len, "init");
    if (r != 0) goto err;

    r = gh_lua_pcall(L, 0, 0);
    if (r != 0) goto err;

err:
    if (r != 0) return lua_poperror(r);

    return GHR_OK;
}

int gh_subjail_main(gh_ipc * ipc, int parent_pid, gh_ipc * parent_ipc) {
    ghr_assert(gh_ipc_dtor(parent_ipc));

    fprintf(stderr, "subjail %d: started by jail pid %d\n", gh_global_subjail_idx, parent_pid);

    L = luaL_newstate();

    gh_result res = lua_init(ipc);
    if (ghr_iserr(res)) {
        fprintf(stderr, "subjail %d: failed initializing lua: ", gh_global_subjail_idx);
        ghr_fputs(stderr, res);
        return 1;
    }

    fprintf(stderr, "subjail %d: luajit ready\n", gh_global_subjail_idx);

    gh_ipcmsg_subjailalive subjailalive_msg;
    subjailalive_msg.type = GH_IPCMSG_SUBJAILALIVE;
    subjailalive_msg.index = gh_global_subjail_idx;
    subjailalive_msg.pid = getpid();
    ghr_assert(gh_ipc_send(ipc, (gh_ipcmsg *)&subjailalive_msg, sizeof(gh_ipcmsg_subjailalive)));

    fprintf(stderr, "subjail %d: waiting for hello\n", gh_global_subjail_idx);

    char msg_buf[GH_IPCMSG_MAXSIZE];
    gh_ipcmsg * msg = (gh_ipcmsg *)msg_buf;
    res = gh_ipc_recv(ipc, msg, GH_JAIL_HELLOTIMEOUTMS);

    if (res == GHR_IPC_RECVMSGTIMEOUT) {
        fprintf(stderr, "subjail %d: timed out while waiting for hello message, bailing\n", gh_global_subjail_idx);
        return 1;
    }
    ghr_assert(res);

    if (msg->type != GH_IPCMSG_HELLO) {
        fprintf(stderr, "subjail %d: received hello message\n", gh_global_subjail_idx);
        return 1;
    }

    fprintf(stderr, "subjail %d: received hello message\n", gh_global_subjail_idx);

    fprintf(stderr, "subjail %d: entering main message loop\n", gh_global_subjail_idx);

    while (true) {
        ghr_assert(gh_ipc_recv(ipc, msg, 0));

        if (message_recv(ipc, msg)) break;
    }

    fprintf(stderr, "subjail %d: quitting normally\n", gh_global_subjail_idx);
    return 0;
}
