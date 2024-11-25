#ifndef GHOST_JAIL_LUA_H
#define GHOST_JAIL_LUA_H

#include <stddef.h>
#include <ghost/result.h>
#include <ghost/ipc.h>
#include <luajit-2.1/lua.h>
#include <luajit-2.1/lauxlib.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-macro-identifier"
// RATIONALE: LuaJIT does this and we don't control LuaJIT source.
#include <luajit-2.1/lualib.h>
#pragma clang diagnostic pop

extern char gh_luainit_script_data[];
extern size_t gh_luainit_script_data_len;

extern char gh_luastdlib_script_data[];
extern size_t gh_luastdlib_script_data_len;

gh_result gh_lua2result(int lua_result);

int gh_lua_pcall(lua_State * L, int nargs, int nret);

#endif
