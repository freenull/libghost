// Valgrind has trouble with fexecve from memfd, which is used in libghost to
// avoid having to distribute ghost-jail separately from libghost.so.
//
// This source file overrides fexecve to execve() the locally built file instead.
// This is only intended for running tests, and can be included in test builds
// in CMakeLists.txt by running GhostTest(...) like so:
//     GhostTest(testname VALGRINDWORKAROUND)
//
// CMake will define GHOST_JAIL_EXE_PATH to point to the locally built ghost-jail.
//
// WARNING: This is a little hacky and assumes that only ghost ever calls fexecve,
// and only when spawning a jail process. This could probably be improved, but it's
// enough for the tests that we currently have.
#include <unistd.h>
#include <ghost/embedded_jail.h>

int fexecve(int fd, char * const * argv, char * const * const env);
int fexecve(int fd, char * const * argv, char * const * const env) {
    (void)fd;
    return execve(GHOST_JAIL_EXE_PATH, argv, env);
}

