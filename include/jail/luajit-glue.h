#ifndef GHOST_JAIL_LUAJITGLUE_H
#define GHOST_JAIL_LUAJITGLUE_H

#include <stdio.h>
#include <luajit-2.1/lua.h>

typedef struct gh_luajit_file gh_luajit_file;

void gh_luajit_pushfile(lua_State * L, FILE * fp);

#endif
