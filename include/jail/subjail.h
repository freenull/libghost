#ifndef GHOST_SUBJAIL_H
#define GHOST_SUBJAIL_H

#include <ghost/ipc.h>
#include <luajit-2.1/lua.h>

extern int gh_global_subjail_idx;
extern int gh_global_script_idx;
extern lua_State * L;

void gh_subjail_spawn(int sockfd, int parent_pid, gh_ipc * parent_ipc);
int gh_subjail_main(gh_ipc * ipc, int parent_pid, gh_ipc * parent_ipc);
gh_result gh_subjail_lockdown(void);

#endif
