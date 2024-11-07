#ifndef GHOST_SUBJAIL_H
#define GHOST_SUBJAIL_H

#include <ghost/ipc.h>

extern int gh_global_subjail_idx;

void gh_subjail_spawn(int sockfd, int parent_pid, gh_ipc * parent_ipc);
int gh_subjail_main(gh_ipc * ipc, int parent_pid, gh_ipc * parent_ipc);

#endif
