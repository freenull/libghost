/** @defgroup thread Sandbox thread
 *
 * @brief Library representation of a live subjail process with an installed RPC handler and permission store. Not to be confused with OS threads, although by definition the subjail process executes in parallel.
 *
 * @{
 */

#ifndef GHOST_THREAD_H
#define GHOST_THREAD_H

#include <stdlib.h>
#include <stdarg.h>
#include <ghost/result.h>
#include <ghost/ipc.h>
#include <ghost/fdmem.h>
#include <ghost/sandbox.h>
#include <ghost/rpc.h>
#include <ghost/alloc.h>
#include <ghost/perms/perms.h>
#include <ghost/variant.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GH_THREAD_MAXNAME 256
#define GH_THREAD_MAXSAFEID 512

/** @brief Thread notification type. */
typedef enum {
    /** @brief RPC function was called by remote Lua. */
    GH_THREADNOTIF_FUNCTIONCALLED,
    /** @brief Lua function, file or string finished executing. */
    GH_THREADNOTIF_SCRIPTRESULT
} gh_threadnotif_type;

/** @brief Maximum size of error message received from remote Lua. */
#define GH_THREADNOTIF_SCRIPT_ERRORMSGMAX 1024

/** @brief Result of the execution of a Lua file, string or function. */
typedef struct {
    /** @brief Result code. */
    gh_result result;
    /** @brief Script ID. */
    int id;
    /** @brief Error message. May be empty if not available. */
    char error_msg[GH_THREADNOTIF_SCRIPT_ERRORMSGMAX];

    /** @brief Pointer to return value in FDMEM.
     *         Only set by Lua function calls. Do not attempt to read
     *         directly, use @ref gh_thread_callframe functions.
     */
    gh_fdmem_ptr call_return_ptr;
} gh_threadnotif_script;

/** @brief Information about an RPC function call request. */
typedef struct {
    /** @brief Name of the RPC function. */
    char name[GH_IPCMSG_FUNCTIONCALL_MAXNAME];
    /** @brief If true, the function wasn't registered. */
    bool missing;
} gh_threadnotif_function;

/** @brief Thread process loop notification. */
typedef struct {
    /** @brief Type. Determines valid field of union. */
    gh_threadnotif_type type;
    union {
        /** @brief Script result. */
        gh_threadnotif_script script;
        /** @brief RPC function call result. */
        gh_threadnotif_function function;
    };
} gh_threadnotif;

/** @brief Sandbox thread. */
struct gh_thread {
    /** @brief Parent sandbox. */
    gh_sandbox * sandbox;

    /** @brief Name of the sandbox thread. */
    char name[GH_THREAD_MAXNAME];

    /** @brief Safe ID of the sandbox thread. @n
     *         This is intended to be a display name for the sandbox thread.
     *         This name should be chosen by the application in such a way,
     *         that a potentially malicious extension/script cannot confuse
     *         the user into accepting permission requests that they would
     *         normally not accept.
     */
    char safe_id[GH_THREAD_MAXSAFEID];

    /** @brief PID of the subjail process. */
    pid_t pid;

    /** @brief IPC instance. */
    gh_ipc ipc;

    /** @brief Associated RPC registrar. */
    gh_rpc * rpc;

    /** @brief Centralized permission system. */
    gh_perms perms;

    /** @brief Maximum time to wait to receive a message back after executing
     *         Lua code or functions.
     */
    int default_timeout_ms;

    /** @brief Arbitrary userdata. */
    void * userdata;
};

#ifndef GH_TYPEDEF_THREAD
typedef struct gh_thread gh_thread;
#define GH_TYPEDEF_THREAD
#endif

#define GH_THREAD_LUAINFO_TIMEOUTMS 1000

/** @brief Thread options. 
 *
 * All fields must be set.
 */
typedef struct {
    /** @brief Associated sandbox. */
    gh_sandbox * sandbox;

    /** @brief Associated RPC registrar. */
    gh_rpc * rpc;

    /** @brief Permission prompter to handle permission requests. */
    gh_permprompter prompter;

    /** @brief Name of the sandbox thread. */
    char name[GH_THREAD_MAXNAME];

    /** @brief Safe ID of the sandbox thread. @n
     *         This is intended to be a display name for the sandbox thread.
     *         This name should be chosen by the application in such a way,
     *         that a potentially malicious extension/script cannot confuse
     *         the user into accepting permission requests that they would
     *         normally not accept.
     */
    char safe_id[GH_THREAD_MAXSAFEID];

    /** @brief Maximum time to wait to receive a message back after executing
     *         Lua code or functions.
     */
    int default_timeout_ms;
} gh_threadoptions;

/** @brief Construct a new sandbox thread.
 *
 * @note This function spawns a new subjail process.
 *
 * @param thread   Pointer to unconstructed memory that will hold the new instance.
 * @param options  Sandbox thread options.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_ctor(gh_thread * thread, gh_threadoptions options);

/** @brief Destroy a sandbox thread.
 *
 * @param thread Pointer to a sandbox thread.
 * @param[out] out_subjailresult If not `NULL`, will contain the result code of the
 *                               subjail shutdown process. For safety (to avoid the
 *                               subjail process accidentally having an effect on the
 *                               control flow of the application), this result code
 *                               is separate from the function's result code.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_dtor(gh_thread * thread, gh_result * out_subjailresult);

/** @brief Attach userdata to a sandbox thread.
 *
 * @param thread Pointer to a sandbox thread.
 * @param userdata Arbitrary userdata.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_attachuserdata(gh_thread * thread, void * userdata);

gh_result gh_thread_process(gh_thread * thread, gh_threadnotif * notif);

gh_result gh_thread_runstring(gh_thread * thread, const char * s, size_t s_len, int * script_id);

/** @brief Run Lua string in sandbox thread.
 *
 * @warning The maximum buffer size is limited by @ref GH_IPCMSG_LUASTRING_MAXSIZE.
 *
 * @param thread Pointer to a sandbox thread.
 * @param s      Pointer to a string containing Lua code.
 * @param s_len  Length (without null terminator) of @p s.
 * @param[out] out_status If not `NULL`, will contain the result of the script.
 *                        Lua errors will *not* be reported through the return value.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_runstringsync(gh_thread * thread, const char * s, size_t s_len, gh_threadnotif_script * out_status);

gh_result gh_thread_runfile(gh_thread * thread, int fd, int * script_id);

/** @brief Run Lua file in sandbox thread.
 *
 * @warning Ensure that @p fd is read-only, otherwise the subjail process
 *          will be able to modify it.
 *
 *
 * @param thread Pointer to a sandbox thread.
 * @param fd     File descriptor to the file.
 * @param[out] out_status If not `NULL`, will contain the result of the script.
 *                        Lua errors will *not* be reported through the return value.
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_runfilesync(gh_thread * thread, int fd, gh_threadnotif_script * out_status);

/** @brief Set host variable.
 *
 * @note Host variables are accessible from Lua through the `ghost.hostvars` table.
 *
 * @param thread Pointer to a sandbox thread.
 * @param name   Null terminated name of the host variable.
 * @param value  Value.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_setint(gh_thread * thread, const char * name, int value);

/** @brief Set host variable.
 *
 * @note Host variables are accessible from Lua through the `ghost.hostvars` table.
 *
 * @param thread Pointer to a sandbox thread.
 * @param name   Null terminated name of the host variable.
 * @param value  Value.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_setdouble(gh_thread * thread, const char * name, double value);

/** @brief Set host variable.
 *
 * @note Host variables are accessible from Lua through the `ghost.hostvars` table.
 *
 * @param thread Pointer to a sandbox thread.
 * @param name   Null terminated name of the host variable.
 * @param string String contents.
 * @param len    Length of string (excluding null terminator).
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_setlstring(gh_thread * thread, const char * name, const char * string, size_t len);

/** @brief Set host variable.
 *
 * @note Host variables are accessible from Lua through the `ghost.hostvars` table.
 *
 * @param thread Pointer to a sandbox thread.
 * @param name   Null terminated name of the host variable.
 * @param string Pointer to null terminated string.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_setstring(gh_thread * thread, const char * name, const char * string);

/** @brief Set host variable (array of strings).
 *
 * @note Host variables are accessible from Lua through the `ghost.hostvars` table.
 *
 * @param thread  Pointer to a sandbox thread.
 * @param name    Null terminated name of the host variable.
 * @param strings Pointer to contiguous array of pointers to null terminated strings.
 * @param count   Number of pointers to strings in the array.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_setstringtable(gh_thread * thread, const char * name, const char * const * strings, int count);

/** @brief Remote Lua function call frame. */
typedef struct {
    /** @brief Number of parameters. */
    size_t param_count;
    /** @brief FDMEM shared memory. */
    gh_fdmem fdmem;
    /** @brief Array of FDMEM virtual pointers to parameters. */
    gh_fdmem_ptr param_ptrs[GH_IPCMSG_LUACALL_MAXPARAMS];

    /** @brief Set to true after the function has returned. */
    bool returned;

    /** @brief Value returned by the function. */
    gh_variant * return_value;
} gh_thread_callframe;

/** @brief Construct a new remote Lua call frame.
 *
 * @param frame    Pointer to unconstructed memory that will hold the new instance.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_callframe_ctor(gh_thread_callframe * frame);

/** @brief Destroy a remote Lua call frame.
 *
 * @param frame  Pointer to remote Lua call frame.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_callframe_dtor(gh_thread_callframe * frame);

/** @brief Add argument to Lua call frame.
 *
 * @param frame    Pointer to remote Lua call frame.
 * @param value    Value.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_callframe_int(gh_thread_callframe * frame, int value);

/** @brief Add argument to Lua call frame.
 *
 * @param frame    Pointer to remote Lua call frame.
 * @param value    Value.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_callframe_double(gh_thread_callframe * frame, double value);

/** @brief Add argument to Lua call frame.
 *
 * @param frame  Pointer to remote Lua call frame.
 * @param len    Length of string (excluding null terminator).
 * @param string String contents.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_callframe_lstring(gh_thread_callframe * frame, size_t len, const char * string);

/** @brief Add argument to Lua call frame.
 *
 * @param frame  Pointer to remote Lua call frame.
 * @param string Pointer to null terminated string.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_callframe_string(gh_thread_callframe * frame, const char * string);

/** @brief Retrieve return value from remote Lua function call as int.
 *
 * @param frame  Pointer to remote Lua call frame.
 * @param[out] out_int Will hold the return value.
 *
 * @return True if succeeded, otherwise false (e.g. if function returned `nil`
 *         or another type).
 */
bool gh_thread_callframe_getint(gh_thread_callframe * frame, int * out_int);

/** @brief Retrieve return value from remote Lua function call as double.
 *
 * @param frame  Pointer to remote Lua call frame.
 * @param[out] out_double Will hold the return value.
 *
 * @return True if succeeded, otherwise false (e.g. if function returned `nil`
 *         or another type).
 */
bool gh_thread_callframe_getdouble(gh_thread_callframe * frame, double * out_double);

/** @brief Retrieve return value from remote Lua function call as string.
 *
 * @param frame  Pointer to remote Lua call frame.
 * @param[out] out_len Will hold the length of the string (excluding null terminator).
 * @param[out] out_string Will hold pointer to string contents.
 *
 * @return True if succeeded, otherwise false (e.g. if function returned `nil`
 *         or another type).
 */
bool gh_thread_callframe_getlstring(gh_thread_callframe * frame, size_t * out_len, const char ** out_string);

/** @brief Retrieve return value from remote Lua function call as string.
 *
 * @param frame  Pointer to remote Lua call frame.
 * @param[out] out_string Will hold pointer to null terminated string.
 *
 * @return True if succeeded, otherwise false (e.g. if function returned `nil`
 *         or another type).
 */
bool gh_thread_callframe_getstring(gh_thread_callframe * frame, const char ** out_string);

gh_result gh_thread_callframe_loadreturnvalue(gh_thread_callframe * frame, gh_fdmem_ptr return_value_ptr);

/** @brief Call remote Lua function.
 *
 * @param thread  Pointer to a sandbox thread.
 * @param name    Null terminated name.
 * @param frame   Remote Lua call frame.
 * @param[out] out_status If not `NULL`, will contain the result of the function.
 *                        Lua errors will *not* be reported through the return value.
 *                        To retrieve the return value, use `gh_thread_callframe_get*` functions.
 *
 * @return @ref GHR_OK on success or a result code indicating an error.
 */
gh_result gh_thread_call(gh_thread * thread, const char * name, gh_thread_callframe * frame, gh_threadnotif_script * out_status);


#ifdef __cplusplus
}
#endif

#endif

/** @} */
