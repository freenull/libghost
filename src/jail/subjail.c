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
#include <ghost/variant.h>
#include <ghost/result.h>
#include <jail/jail.h>
#include <jail/subjail.h>
#include <jail/lua.h>
#include <jail/luajit-glue.h>

int gh_global_subjail_idx = -1;
int gh_global_script_idx = -1;
lua_State * L;

static gh_result lua_poperror(int r, char * error_msg_buf) {
    const char * s = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (error_msg_buf != NULL) {
        strncpy(error_msg_buf, s, GH_IPCMSG_LUARESULT_ERRORMSGMAX - 1);
        error_msg_buf[GH_IPCMSG_LUARESULT_ERRORMSGMAX - 1] = '\0';
    } else {
        gh_jail_printf("lua error: %s\n", s);
    }


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

static gh_result lua_sethostvariable(gh_ipc * ipc, gh_ipcmsg_luahostvariable * msg) {
    int script_id;
    gh_result res = lua_sendinfomsg(ipc, &script_id);
    if (ghr_iserr(res)) return res;

    int prev_top = lua_gettop(L);

    lua_getglobal(L, "__ghost_host");
    if (lua_type(L, -1) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_createtable(L, 0, 1);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "__ghost_host");
    }

    if (msg->table_index > 0) {
        lua_pushstring(L, msg->name);
        lua_gettable(L, -2);
        if (lua_type(L, -1) == LUA_TNIL) {
            lua_pop(L, 1);
            lua_createtable(L, 1, 0);
            lua_pushstring(L, msg->name);
            lua_pushvalue(L, -2);
            lua_settable(L, -4);
        }
        lua_pushnumber(L, msg->table_index);
    } else {
        lua_pushstring(L, msg->name);
    }

    switch(msg->datatype) {
    case GH_IPCMSG_LUAHOSTVARIABLE_INT:
        lua_pushnumber(L, (lua_Number)msg->t_integer);
        break;
    case GH_IPCMSG_LUAHOSTVARIABLE_DOUBLE:
        lua_pushnumber(L, (lua_Number)msg->t_double);
        break;
    case GH_IPCMSG_LUAHOSTVARIABLE_STRING: {
        size_t len = msg->t_string.len;
        if (len + 1 > GH_IPCMSG_LUAHOSTVARIABLE_STRINGMAX) {
            len = GH_IPCMSG_LUAHOSTVARIABLE_STRINGMAX - 1;
        }
        lua_pushlstring(L, msg->t_string.buffer, msg->t_string.len);
        break;
    }
    default:
        lua_pushnil(L);
    }

    lua_settable(L, -3);

    lua_settop(L, prev_top);

    gh_ipcmsg_luaresult result_msg = {
        .type = GH_IPCMSG_LUARESULT,
        .result = GHR_OK,
        .script_id = script_id,
        .error_msg = {0}
    };
    res = gh_ipc_send(ipc, (gh_ipcmsg *)&result_msg, sizeof(gh_ipcmsg_luaresult));
    if (ghr_iserr(res)) return res;
    return GHR_OK;
}

static gh_result lua_callfunction_pushparam(gh_ipc * ipc, gh_fdmem * mem, gh_variant * param) {
    (void)ipc;
    (void)mem;

    switch(param->type) {
    case GH_VARIANT_NIL: {
        lua_pushnil(L);
        return GHR_OK;
    }
    case GH_VARIANT_INT: {
        lua_pushnumber(L, (lua_Number)param->t_int);
        return GHR_OK;
    }
    case GH_VARIANT_DOUBLE: {
        lua_pushnumber(L, (lua_Number)param->t_double);
        return GHR_OK;
    }
    case GH_VARIANT_STRING: {
        lua_pushlstring(L, param->t_string_data, param->t_string_len);
        return GHR_OK;
    }
    default: return GHR_JAIL_LUACALLPARAM;
    }
}

static gh_result lua_callfunction_getreturn(gh_ipc * ipc, gh_fdmem * mem, gh_fdmem_ptr * out_virtptr) {
    (void)ipc;

    int type = lua_type(L, -1);
    gh_result res = GHR_OK;

    switch(type) {
    case LUA_TNUMBER: {
        gh_variant * param;
        res = gh_fdmem_new(mem, sizeof(gh_variant), (void**)&param);
        if (ghr_iserr(res)) return res;

        *out_virtptr = gh_fdmem_virtptr(mem, param, sizeof(gh_variant));
        if (*out_virtptr == 0) return GHR_JAIL_LUACALLRETURN;

        param->type = GH_VARIANT_DOUBLE;
        param->t_double = (double)lua_tonumber(L, -1);
        break;
    }

    case LUA_TSTRING: {
        size_t len;
        const char * str = lua_tolstring(L, -1, &len);

        gh_variant * param;
        res = gh_fdmem_new(mem, sizeof(gh_variant) + len + 1, (void**)&param);
        if (ghr_iserr(res)) return res;

        *out_virtptr = gh_fdmem_virtptr(mem, param, sizeof(gh_variant) + len + 1);
        if (*out_virtptr == 0) return GHR_JAIL_LUACALLRETURN;

        param->type = GH_VARIANT_STRING;

        param->t_string_len = len;
        strncpy(param->t_string_data, str, len);
        param->t_string_data[len] = '\0';
        break;
    }

    default:
    case LUA_TNIL: {
        gh_variant * param;
        res = gh_fdmem_new(mem, sizeof(gh_variant), (void**)&param);
        if (ghr_iserr(res)) return res;

        *out_virtptr = gh_fdmem_virtptr(mem, param, sizeof(gh_variant));
        if (*out_virtptr == 0) return GHR_UNKNOWN;

        param->type = GH_VARIANT_NIL;
        break;
    }
    }

    return res;
}

static gh_result lua_callfunction(gh_ipc * ipc, gh_ipcmsg_luacall * msg) {
    int script_id;
    gh_result inner_res = GHR_OK;
    gh_result ipcfdmem_dtor_res = GHR_OK;

    gh_result res = lua_sendinfomsg(ipc, &script_id);
    if (ghr_iserr(res)) return res;

    gh_fdmem mem;
    res = gh_fdmem_ctorfdo(&mem, msg->ipcfdmem_fd, msg->ipcfdmem_occupied);
    if (ghr_iserr(res)) return res;
    
    int prev_top = lua_gettop(L);

    gh_ipcmsg_luaresult result_msg = {
        .type = GH_IPCMSG_LUARESULT,
        .result = GHR_OK,
        .script_id = script_id,
        .error_msg = {0},
        
        .return_ptr = 0
    };

    lua_getglobal(L, "__ghost_callbacks");
    if (lua_type(L, -1) == LUA_TNIL) {
        result_msg.result = GHR_JAIL_LUACALLMISSING;
        goto respond;
    }

    lua_getfield(L, -1, msg->name);
    if (lua_type(L, -1) == LUA_TNIL) {
        result_msg.result = GHR_JAIL_LUACALLMISSING;
        goto respond;
    }

    int nargs = 0;
    for (size_t i = 0; i < GH_IPCMSG_LUACALL_MAXPARAMS; i++) {
        gh_fdmem_ptr param_ptr = msg->params[i];
        if (param_ptr == 0) break;

        gh_variant * param = (gh_variant *)gh_fdmem_realptr(&mem, param_ptr, 1);
        if (param == NULL) {
            result_msg.result = GHR_JAIL_LUACALLPARAM;
            goto respond;
        }

        res = lua_callfunction_pushparam(ipc, &mem, param);
        if (ghr_iserr(res)) goto respond;

        nargs += 1;
    }

    int r = gh_lua_pcall(L, nargs, 1);
    if (r != 0) {
        result_msg.result = lua_poperror(r, result_msg.error_msg);
        goto respond;
    }

    res = lua_callfunction_getreturn(ipc, &mem, &result_msg.return_ptr);
    if (ghr_iserr(res)) {
        goto respond;
    }

respond:
    if (ghr_iserr(res)) result_msg.result = res;

    lua_settop(L, prev_top);

    ipcfdmem_dtor_res = gh_fdmem_dtor(&mem);

    inner_res = gh_ipc_send(ipc, (gh_ipcmsg *)&result_msg, sizeof(gh_ipcmsg_luaresult));
    if (ghr_iserr(inner_res)) return inner_res;
    if (ghr_iserr(ipcfdmem_dtor_res)) return ipcfdmem_dtor_res;

    return res;
}

static bool message_recv(gh_ipc * ipc, gh_ipcmsg * msg) {
    (void)ipc;

    switch(msg->type) {
    case GH_IPCMSG_HELLO: ghr_fail(GHR_JAIL_MULTIHELLO); break;

    case GH_IPCMSG_QUIT:
        gh_jail_printf("subjail %d: received request to exit\n", gh_global_subjail_idx);
        return true;

    case GH_IPCMSG_LUASTRING:
        gh_jail_printf("subjail %d: running lua (string)\n", gh_global_subjail_idx);
        ghr_assert(lua_executestring(ipc, ((gh_ipcmsg_luastring *)msg)->content));
        gh_jail_printf("subjail %d: finished running lua (string)\n", gh_global_subjail_idx);

        return false;

    case GH_IPCMSG_LUAFILE: {
        gh_jail_printf("subjail %d: running lua (file)\n", gh_global_subjail_idx);
        gh_ipcmsg_luafile * file_msg = (gh_ipcmsg_luafile *)msg;
        ghr_assert(lua_executefile(ipc, file_msg->fd, file_msg->chunk_name));
        gh_jail_printf("subjail %d: finished running lua (file)\n", gh_global_subjail_idx);

        return false;
    }

    case GH_IPCMSG_LUAHOSTVARIABLE: {
        gh_ipcmsg_luahostvariable * global_msg = (gh_ipcmsg_luahostvariable *)msg;
        gh_jail_printf("subjail %d: setting lua host variable '%s'\n", gh_global_subjail_idx, global_msg->name);
        ghr_assert(lua_sethostvariable(ipc, global_msg));
        gh_jail_printf("subjail %d: finished setting lua host variable '%s'\n", gh_global_subjail_idx, global_msg->name);

        return false;
    }

    case GH_IPCMSG_LUACALL: {
        gh_ipcmsg_luacall * call_msg = (gh_ipcmsg_luacall *)msg;
        gh_jail_printf("subjail %d: calling lua function '%s'\n", gh_global_subjail_idx, call_msg->name);
        ghr_assert(lua_callfunction(ipc, call_msg));
        gh_jail_printf("subjail %d: finished calling lua function '%s'\n", gh_global_subjail_idx, call_msg->name);

        return false;
    }

    case GH_IPCMSG_LUAINFO: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);
    case GH_IPCMSG_LUARESULT: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);

    case GH_IPCMSG_SUBJAILALIVE: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);
    case GH_IPCMSG_NEWSUBJAIL: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);

    case GH_IPCMSG_FUNCTIONCALL: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);
    case GH_IPCMSG_FUNCTIONRETURN: ghr_fail(GHR_JAIL_UNSUPPORTEDMSG);

    default:
        gh_jail_printf("subjail %d: received unknown message of type %d\n", gh_global_subjail_idx, (int)msg->type);
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

static int luafunc_udptr(lua_State * state) {
    uintptr_t addr = (uintptr_t)(void*)lua_touserdata(state, 1);

    char buf[16] = {0};
    if (snprintf(buf, sizeof(buf), "%zx", addr) < 0) {
        lua_pushnil(state);
        return 1;
    }

    lua_pushlstring(state, buf, sizeof(buf));
    return 1;
}

static gh_result lua_init(gh_ipc * ipc) {
    luaL_openlibs(L);

    lua_createtable(L, 0, 1);

    lua_pushvalue(L, -1);
    lua_setglobal(L, "c_support");

    lua_pushvalue(L, -1);
    lua_pushcfunction(L, luafunc_fdopen);
    lua_setfield(L, -2, "fdopen");

    lua_pushcfunction(L, luafunc_udptr);
    lua_setfield(L, -2, "udptr");

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
#pragma GCC diagnostic pop

    r = luaL_loadbuffer(L, ipc_line, (size_t)ipc_line_len, "init(ipc_line)");
    if (r != 0) goto err;

    r = gh_lua_pcall(L, 0, 0);
    if (r != 0) goto err;

    r = luaL_loadbuffer(L, gh_luainit_script_data, gh_luainit_script_data_len, "init");
    if (r != 0) goto err;

    r = gh_lua_pcall(L, 0, 0);
    if (r != 0) goto err;

    r = luaL_loadbuffer(L, gh_luastdlib_script_data, gh_luastdlib_script_data_len, "stdlib");
    if (r != 0) goto err;

    r = gh_lua_pcall(L, 0, 0);
    if (r != 0) goto err;

err:
    if (r != 0) return lua_poperror(r, NULL);

    return GHR_OK;
}

int gh_subjail_main(gh_ipc * ipc, int parent_pid, gh_ipc * parent_ipc) {
    (void)parent_pid;

    ghr_assert(gh_ipc_dtor(parent_ipc));

    gh_jail_printf("subjail %d: started by jail pid %d\n", gh_global_subjail_idx, parent_pid);

    gh_jail_printf("subjail %d: installing second seccomp filter\n", gh_global_subjail_idx);
    ghr_assert(gh_subjail_lockdown());
    gh_jail_printf("subjail %d: security policy in effect\n", gh_global_subjail_idx);

    L = luaL_newstate();

    gh_result res = lua_init(ipc);
    if (ghr_iserr(res)) {
        gh_jail_printf("subjail %d: failed initializing lua: ", gh_global_subjail_idx);
        ghr_fputs(stderr, res);
        return 1;
    }

    gh_jail_printf("subjail %d: luajit ready\n", gh_global_subjail_idx);

    gh_ipcmsg_subjailalive subjailalive_msg;
    subjailalive_msg.type = GH_IPCMSG_SUBJAILALIVE;
    subjailalive_msg.index = gh_global_subjail_idx;
    subjailalive_msg.pid = getpid();
    ghr_assert(gh_ipc_send(ipc, (gh_ipcmsg *)&subjailalive_msg, sizeof(gh_ipcmsg_subjailalive)));

    gh_jail_printf("subjail %d: waiting for hello\n", gh_global_subjail_idx);

    char msg_buf[GH_IPCMSG_MAXSIZE];
    gh_ipcmsg * msg = (gh_ipcmsg *)msg_buf;
    res = gh_ipc_recv(ipc, msg, GH_JAIL_HELLOTIMEOUTMS);

    if (res == GHR_IPC_RECVMSGTIMEOUT) {
        gh_jail_printf("subjail %d: timed out while waiting for hello message, bailing\n", gh_global_subjail_idx);
        return 1;
    }
    ghr_assert(res);

    if (msg->type != GH_IPCMSG_HELLO) {
        gh_jail_printf("subjail %d: received hello message\n", gh_global_subjail_idx);
        return 1;
    }

    gh_jail_printf("subjail %d: received hello message\n", gh_global_subjail_idx);

    gh_jail_printf("subjail %d: entering main message loop\n", gh_global_subjail_idx);

    while (true) {
        ghr_assert(gh_ipc_recv(ipc, msg, 0));

        if (message_recv(ipc, msg)) break;
    }

    lua_close(L);

    gh_jail_printf("subjail %d: stopping gracefully\n", gh_global_subjail_idx);
    return 0;
}

gh_result gh_subjail_lockdown(void) {
    char * gh_sandbox = getenv("GH_SANDBOX_DISABLED");
    if (gh_sandbox != NULL && strcmp(gh_sandbox, "1") == 0) {
        gh_jail_printf("jail: SANDBOX DISABLED\n");
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
