#define _GNU_SOURCE
#include <string.h>
#include <signal.h>
#include <ghost/result.h>

const char * ghr_context(gh_result value) {
    gh_error err = ghr_frag_context(value);
    if (value != 0 && err == GHR_OK) {
        return "Incorrectly constructed contextless result value";
    }

    return gh_error_desc(ghr_frag_context(value));
}

const char * ghr_error(gh_result value) {
    int errno_value = ghr_frag_errno(value);
    if (errno_value == 0xFFFF) {
        return "Errno value beyond storable range (>0xFFFF)";
    }

    return strerror(errno_value);
}

void ghr_fprintf(FILE * file, gh_result value) {
    gh_error err = ghr_frag_context(value);

    if (value != 0 && err == GHR_OK) {
        fprintf(file, "[INCORRECT] Incorrectly constructed contextless result value");
    } else {
        fprintf(file, "[%s] %s", gh_error_name(err), gh_error_desc(err));
    }


    if (ghr_context_has_exitcode(err)) {
        fprintf(file, " (exit code %d)", ghr_exitcode(value));
    } else if (ghr_context_has_signalno(err)) {
        fprintf(file, " (signal %d SIG%s)", ghr_signalno(value), sigabbrev_np(ghr_signalno(value)));
    } else {
        int errno_value = ghr_frag_errno(value);
        if (errno_value != 0) {
            fprintf(file, " (%s)", strerror(errno_value));
        }
    }
}

void ghr_fputs(FILE * file, gh_result value) {
    ghr_fprintf(file, value);
    fprintf(file, "\n");
}
