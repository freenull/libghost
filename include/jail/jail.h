#ifndef GHOST_JAIL_H
#define GHOST_JAIL_H

#include <ghost/sandbox.h>

#ifdef GH_JAIL_DIAGNOSTICS
#define gh_jail_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define gh_jail_printf(...)
#endif

#define GH_JAIL_HELLOTIMEOUTMS 1000 * 5

/** @brief Global sandbox options.
 *
 * @par This structure is filled in by reading from a memory file prepared by the library when executing the jail, passed in through `argv[1]`.
 *
 */
extern gh_sandboxoptions gh_global_sandboxoptions;

/** @brief Installs memory limits and the seccomp policy.
 *
 * @param options Structure containing sandbox options, incl. those related to the security policy.
 *
 * @return Result code.
 */
gh_result gh_jail_lockdown(gh_sandboxoptions * options);

#endif
