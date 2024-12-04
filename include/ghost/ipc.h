/** @defgroup ipc IPC
 *
 * @brief Inter-process communication primitive with the ability to send/receive various messages and transfer file descriptors.
 *
 * @{
 */

#ifndef GHOST_IPC_H
#define GHOST_IPC_H

#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <ghost/result.h>
#include <ghost/alloc.h>
#include <ghost/fdmem.h>
#include <ghost/byte_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GH_CONCAT2(A,B) A##B
#define GH_CONCAT(A,B) GH_CONCAT2(A,B)
#define GH_STATICASSERT(cond, msg) typedef char GH_CONCAT(gh_static_assert__, __LINE__) [(cond) ? 1 : -1]

#define GH_IPCMSG_MAXSIZE (1024 * 10)
#define GH_IPCMSG_BUFFER(name) char name [GH_IPCMSG_MAXSIZE]

#define GH_IPCMSG_CDATAMAXSIZE sizeof(int)

#define GH_IPCMSG_ALIGN __attribute__((aligned(8)))

#define GH_IPC_HUPTIMEOUTMS 1000
#define GH_IPC_NOTIMEOUT 0

typedef enum {
    GH_IPCMODE_CONTROLLER,
    GH_IPCMODE_CHILD
} gh_ipc_mode;

typedef struct {
    gh_ipc_mode mode;
    int sockfd;
} gh_ipc;

typedef enum {
    // jail+subjail recv
    GH_IPCMSG_HELLO,
    GH_IPCMSG_QUIT,

    // jail recv
    GH_IPCMSG_NEWSUBJAIL,

    // subjail recv
    GH_IPCMSG_LUASTRING,
    GH_IPCMSG_LUAFILE,
    GH_IPCMSG_LUAHOSTVARIABLE,
    GH_IPCMSG_LUACALL,
    GH_IPCMSG_FUNCTIONRETURN,

    // subjail send
    GH_IPCMSG_SUBJAILALIVE,
    GH_IPCMSG_LUAINFO,
    GH_IPCMSG_LUARESULT,
    GH_IPCMSG_FUNCTIONCALL
} gh_ipcmsg_type;

GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    char data[];
} gh_ipcmsg;

GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    pid_t pid;
} gh_ipcmsg_hello;

GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
} gh_ipcmsg_quit;

GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    int sockfd;
} gh_ipcmsg_newsubjail;

GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    pid_t pid;
} gh_ipcmsg_subjaildead;

GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    int index;
    pid_t pid;
} gh_ipcmsg_subjailalive;

#define GH_IPCMSG_LUASTRING_MAXSIZE (GH_IPCMSG_MAXSIZE - sizeof(gh_ipcmsg_type))
GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    char content[GH_IPCMSG_LUASTRING_MAXSIZE];
} gh_ipcmsg_luastring;

GH_STATICASSERT(
    sizeof(gh_ipcmsg_luastring) <= GH_IPCMSG_MAXSIZE,
    "Lua string message exceeds max IPC message size"
);

#define GH_IPCMSG_LUAFILE_CHUNKNAMEMAX 512
GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    int fd;
    char chunk_name[GH_IPCMSG_LUAFILE_CHUNKNAMEMAX];
} gh_ipcmsg_luafile;

typedef enum {
    GH_IPCMSG_LUAHOSTVARIABLE_INT,
    GH_IPCMSG_LUAHOSTVARIABLE_DOUBLE,
    GH_IPCMSG_LUAHOSTVARIABLE_STRING,
} gh_ipcmsg_luahostvariable_type;

#define GH_IPCMSG_LUAHOSTVARIABLE_NAMEMAX 128
#define GH_IPCMSG_LUAHOSTVARIABLE_STRINGMAX 1024
GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    gh_ipcmsg_luahostvariable_type datatype;
    char name[GH_IPCMSG_LUAHOSTVARIABLE_NAMEMAX];
    union {
        struct {
            char buffer[GH_IPCMSG_LUAHOSTVARIABLE_STRINGMAX];
            size_t len;
        } t_string;
        int t_integer;
        double t_double;
    };
    int table_index;
} gh_ipcmsg_luahostvariable;

GH_STATICASSERT(
    sizeof(gh_ipcmsg_luahostvariable) <= GH_IPCMSG_MAXSIZE,
    "Lua host variable message exceeds max IPC message size"
);


#define GH_IPCMSG_LUACALL_NAMEMAX 128
#define GH_IPCMSG_LUACALL_MAXPARAMS 16
GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    int ipcfdmem_fd;
    size_t ipcfdmem_occupied;
    char name[GH_IPCMSG_LUACALL_NAMEMAX];
    gh_fdmem_ptr params[GH_IPCMSG_LUACALL_MAXPARAMS];
} gh_ipcmsg_luacall;

GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    int script_id;
} gh_ipcmsg_luainfo;

#define GH_IPCMSG_LUARESULT_ERRORMSGMAX 1024
GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    gh_result result;
    char error_msg[GH_IPCMSG_LUARESULT_ERRORMSGMAX];
    int script_id;

    // only filled in for response to LUACALL
    gh_fdmem_ptr return_ptr;
} gh_ipcmsg_luaresult;

GH_STATICASSERT(
    sizeof(gh_ipcmsg_luaresult) <= GH_IPCMSG_MAXSIZE,
    "Lua result message exceeds max IPC message size"
);

typedef struct {
    uintptr_t addr;
    size_t size;
} gh_ipcmsg_functioncall_arg;

#define GH_IPCMSG_FUNCTIONCALL_MAXARGS 16
#define GH_IPCMSG_FUNCTIONCALL_MAXNAME 256

GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    char name[GH_IPCMSG_FUNCTIONCALL_MAXNAME];
    gh_ipcmsg_functioncall_arg return_arg;
    size_t arg_count;
    gh_ipcmsg_functioncall_arg args[GH_IPCMSG_FUNCTIONCALL_MAXARGS];
} gh_ipcmsg_functioncall;

GH_STATICASSERT(
    sizeof(gh_ipcmsg_functioncall) < GH_IPCMSG_MAXSIZE,
    "Function call message exceeds max IPC message size"
);

GH_IPCMSG_ALIGN
typedef struct {
    gh_ipcmsg_type type;
    int fd;
    gh_result result;
} gh_ipcmsg_functionreturn;

/** @brief Constructs an IPC object.
 *
 * @par This function creates an anonymous socket and associates it with a newly constructed IPC object.
 *      One side of the socket should create an IPC object with this function. The other side of the socket should use @ref gh_ipc_ctorconnect with the value stored in @ref out_peerfd to connect to the socket.
 *      The IPC object created in this fashion is in @ref GH_IPCMODE_CONTROLLER mode.
 *
 * @par After calling this function and passing the value stored in @ref out_peerfd to another process (either by fork or CMSG), the file descriptor should be closed manually by the caller of this function.
 *
 * @param ipc        Pointer to the uninitialized IPC object.
 * @param out_peerfd Output parameter holding the file descriptor of the peer for use with @ref gh_ipc_ctorconnect.
 *
 * @return Result code.
 */
gh_result gh_ipc_ctor(gh_ipc * ipc, int * out_peerfd);

/** @brief Constructs an IPC object by connecting to an existing socket.
 *
 *  @par The IPC object created in this fashion is in @ref GH_IPCMODE_CHILD mode.
 *
 * @param ipc     Pointer to the uninitialized IPC object.
 * @param sockfd  File descriptor of the open socket.
 *
 * @return Result code.
 */
gh_result gh_ipc_ctorconnect(gh_ipc * ipc, int sockfd);

/** @brief Destroys an IPC object.
 *
 * @par Memory allocated for the object is **not** freed by this function.
 *
 * @param ipc Pointer to the IPC object.
 *
 * @return Result code.
 */
gh_result gh_ipc_dtor(gh_ipc * ipc);

/** @brief Sends a message over IPC.
 *
 * @param ipc Pointer to the IPC object.
 * @param msg Pointer to message data with type.
 *
 * @return Result code.
 */
gh_result gh_ipc_send(gh_ipc * ipc, gh_ipcmsg * msg, size_t msg_size);

/** @brief Receives a message over IPC.
 *
 * @param ipc         Pointer to the IPC object.
 * @param msg         Pointer to the buffer that will contain the message.
 *                    Must be a block of memory of size at least GH_IPCMSG_MAXSIZE
 *                    (use @ref GH_IPCMSG_BUFFER).
 * @param timeout_ms  Timeout in milliseconds.
 *                    If reached before a message is available on the socket, the function will return @ref GHR_IPC_RECVMSGTIMEOUT.
 *                    If 0, there is no timeout.
 *
 * @return Result code.
 */
gh_result gh_ipc_recv(gh_ipc * ipc, gh_ipcmsg * msg, int timeout_ms);

gh_result gh_ipc_call(gh_ipc * ipc, const char * name, size_t argc, gh_ipcmsg_functioncall_arg * args, int * return_fd, void * return_arg, size_t return_arg_size);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
