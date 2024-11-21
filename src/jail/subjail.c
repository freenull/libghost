#define _GNU_SOURCE
#include <linux/bpf_common.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <execinfo.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <ghost/ipc.h>
#include <ghost/result.h>
#include <jail/jail.h>
#include <jail/subjail.h>
#include <jail/lua.h>
#include <jail/luajit-glue.h>

int gh_global_subjail_idx = -1;
int gh_global_script_idx = -1;
lua_State * L;

static gh_result lua_poperror(int r) {
    const char * s = lua_tostring(L, -1);
    lua_pop(L, 1);
    fprintf(stderr, "lua error: %s\n", s);
    return gh_lua2result(r);
}

static gh_result lua_sendinfomsg(gh_ipc * ipc, int * out_script_id) {
    gh_ipcmsg_luainfo msg = {0};
    msg.type = GH_IPCMSG_LUAINFO;
    gh_global_script_idx += 1;
    int script_idx = gh_global_script_idx;
    msg.script_id = gh_global_script_idx;

    gh_result res = gh_ipc_send(ipc, (gh_ipcmsg *)&msg, sizeof(gh_ipcmsg_luainfo));
    if (ghr_iserr(res)) return res;

    *out_script_id = script_idx;

    return GHR_OK;
}

static gh_result lua_execute(gh_ipc * ipc, int script_id) {
    gh_ipcmsg_luaresult result_msg = {0};

    gh_result lua_result = GHR_OK;
    int r = gh_lua_pcall(L, 0, 0);
    if (r != 0) {
        const char * err = lua_tostring(L, -1);
        strncpy(result_msg.error_msg, err, GH_IPCMSG_LUARESULT_ERRORMSGMAX - 1);
        result_msg.error_msg[GH_IPCMSG_LUARESULT_ERRORMSGMAX - 1] = '\0';
        lua_pop(L, 1);

        lua_result = gh_lua2result(r);
    }

    result_msg.type = GH_IPCMSG_LUARESULT;
    result_msg.result = lua_result;
    result_msg.script_id = script_id;
    gh_result res = gh_ipc_send(ipc, (gh_ipcmsg *)&result_msg, sizeof(gh_ipcmsg_luaresult));
    if (ghr_iserr(res)) return res;

    return GHR_OK;
}

static gh_result lua_executestring(gh_ipc * ipc, const char * s) {
    int script_id;
    gh_result res = lua_sendinfomsg(ipc, &script_id);
    if (ghr_iserr(res)) return res;

    int r = luaL_loadbuffer(L, s, strlen(s), "string");
    if (r != 0) {
        gh_result lua_result = gh_lua2result(r);

        gh_ipcmsg_luaresult msg = {
            .type = GH_IPCMSG_LUARESULT,
            .result = lua_result,
            .script_id = script_id,
            .error_msg = {0}
        };
        res = gh_ipc_send(ipc, (gh_ipcmsg *)&msg, sizeof(gh_ipcmsg_luaresult));
        if (ghr_iserr(res)) return res;
    }

    return lua_execute(ipc, script_id);
}

typedef struct {
    int fd;
    size_t buffer_size;
    char * buffer;
    int errno_result;
} lua_fdreader_data;

static const char * lua_fdreader(lua_State * state, void * data_voidp, size_t * out_size) {
    (void)state;

    lua_fdreader_data * data = (lua_fdreader_data *)data_voidp;

    ssize_t readn = read(data->fd, data->buffer, data->buffer_size);
    if (readn < 0) {
        data->errno_result = errno;
        return NULL;
    }

    *out_size = (size_t)readn;
    return data->buffer;
}

#define LUA_EXECUTEFILE_BUFFERSIZE 4096
static gh_result lua_executefile(gh_ipc * ipc, int fd, const char * chunk_name) {
    int script_id;
    gh_result res = lua_sendinfomsg(ipc, &script_id);
    if (ghr_iserr(res)) return res;

    char read_buffer[LUA_EXECUTEFILE_BUFFERSIZE];
    lua_fdreader_data fdreader_ud = {
        .errno_result = -1,
        .fd = fd,
        .buffer = read_buffer,
        .buffer_size = LUA_EXECUTEFILE_BUFFERSIZE
    };
    int r = lua_load(L, lua_fdreader, (void*)&fdreader_ud, chunk_name);
    if (r != 0) {
        gh_result lua_result = gh_lua2result(r);

        gh_ipcmsg_luaresult msg = {
            .type = GH_IPCMSG_LUARESULT,
            .result = lua_result,
            .script_id = script_id,
            .error_msg = {0}
        };
        res = gh_ipc_send(ipc, (gh_ipcmsg *)&msg, sizeof(gh_ipcmsg_luaresult));
        if (ghr_iserr(res)) return res;
    }

    return lua_execute(ipc, script_id);
}

static bool message_recv(gh_ipc * ipc, gh_ipcmsg * msg) {
    (void)ipc;

    switch(msg->type) {
    case GH_IPCMSG_HELLO: ghr_fail(GHR_JAIL_MULTIHELLO); break;

    case GH_IPCMSG_QUIT:
        fprintf(stderr, "subjail %d: received request to exit\n", gh_global_subjail_idx);
        return true;

    case GH_IPCMSG_LUASTRING:
        fprintf(stderr, "subjail %d: running lua (string)\n", gh_global_subjail_idx);
        ghr_assert(lua_executestring(ipc, ((gh_ipcmsg_luastring *)msg)->content));
        fprintf(stderr, "subjail %d: finished running lua (string)\n", gh_global_subjail_idx);

        return false;

    case GH_IPCMSG_LUAFILE: {
        fprintf(stderr, "subjail %d: running lua (file)\n", gh_global_subjail_idx);
        gh_ipcmsg_luafile * file_msg = (gh_ipcmsg_luafile *)msg;
        ghr_assert(lua_executefile(ipc, file_msg->fd, file_msg->chunk_name));
        fprintf(stderr, "subjail %d: finished running lua (file)\n", gh_global_subjail_idx);

        return false;
    }

    case GH_IPCMSG_KILLSUBJAIL: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);
    case GH_IPCMSG_SUBJAILDEAD: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);
        
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

static int luafunc_fdopen(lua_State * state) {
    lua_Number fd = lua_tonumber(state, 1);
    const char * mode = lua_tostring(state, 2);

    FILE * fp = fdopen((int)fd, mode);
    if (fp == NULL) {
        int saved_errno = errno;
        lua_pushstring(state, "fdopen failed: ");
        lua_pushstring(state, strerror(saved_errno));
        lua_concat(state, 2);
        lua_error(state);
        return 0;
    }

    gh_luajit_pushfile(state, fp);
    return 1;
}

static gh_result lua_init(gh_ipc * ipc) {
    luaL_openlibs(L);

    lua_createtable(L, 0, 1);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "c_support");
    lua_pushcfunction(L, luafunc_fdopen);
    lua_setfield(L, -2, "fdopen");


    int r = 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    // RATIONALE: This is intentional to avoid accidental bugs when the format changes.

    // pushnumber is 53 bit int max (because it's a double)
    // there is no pushcdata in luajit, so this is the easiest way to
    // give the init script the gh_ipc pointer
    static const char * ipc_line_format = "c_support.ipc = require('ffi').cast('void *', 0x%"PRIxPTR"LL)";
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

    fprintf(stderr, "subjail %d: installing second seccomp filter\n", gh_global_subjail_idx);
    ghr_assert(gh_subjail_lockdown());
    fprintf(stderr, "subjail %d: security policy in effect\n", gh_global_subjail_idx);

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

gh_result gh_subjail_lockdown(void) {
    char * gh_sandbox = getenv("GH_SANDBOX_DISABLED");
    if (gh_sandbox != NULL && strcmp(gh_sandbox, "1") == 0) {
        fprintf(stderr, "jail: SANDBOX DISABLED\n");
        return GHR_OK;
    }

    // additional filter to block fork, kill and ather syscalls in subjail,
    // that are only needed by the jail process
    static const struct sock_filter filter[] = {
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, arch))),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL_PROCESS),

        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, (offsetof(struct seccomp_data, nr))),
        
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_wait4, 3, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_fork, 2, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_kill, 1, 0),

        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL_PROCESS)
    };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    // RATIONALE: Field .filter will never be modified. This struct is only passed to seccomp.
    static const struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = (struct sock_filter*)filter
    };
#pragma GCC diagnostic pop

    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog) != 0)
    {
        return ghr_errno(GHR_JAIL_SECCOMPFAIL);
    }

    return GHR_OK;
}
