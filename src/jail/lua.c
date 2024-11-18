#include <jail/lua.h>
#include <luajit-2.1/lua.h>

gh_result gh_lua2result(int lua_result) {
    switch(lua_result) {
    case 0: return GHR_OK;
    case LUA_ERRSYNTAX: return GHR_LUA_SYNTAX;
    case LUA_ERRMEM: return GHR_LUA_MEM;
    case LUA_ERRRUN: return GHR_LUA_RUNTIME;
    case LUA_ERRERR: return GHR_LUA_RECURSIVEERR;
    default: return GHR_LUA_FAIL;
    }
}

static int luafunc_errorhandler(lua_State * L) {
    if (!lua_isstring(L, -1)) return 1;

    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }

    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }

    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);

    return 1;
}

int gh_lua_pcall(lua_State * L, int nargs, int nret) {
    int errorhandler_pos = lua_gettop(L) - nargs;
    lua_pushcfunction(L, luafunc_errorhandler);
    lua_insert(L, errorhandler_pos);

    int pcall_res = lua_pcall(L, nargs, nret, errorhandler_pos);
    lua_remove(L, errorhandler_pos);
    return pcall_res;
}
