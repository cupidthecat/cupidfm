#ifndef CS_EVENT_LOOP_H
#define CS_EVENT_LOOP_H

#include "cupidscript.h"
#include "cs_value.h"

// Platform detection
#ifdef _WIN32
    #define CS_USE_SELECT 1
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET cs_socket_t;
    #define CS_INVALID_SOCKET INVALID_SOCKET
    #define CS_SOCKET_ERROR SOCKET_ERROR
#else
    #define CS_USE_POLL 1
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <errno.h>
    typedef int cs_socket_t;
    #define CS_INVALID_SOCKET (-1)
    #define CS_SOCKET_ERROR (-1)
#endif

// Poll event flags
#define CS_POLL_READ   1
#define CS_POLL_WRITE  2
#define CS_POLL_ERROR  4

// Pending I/O operation
typedef struct cs_pending_io {
    struct cs_pending_io* next;
    cs_socket_t fd;
    int events;              // CS_POLL_READ, CS_POLL_WRITE
    cs_value promise;        // promise to resolve when ready
    cs_value context;        // socket map or other context
    uint64_t timeout_ms;     // 0 = use default
    uint64_t start_ms;       // when this I/O was scheduled
} cs_pending_io;

// Platform init/cleanup
int cs_event_init(void);
void cs_event_cleanup(void);

// Socket helpers
int cs_socket_set_nonblocking(cs_socket_t fd);
int cs_socket_close(cs_socket_t fd);
int cs_socket_get_error(cs_socket_t fd);
const char* cs_socket_error_str(int err);

// I/O scheduling (called from cs_vm.c)
void cs_add_pending_io(cs_vm* vm, cs_socket_t fd, int events, cs_value promise, cs_value context, uint64_t timeout_ms);
void cs_remove_pending_io(cs_vm* vm, cs_socket_t fd);
int cs_poll_pending_io(cs_vm* vm, int timeout_ms);

#endif
