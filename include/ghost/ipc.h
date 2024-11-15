#ifndef GHOST_IPC_H
#define GHOST_IPC_H

#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <ghost/result.h>
#include <ghost/alloc.h>

#define GH_IPCMSG_MAXSIZE (1024 * 10)
#define GH_IPCMSG_BUFFER(name) \
    char name ## __backing_buf[GH_IPCMSG_MAXSIZE]; \
    gh_ipcmsg * name = (gh_ipcmsg*)name ## __backing_buf
#define GH_IPCMSG_CDATAMAXSIZE sizeof(int)

typedef enum {
    GH_IPCMODE_CONTROLLER,
    GH_IPCMODE_CHILD
} gh_ipc_mode;

typedef struct {
    gh_ipc_mode mode;
    int sockfd;
} gh_ipc;

typedef enum {
    GH_IPCMSG_HELLO,
    GH_IPCMSG_QUIT,
    GH_IPCMSG_NEWSUBJAIL,
    GH_IPCMSG_SUBJAILALIVE,
} gh_ipcmsg_type;

typedef struct {
    gh_ipcmsg_type type;
    char data[];
} gh_ipcmsg;

typedef struct {
    gh_ipcmsg_type type;
    pid_t pid;
} gh_ipcmsg_hello;

typedef struct {
    gh_ipcmsg_type type;
} gh_ipcmsg_quit;

typedef struct {
    gh_ipcmsg_type type;
    int sockfd;
} gh_ipcmsg_newsubjail;

typedef struct {
    gh_ipcmsg_type type;
    int index;
} gh_ipcmsg_subjailalive;

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

#endif
