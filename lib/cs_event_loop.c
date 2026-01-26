#include "cs_event_loop.h"
#include "cs_vm.h"
#include "cs_tls.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Platform init/cleanup
static int g_event_initialized = 0;

int cs_event_init(void) {
    if (g_event_initialized) return 0;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
#endif
    g_event_initialized = 1;
    return 0;
}

void cs_event_cleanup(void) {
    if (!g_event_initialized) return;
#ifdef _WIN32
    WSACleanup();
#endif
    g_event_initialized = 0;
}

int cs_socket_set_nonblocking(cs_socket_t fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ? -1 : 0;
#endif
}

int cs_socket_close(cs_socket_t fd) {
#ifdef _WIN32
    return closesocket(fd);
#else
    return close(fd);
#endif
}

int cs_socket_get_error(cs_socket_t fd) {
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
    return err;
}

const char* cs_socket_error_str(int err) {
#ifdef _WIN32
    static char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, buf, sizeof(buf), NULL);
    return buf;
#else
    return strerror(err);
#endif
}

// Get current time in milliseconds
static uint64_t get_time_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#endif
}

void cs_add_pending_io(cs_vm* vm, cs_socket_t fd, int events, cs_value promise, cs_value context, uint64_t timeout_ms) {
    if (!vm) return;

    cs_pending_io* io = (cs_pending_io*)calloc(1, sizeof(cs_pending_io));
    if (!io) return;

    io->fd = fd;
    io->events = events;
    io->promise = cs_value_copy(promise);
    io->context = cs_value_copy(context);
    io->timeout_ms = timeout_ms;
    io->start_ms = get_time_ms();
    io->next = vm->pending_io;
    vm->pending_io = io;
    vm->pending_io_count++;
}

void cs_remove_pending_io(cs_vm* vm, cs_socket_t fd) {
    if (!vm) return;

    cs_pending_io** pp = &vm->pending_io;
    while (*pp) {
        if ((*pp)->fd == fd) {
            cs_pending_io* dead = *pp;
            *pp = dead->next;
            cs_value_release(dead->promise);
            cs_value_release(dead->context);
            free(dead);
            vm->pending_io_count--;
            continue;
        }
        pp = &(*pp)->next;
    }
}

static cs_value make_error(cs_vm* vm, const char* msg, const char* code) {
    cs_value err = cs_map(vm);
    if (!err.as.p) return cs_nil();
    cs_map_set(err, "msg", cs_str(vm, msg));
    cs_map_set(err, "code", cs_str(vm, code));
    return err;
}

static uint64_t pending_timeout_ms(cs_vm* vm, const cs_pending_io* io) {
    if (!io) return 0;
    if (io->timeout_ms > 0) return io->timeout_ms;
    if (!vm || vm->net_default_timeout_ms == 0) return 30000;
    return vm->net_default_timeout_ms;
}

static int resolve_pending(cs_vm* vm, cs_pending_io* io, cs_value value) {
    if (!vm || !io) {
        cs_value_release(value);
        return 0;
    }
    int ok = cs_promise_resolve(vm, io->promise, value);
    cs_value_release(value);
    return ok;
}

static int reject_pending(cs_vm* vm, cs_pending_io* io, const char* msg, const char* code) {
    if (!vm || !io) return 0;
    cs_value err = make_error(vm, msg, code);
    int ok = cs_promise_reject(vm, io->promise, err);
    cs_value_release(err);
    return ok;
}

static int handle_send_ready(cs_vm* vm, cs_pending_io* io) {
    cs_value ctx = io->context;
    cs_value sock = cs_map_get(ctx, "sock");
    cs_value data_val = cs_map_get(ctx, "data");
    if (sock.type != CS_T_MAP || data_val.type != CS_T_STR) {
        cs_value_release(sock);
        cs_value_release(data_val);
        return reject_pending(vm, io, "socket_send() invalid context", "NET_SEND");
    }

    const char* data = cs_to_cstr(data_val);
    size_t len = strlen(data);

    ssize_t sent = -1;
#ifndef CS_NO_TLS
    cs_value tls_val = cs_map_get(sock, "_tls");
    if (tls_val.type == CS_T_INT && tls_val.as.i != 0) {
        SSL* ssl = (SSL*)(uintptr_t)tls_val.as.i;
        sent = SSL_write(ssl, data, (int)len);
        if (sent <= 0) {
            int err = SSL_get_error(ssl, (int)sent);
            cs_value_release(tls_val);
            cs_value_release(sock);
            cs_value_release(data_val);
            if (err == SSL_ERROR_WANT_READ) {
                io->events = CS_POLL_READ;
                return 0;
            }
            if (err == SSL_ERROR_WANT_WRITE) {
                io->events = CS_POLL_WRITE;
                return 0;
            }
            return reject_pending(vm, io, "TLS write failed", "TLS_WRITE");
        }
        cs_value_release(tls_val);
    } else {
        cs_value_release(tls_val);
        sent = send(io->fd, data, len, 0);
    }
#else
    sent = send(io->fd, data, len, 0);
#endif

    cs_value_release(sock);
    cs_value_release(data_val);

    if (sent >= 0) {
        return resolve_pending(vm, io, cs_int(sent));
    }

#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) return 0;
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
#endif

    return reject_pending(vm, io, "socket_send() failed", "NET_SEND");
}

static int handle_recv_ready(cs_vm* vm, cs_pending_io* io) {
    cs_value ctx = io->context;
    cs_value sock = cs_map_get(ctx, "sock");
    cs_value max_val = cs_map_get(ctx, "max");
    if (sock.type != CS_T_MAP || max_val.type != CS_T_INT) {
        cs_value_release(sock);
        cs_value_release(max_val);
        return reject_pending(vm, io, "socket_recv() invalid context", "NET_RECV");
    }

    int max_bytes = (int)max_val.as.i;
    if (max_bytes <= 0 || max_bytes > 1024 * 1024) max_bytes = 4096;

    char* buf = (char*)malloc((size_t)max_bytes + 1);
    if (!buf) {
        cs_value_release(sock);
        cs_value_release(max_val);
        return reject_pending(vm, io, "out of memory", "NET_RECV");
    }

    ssize_t received = -1;
#ifndef CS_NO_TLS
    cs_value tls_val = cs_map_get(sock, "_tls");
    if (tls_val.type == CS_T_INT && tls_val.as.i != 0) {
        SSL* ssl = (SSL*)(uintptr_t)tls_val.as.i;
        received = SSL_read(ssl, buf, max_bytes);
        if (received <= 0) {
            int err = SSL_get_error(ssl, (int)received);
            cs_value_release(tls_val);
            cs_value_release(sock);
            cs_value_release(max_val);
            free(buf);
            if (err == SSL_ERROR_WANT_READ) {
                io->events = CS_POLL_READ;
                return 0;
            }
            if (err == SSL_ERROR_WANT_WRITE) {
                io->events = CS_POLL_WRITE;
                return 0;
            }
            return reject_pending(vm, io, "TLS read failed", "TLS_READ");
        }
        cs_value_release(tls_val);
    } else {
        cs_value_release(tls_val);
        received = recv(io->fd, buf, (size_t)max_bytes, 0);
    }
#else
    received = recv(io->fd, buf, (size_t)max_bytes, 0);
#endif

    cs_value_release(sock);
    cs_value_release(max_val);

    if (received > 0) {
        buf[received] = '\0';
        return resolve_pending(vm, io, cs_str_take(vm, buf, (uint64_t)received));
    }
    free(buf);

    if (received == 0) {
        return reject_pending(vm, io, "socket closed", "NET_CLOSED");
    }

#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) return 0;
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
#endif

    return reject_pending(vm, io, "socket_recv() failed", "NET_RECV");
}

static int handle_accept_ready(cs_vm* vm, cs_pending_io* io) {
    cs_value ctx = io->context;
    cs_value server = cs_map_get(ctx, "sock");
    if (server.type != CS_T_MAP) {
        cs_value_release(server);
        return reject_pending(vm, io, "socket_accept() invalid context", "NET_CONNECT");
    }

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    cs_socket_t client = accept(io->fd, (struct sockaddr*)&addr, &addrlen);
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
        cs_value_release(server);
        return resolve_pending(vm, io, sock);
    }

    cs_value_release(server);

#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) return 0;
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
#endif
    return reject_pending(vm, io, "socket_accept() failed", "NET_CONNECT");
}

static int handle_connect_ready(cs_vm* vm, cs_pending_io* io) {
    cs_value ctx = io->context;
    cs_value sock = cs_map_get(ctx, "sock");
    if (sock.type != CS_T_MAP) {
        cs_value_release(sock);
        return reject_pending(vm, io, "tcp_connect() invalid context", "NET_CONNECT");
    }

    int err = cs_socket_get_error(io->fd);
    if (err != 0) {
        cs_value_release(sock);
        return reject_pending(vm, io, cs_socket_error_str(err), "NET_CONNECT");
    }

    return resolve_pending(vm, io, sock);
}

static int handle_tls_handshake(cs_vm* vm, cs_pending_io* io, int is_upgrade) {
#ifdef CS_NO_TLS
    (void)vm; (void)io; (void)is_upgrade;
    return reject_pending(vm, io, "TLS disabled", "HTTP_NO_TLS");
#else
    cs_value ctx = io->context;
    cs_value sock = cs_map_get(ctx, "sock");
    if (sock.type != CS_T_MAP) {
        cs_value_release(sock);
        return reject_pending(vm, io, "tls handshake invalid context", "TLS_HANDSHAKE");
    }

    if (!is_upgrade) {
        int err = cs_socket_get_error(io->fd);
        if (err != 0) {
            cs_value_release(sock);
            return reject_pending(vm, io, cs_socket_error_str(err), "NET_CONNECT");
        }
    }

    SSL* ssl = NULL;
    cs_value ssl_val = cs_map_get(ctx, "_ssl");
    if (ssl_val.type == CS_T_INT && ssl_val.as.i != 0) {
        ssl = (SSL*)(uintptr_t)ssl_val.as.i;
    }

    if (!ssl) {
        cs_value host_val = cs_map_get(ctx, "host");
        const char* host = (host_val.type == CS_T_STR) ? cs_to_cstr(host_val) : NULL;
        ssl = cs_tls_new_ssl(io->fd, host);
        cs_value_release(host_val);
        if (!ssl) {
            cs_value_release(ssl_val);
            cs_value_release(sock);
            return reject_pending(vm, io, "TLS init failed", "TLS_INIT");
        }
        cs_map_set(ctx, "_ssl", cs_int((int64_t)(uintptr_t)ssl));
    }
    cs_value_release(ssl_val);

    int ret = SSL_connect(ssl);
    if (ret == 1) {
        if (cs_tls_verify_cert(ssl) != 0) {
            cs_tls_close(ssl);
            if (!is_upgrade) cs_socket_close(io->fd);
            cs_value_release(sock);
            return reject_pending(vm, io, "certificate verification failed", "TLS_CERT");
        }
        cs_map_set(sock, "_tls", cs_int((int64_t)(uintptr_t)ssl));
        cs_map_set(sock, "_secure", cs_bool(1));
        if (is_upgrade) {
            cs_value_release(sock);
            return resolve_pending(vm, io, cs_nil());
        }
        return resolve_pending(vm, io, sock);
    }

    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ) {
        io->events = CS_POLL_READ;
        cs_value_release(sock);
        return 0;
    }
    if (err == SSL_ERROR_WANT_WRITE) {
        io->events = CS_POLL_WRITE;
        cs_value_release(sock);
        return 0;
    }

    cs_tls_close(ssl);
    if (!is_upgrade) cs_socket_close(io->fd);
    cs_value_release(sock);
    return reject_pending(vm, io, "TLS handshake failed", "TLS_HANDSHAKE");
#endif
}

static int handle_ready_io(cs_vm* vm, cs_pending_io* io) {
    if (!io || io->context.type != CS_T_MAP) {
        return resolve_pending(vm, io, cs_value_copy(io->context));
    }

    cs_value op_val = cs_map_get(io->context, "_op");
    if (op_val.type != CS_T_STR) {
        cs_value_release(op_val);
        return resolve_pending(vm, io, cs_value_copy(io->context));
    }

    const char* op = cs_to_cstr(op_val);
    int result = 0;
    if (strcmp(op, "send") == 0) result = handle_send_ready(vm, io);
    else if (strcmp(op, "recv") == 0) result = handle_recv_ready(vm, io);
    else if (strcmp(op, "accept") == 0) result = handle_accept_ready(vm, io);
    else if (strcmp(op, "connect") == 0) result = handle_connect_ready(vm, io);
    else if (strcmp(op, "tls_connect") == 0) result = handle_tls_handshake(vm, io, 0);
    else if (strcmp(op, "tls_upgrade") == 0) result = handle_tls_handshake(vm, io, 1);
    else result = resolve_pending(vm, io, cs_value_copy(io->context));

    cs_value_release(op_val);
    return result;
}

int cs_poll_pending_io(cs_vm* vm, int timeout_ms) {
    if (!vm || !vm->pending_io) return 0;

    int ready_count = 0;
    uint64_t now = get_time_ms();

#ifdef CS_USE_POLL
    int nfds = 0;
    for (cs_pending_io* io = vm->pending_io; io; io = io->next) nfds++;
    if (nfds == 0) return 0;

    struct pollfd* fds = (struct pollfd*)calloc((size_t)nfds, sizeof(struct pollfd));
    cs_pending_io** ios = (cs_pending_io**)calloc((size_t)nfds, sizeof(cs_pending_io*));
    if (!fds || !ios) { free(fds); free(ios); return 0; }

    int i = 0;
    for (cs_pending_io* io = vm->pending_io; io; io = io->next, i++) {
        fds[i].fd = io->fd;
        fds[i].events = 0;
        if (io->events & CS_POLL_READ) fds[i].events |= POLLIN;
        if (io->events & CS_POLL_WRITE) fds[i].events |= POLLOUT;
        if (io->events & CS_POLL_ERROR) fds[i].events |= POLLERR;
        ios[i] = io;
    }

    int ret = poll(fds, (nfds_t)nfds, timeout_ms);

    if (ret > 0) {
        for (i = 0; i < nfds; i++) {
            if (!fds[i].revents) continue;
            cs_pending_io* io = ios[i];
            int resolved = handle_ready_io(vm, io);
            if (resolved) {
                cs_remove_pending_io(vm, io->fd);
                ready_count++;
            }
        }
    }

    for (cs_pending_io* io = vm->pending_io; io; ) {
        cs_pending_io* next = io->next;
        uint64_t timeout = pending_timeout_ms(vm, io);
        if (timeout > 0 && (now - io->start_ms) >= timeout) {
            reject_pending(vm, io, "operation timed out", "NET_TIMEOUT");
            cs_remove_pending_io(vm, io->fd);
            ready_count++;
        }
        io = next;
    }

    free(fds);
    free(ios);
#else
    fd_set read_fds, write_fds, error_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&error_fds);

    cs_socket_t max_fd = 0;
    for (cs_pending_io* io = vm->pending_io; io; io = io->next) {
        if (io->events & CS_POLL_READ) FD_SET(io->fd, &read_fds);
        if (io->events & CS_POLL_WRITE) FD_SET(io->fd, &write_fds);
        if (io->events & CS_POLL_ERROR) FD_SET(io->fd, &error_fds);
        if (io->fd > max_fd) max_fd = io->fd;
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select((int)(max_fd + 1), &read_fds, &write_fds, &error_fds, &tv);

    if (ret > 0) {
        for (cs_pending_io* io = vm->pending_io; io; ) {
            cs_pending_io* next = io->next;
            int ready = 0;
            if ((io->events & CS_POLL_READ) && FD_ISSET(io->fd, &read_fds)) ready = 1;
            if ((io->events & CS_POLL_WRITE) && FD_ISSET(io->fd, &write_fds)) ready = 1;
            if ((io->events & CS_POLL_ERROR) && FD_ISSET(io->fd, &error_fds)) ready = 1;
            if (ready) {
                int resolved = handle_ready_io(vm, io);
                if (resolved) {
                    cs_remove_pending_io(vm, io->fd);
                    ready_count++;
                }
            }
            io = next;
        }
    }

    for (cs_pending_io* io = vm->pending_io; io; ) {
        cs_pending_io* next = io->next;
        uint64_t timeout = pending_timeout_ms(vm, io);
        if (timeout > 0 && (now - io->start_ms) >= timeout) {
            reject_pending(vm, io, "operation timed out", "NET_TIMEOUT");
            cs_remove_pending_io(vm, io->fd);
            ready_count++;
        }
        io = next;
    }
#endif

    return ready_count;
}
