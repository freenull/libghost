#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

int fexecve(int fd, char *const argv[], char *const envp[]) {
    ssize_t len = snprintf(NULL, 0, "/proc/self/fd/%d", fd);
    if (len < 0) return -1;
    char tmp[len + 1];
    if (snprintf(tmp, len + 1, "/proc/self/fd/%d", fd) < 0) return -1;

    return execve(tmp, argv, envp);
}
