#include "cs_net.h"
#include "cs_vm.h"
#include "cs_value.h"
#include "cs_tls.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void net_errorf(cs_vm* vm, const char* prefix, const char* err) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: %s", prefix, err ? err : "error");
    cs_error(vm, buf);
}

static cs_value net_error_obj(cs_vm* vm, const char* msg, const char* code) {
    cs_value err = cs_map(vm);
    if (!err.as.p) return cs_nil();
    cs_map_set(err, "msg", cs_str(vm, msg));
    cs_map_set(err, "code", cs_str(vm, code));
    return err;
}

int cs_resolve_host(const char* host, int port, struct sockaddr_in* addr) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr->sin_addr) == 1) {
        return 0;
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        return -1;
    }

    struct sockaddr_in* resolved = (struct sockaddr_in*)res->ai_addr;
    addr->sin_addr = resolved->sin_addr;
    freeaddrinfo(res);
    return 0;
}

cs_value cs_make_socket_map(cs_vm* vm, cs_socket_t fd, const char* type, const char* host, int port) {
    cs_value sock = cs_map(vm);
    if (!sock.as.p) return cs_nil();

    cs_map_set(sock, "_fd", cs_int((int64_t)fd));
    cs_map_set(sock, "_type", cs_str(vm, type));
    if (host) cs_map_set(sock, "host", cs_str(vm, host));
    if (port > 0) cs_map_set(sock, "port", cs_int(port));
    return sock;
}

cs_socket_t cs_socket_map_fd(cs_value sock) {
    if (sock.type != CS_T_MAP) return CS_INVALID_SOCKET;
    cs_value fd_val = cs_map_get(sock, "_fd");
    if (fd_val.type != CS_T_INT) {
        cs_value_release(fd_val);
        return CS_INVALID_SOCKET;
    }
    cs_socket_t fd = (cs_socket_t)fd_val.as.i;
    cs_value_release(fd_val);
    return fd;
}

static cs_value make_op_context(cs_vm* vm, const char* op, cs_value sock) {
    cs_value ctx = cs_map(vm);
    if (!ctx.as.p) return cs_nil();
    cs_map_set(ctx, "_op", cs_str(vm, op));
    cs_map_set(ctx, "sock", sock);
    return ctx;
}

// Native function: tcp_connect(host, port) -> promise<socket>
static int nf_tcp_connect(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 2) {
        cs_error(vm, "tcp_connect() requires 2 arguments (host, port)");
        return 1;
    }
    if (argv[0].type != CS_T_STR) {
        cs_error(vm, "tcp_connect() host must be a string");
        return 1;
    }
    if (argv[1].type != CS_T_INT) {
        cs_error(vm, "tcp_connect() port must be an integer");
        return 1;
    }

    const char* host = cs_to_cstr(argv[0]);
    int port = (int)argv[1].as.i;

    cs_event_init();

    struct sockaddr_in addr;
    if (cs_resolve_host(host, port, &addr) != 0) {
        cs_value promise = cs_promise_new(vm);
        cs_value err = net_error_obj(vm, "tcp_connect() failed to resolve host", "NET_RESOLVE");
        cs_promise_reject(vm, promise, err);
        cs_value_release(err);
        if (out) *out = promise;
        return 0;
    }

    cs_socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == CS_INVALID_SOCKET) {
        cs_value promise = cs_promise_new(vm);
        cs_value err = net_error_obj(vm, "tcp_connect() failed to create socket", "NET_CONNECT");
        cs_promise_reject(vm, promise, err);
        cs_value_release(err);
        if (out) *out = promise;
        return 0;
    }

    if (cs_socket_set_nonblocking(fd) != 0) {
        cs_socket_close(fd);
        cs_value promise = cs_promise_new(vm);
        cs_value err = net_error_obj(vm, "tcp_connect() failed to set non-blocking", "NET_CONNECT");
        cs_promise_reject(vm, promise, err);
        cs_value_release(err);
        if (out) *out = promise;
        return 0;
    }

    int ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == 0) {
        if (out) *out = cs_make_socket_map(vm, fd, "tcp", host, port);
        return 0;
    }

#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
#else
    if (errno == EINPROGRESS) {
#endif
        cs_value promise = cs_promise_new(vm);
        cs_value sock = cs_make_socket_map(vm, fd, "tcp", host, port);
        cs_value ctx = make_op_context(vm, "connect", sock);
        cs_add_pending_io(vm, fd, CS_POLL_WRITE, promise, ctx, 0);
        cs_value_release(ctx);
        cs_value_release(sock);
        if (out) *out = promise;
        return 0;
    }

    cs_socket_close(fd);
#ifdef _WIN32
    int err2 = WSAGetLastError();
#else
    int err2 = errno;
#endif
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "tcp_connect() failed: %s", cs_socket_error_str(err2));
        cs_value promise = cs_promise_new(vm);
        cs_value err = net_error_obj(vm, buf, "NET_CONNECT");
        cs_promise_reject(vm, promise, err);
        cs_value_release(err);
        if (out) *out = promise;
    }
    return 0;
}

// Native function: socket_send(sock, data) -> promise<int>
static int nf_socket_send(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 2) {
        cs_error(vm, "socket_send() requires 2 arguments (sock, data)");
        return 1;
    }
    if (argv[0].type != CS_T_MAP) {
        cs_error(vm, "socket_send() first argument must be a socket");
        return 1;
    }
    if (argv[1].type != CS_T_STR) {
        cs_error(vm, "socket_send() data must be a string");
        return 1;
    }

    cs_socket_t fd = cs_socket_map_fd(argv[0]);
    if (fd == CS_INVALID_SOCKET) {
        cs_error(vm, "socket_send() invalid socket");
        return 1;
    }

    const char* data = cs_to_cstr(argv[1]);
    size_t len = strlen(data);

    ssize_t sent = -1;
#ifndef CS_NO_TLS
    cs_value tls_val = cs_map_get(argv[0], "_tls");
    if (tls_val.type == CS_T_INT && tls_val.as.i != 0) {
        SSL* ssl = (SSL*)(uintptr_t)tls_val.as.i;
        sent = SSL_write(ssl, data, (int)len);
        if (sent <= 0) {
            int err = SSL_get_error(ssl, (int)sent);
            cs_value_release(tls_val);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                cs_value promise = cs_promise_new(vm);
                cs_value ctx = make_op_context(vm, "send", argv[0]);
                cs_map_set(ctx, "data", argv[1]);
                int events = (err == SSL_ERROR_WANT_READ) ? CS_POLL_READ : CS_POLL_WRITE;
                cs_add_pending_io(vm, fd, events, promise, ctx, 0);
                cs_value_release(ctx);
                if (out) *out = promise;
                return 0;
            }
            cs_error(vm, "socket_send() TLS write failed");
            return 1;
        }
        cs_value_release(tls_val);
    } else {
        cs_value_release(tls_val);
        sent = send(fd, data, len, 0);
    }
#else
    sent = send(fd, data, len, 0);
#endif

    if (sent >= 0) {
        if (out) *out = cs_int(sent);
        return 0;
    }

#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) {
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
        cs_value promise = cs_promise_new(vm);
        cs_value ctx = make_op_context(vm, "send", argv[0]);
        cs_map_set(ctx, "data", argv[1]);
        cs_add_pending_io(vm, fd, CS_POLL_WRITE, promise, ctx, 0);
        cs_value_release(ctx);
        if (out) *out = promise;
        return 0;
    }

        {
    #ifdef _WIN32
        int err2 = WSAGetLastError();
    #else
        int err2 = errno;
    #endif
        net_errorf(vm, "socket_send() failed", cs_socket_error_str(err2));
        }
    return 1;
}

// Native function: socket_recv(sock, max_bytes) -> promise<string>
static int nf_socket_recv(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 2) {
        cs_error(vm, "socket_recv() requires 2 arguments (sock, max_bytes)");
        return 1;
    }
    if (argv[0].type != CS_T_MAP) {
        cs_error(vm, "socket_recv() first argument must be a socket");
        return 1;
    }
    if (argv[1].type != CS_T_INT) {
        cs_error(vm, "socket_recv() max_bytes must be an integer");
        return 1;
    }

    cs_socket_t fd = cs_socket_map_fd(argv[0]);
    if (fd == CS_INVALID_SOCKET) {
        cs_error(vm, "socket_recv() invalid socket");
        return 1;
    }

    int max_bytes = (int)argv[1].as.i;
    if (max_bytes <= 0 || max_bytes > 1024 * 1024) {
        cs_error(vm, "socket_recv() max_bytes must be between 1 and 1048576");
        return 1;
    }

    char* buf = (char*)malloc((size_t)max_bytes + 1);
    if (!buf) {
        cs_error(vm, "socket_recv() out of memory");
        return 1;
    }

    ssize_t received = -1;
#ifndef CS_NO_TLS
    cs_value tls_val = cs_map_get(argv[0], "_tls");
    if (tls_val.type == CS_T_INT && tls_val.as.i != 0) {
        SSL* ssl = (SSL*)(uintptr_t)tls_val.as.i;
        received = SSL_read(ssl, buf, max_bytes);
        if (received <= 0) {
            int err = SSL_get_error(ssl, (int)received);
            cs_value_release(tls_val);
            free(buf);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                cs_value promise = cs_promise_new(vm);
                cs_value ctx = make_op_context(vm, "recv", argv[0]);
                cs_map_set(ctx, "max", argv[1]);
                int events = (err == SSL_ERROR_WANT_READ) ? CS_POLL_READ : CS_POLL_WRITE;
                cs_add_pending_io(vm, fd, events, promise, ctx, 0);
                cs_value_release(ctx);
                if (out) *out = promise;
                return 0;
            }
            cs_error(vm, "socket_recv() TLS read failed");
            return 1;
        }
        cs_value_release(tls_val);
    } else {
        cs_value_release(tls_val);
        received = recv(fd, buf, (size_t)max_bytes, 0);
    }
#else
    received = recv(fd, buf, (size_t)max_bytes, 0);
#endif

    if (received > 0) {
        buf[received] = '\0';
        if (out) *out = cs_str_take(vm, buf, (uint64_t)received);
        return 0;
    }

    free(buf);

    if (received == 0) {
        if (out) *out = cs_nil();
        return 0;
    }

#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) {
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
        cs_value promise = cs_promise_new(vm);
        cs_value ctx = make_op_context(vm, "recv", argv[0]);
        cs_map_set(ctx, "max", argv[1]);
        cs_add_pending_io(vm, fd, CS_POLL_READ, promise, ctx, 0);
        cs_value_release(ctx);
        if (out) *out = promise;
        return 0;
    }

        {
    #ifdef _WIN32
        int err2 = WSAGetLastError();
    #else
        int err2 = errno;
    #endif
        net_errorf(vm, "socket_recv() failed", cs_socket_error_str(err2));
        }
    return 1;
}

// Native function: socket_close(sock) -> nil
static int nf_socket_close_native(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 1) {
        cs_error(vm, "socket_close() requires 1 argument");
        return 1;
    }
    if (argv[0].type != CS_T_MAP) {
        cs_error(vm, "socket_close() argument must be a socket");
        return 1;
    }

    cs_socket_t fd = cs_socket_map_fd(argv[0]);
    if (fd != CS_INVALID_SOCKET) {
        cs_remove_pending_io(vm, fd);
#ifndef CS_NO_TLS
        cs_value tls_val = cs_map_get(argv[0], "_tls");
        if (tls_val.type == CS_T_INT && tls_val.as.i != 0) {
            SSL* ssl = (SSL*)(uintptr_t)tls_val.as.i;
            cs_tls_close(ssl);
        }
        cs_value_release(tls_val);
#endif
        cs_socket_close(fd);
        cs_map_set(argv[0], "_fd", cs_int(-1));
    }

    if (out) *out = cs_nil();
    return 0;
}

// Native function: tcp_listen(host, port) -> socket
static int nf_tcp_listen(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 2) {
        cs_error(vm, "tcp_listen() requires 2 arguments (host, port)");
        return 1;
    }
    if (argv[0].type != CS_T_STR) {
        cs_error(vm, "tcp_listen() host must be a string");
        return 1;
    }
    if (argv[1].type != CS_T_INT) {
        cs_error(vm, "tcp_listen() port must be an integer");
        return 1;
    }

    const char* host = cs_to_cstr(argv[0]);
    int port = (int)argv[1].as.i;

    cs_event_init();

    cs_socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == CS_INVALID_SOCKET) {
        cs_error(vm, "tcp_listen() failed to create socket");
        return 1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    cs_socket_set_nonblocking(fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (strcmp(host, "0.0.0.0") == 0 || strcmp(host, "") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
            cs_socket_close(fd);
            cs_error(vm, "tcp_listen() invalid host");
            return 1;
        }
    }

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        cs_socket_close(fd);
        cs_error(vm, "tcp_listen() bind failed");
        return 1;
    }

    if (listen(fd, 128) != 0) {
        cs_socket_close(fd);
        cs_error(vm, "tcp_listen() listen failed");
        return 1;
    }

    cs_value sock = cs_make_socket_map(vm, fd, "tcp_server", host, port);
    if (out) *out = sock;
    return 0;
}

// Native function: socket_accept(server) -> promise<socket>
static int nf_socket_accept(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 1) {
        cs_error(vm, "socket_accept() requires 1 argument");
        return 1;
    }
    if (argv[0].type != CS_T_MAP) {
        cs_error(vm, "socket_accept() argument must be a server socket");
        return 1;
    }

    cs_socket_t fd = cs_socket_map_fd(argv[0]);
    if (fd == CS_INVALID_SOCKET) {
        cs_error(vm, "socket_accept() invalid socket");
        return 1;
    }

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    cs_socket_t client = accept(fd, (struct sockaddr*)&addr, &addrlen);
    if (client != CS_INVALID_SOCKET) {
        cs_socket_set_nonblocking(client);
        char ipbuf[64];
        const char* ip = inet_ntop(AF_INET, &addr.sin_addr, ipbuf, sizeof(ipbuf));
        cs_value sock = cs_map(vm);
        if (sock.as.p) {
            cs_map_set(sock, "_fd", cs_int((int64_t)client));
            cs_map_set(sock, "_type", cs_str(vm, "tcp"));
            if (ip) cs_map_set(sock, "host", cs_str(vm, ip));
            cs_map_set(sock, "port", cs_int((int)ntohs(addr.sin_port)));
        }
        if (out) *out = sock;
        return 0;
    }

#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) {
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
        cs_value promise = cs_promise_new(vm);
        cs_value ctx = make_op_context(vm, "accept", argv[0]);
        cs_add_pending_io(vm, fd, CS_POLL_READ, promise, ctx, 0);
        cs_value_release(ctx);
        if (out) *out = promise;
        return 0;
    }

    cs_error(vm, "socket_accept() failed");
    return 1;
}

// Native function: net_set_default_timeout(ms) -> nil
static int nf_net_set_default_timeout(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 1 || argv[0].type != CS_T_INT) {
        cs_error(vm, "net_set_default_timeout() requires integer ms");
        return 1;
    }
    int64_t ms = argv[0].as.i;
    if (ms < 0) ms = 0;
    vm->net_default_timeout_ms = (uint64_t)ms;
    if (out) *out = cs_nil();
    return 0;
}

void cs_register_net_stdlib(cs_vm* vm) {
    cs_register_native(vm, "tcp_connect", nf_tcp_connect, NULL);
    cs_register_native(vm, "socket_send", nf_socket_send, NULL);
    cs_register_native(vm, "socket_recv", nf_socket_recv, NULL);
    cs_register_native(vm, "socket_close", nf_socket_close_native, NULL);
    cs_register_native(vm, "tcp_listen", nf_tcp_listen, NULL);
    cs_register_native(vm, "socket_accept", nf_socket_accept, NULL);
    cs_register_native(vm, "net_set_default_timeout", nf_net_set_default_timeout, NULL);
}
