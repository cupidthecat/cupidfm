#include "cs_tls.h"

#ifndef CS_NO_TLS

#include "cs_vm.h"
#include "cs_net.h"
#include <stdlib.h>
#include <string.h>

static SSL_CTX *g_ssl_ctx = NULL;
static int g_tls_initialized = 0;

int cs_tls_init(void) {
    if (g_tls_initialized) return 0;
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    g_ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!g_ssl_ctx) return -1;

    SSL_CTX_set_default_verify_paths(g_ssl_ctx);
    SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_2_VERSION);

    g_tls_initialized = 1;
    return 0;
}

void cs_tls_cleanup(void) {
    if (!g_tls_initialized) return;
    if (g_ssl_ctx) {
        SSL_CTX_free(g_ssl_ctx);
        g_ssl_ctx = NULL;
    }
    g_tls_initialized = 0;
}

SSL_CTX* cs_tls_get_ctx(void) {
    if (!g_tls_initialized) {
        if (cs_tls_init() != 0) return NULL;
    }
    return g_ssl_ctx;
}

SSL* cs_tls_new_ssl(cs_socket_t fd, const char *hostname) {
    SSL_CTX *ctx = cs_tls_get_ctx();
    if (!ctx) return NULL;
    SSL *ssl = SSL_new(ctx);
    if (!ssl) return NULL;
    SSL_set_fd(ssl, (int)fd);
    if (hostname && *hostname) SSL_set_tlsext_host_name(ssl, hostname);
    return ssl;
}

int cs_tls_do_handshake(SSL *ssl) {
    int ret = SSL_connect(ssl);
    if (ret == 1) return 1;
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
    return -1;
}

int cs_tls_read(SSL *ssl, char *buf, int len) {
    return SSL_read(ssl, buf, len);
}

int cs_tls_write(SSL *ssl, const char *buf, int len) {
    return SSL_write(ssl, buf, len);
}

void cs_tls_close(SSL *ssl) {
    if (!ssl) return;
    SSL_shutdown(ssl);
    SSL_free(ssl);
}

int cs_tls_verify_cert(SSL *ssl) {
    X509 *cert = SSL_get_peer_certificate(ssl);
    if (!cert) return -1;
    X509_free(cert);
    long result = SSL_get_verify_result(ssl);
    return (result == X509_V_OK) ? 0 : -1;
}

static cs_value make_error(cs_vm* vm, const char* msg, const char* code) {
    cs_value err = cs_map(vm);
    if (!err.as.p) return cs_nil();
    cs_map_set(err, "msg", cs_str(vm, msg));
    cs_map_set(err, "code", cs_str(vm, code));
    return err;
}

// Native function: tls_connect(host, port) -> promise<socket>
static int nf_tls_connect(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud;
    if (argc < 2) {
        cs_error(vm, "tls_connect() requires 2 arguments (host, port)");
        return 1;
    }
    if (argv[0].type != CS_T_STR || argv[1].type != CS_T_INT) {
        cs_error(vm, "tls_connect() requires (string host, int port)");
        return 1;
    }

    const char *host = cs_to_cstr(argv[0]);
    int port = (int)argv[1].as.i;

    if (cs_tls_init() != 0) {
        cs_error(vm, "tls_connect() failed to initialize TLS");
        return 1;
    }

    struct sockaddr_in addr;
    if (cs_resolve_host(host, port, &addr) != 0) {
        cs_error(vm, "tls_connect() failed to resolve host");
        return 1;
    }

    cs_socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == CS_INVALID_SOCKET) {
        cs_error(vm, "tls_connect() failed to create socket");
        return 1;
    }

    if (cs_socket_set_nonblocking(fd) != 0) {
        cs_socket_close(fd);
        cs_error(vm, "tls_connect() failed to set non-blocking");
        return 1;
    }

    int ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == 0) {
        SSL *ssl = cs_tls_new_ssl(fd, host);
        if (!ssl) {
            cs_socket_close(fd);
            cs_error(vm, "tls_connect() failed to create SSL object");
            return 1;
        }
        int hs = cs_tls_do_handshake(ssl);
        if (hs == 1) {
            if (cs_tls_verify_cert(ssl) != 0) {
                cs_tls_close(ssl);
                cs_socket_close(fd);
                cs_error(vm, "tls_connect() certificate verification failed");
                return 1;
            }
            cs_value sock = cs_make_socket_map(vm, fd, "tcp", host, port);
            cs_map_set(sock, "_tls", cs_int((int64_t)(uintptr_t)ssl));
            cs_map_set(sock, "_secure", cs_bool(1));
            if (out) *out = sock;
            return 0;
        }
        if (hs == 0) {
            cs_value promise = cs_promise_new(vm);
            cs_value sock = cs_make_socket_map(vm, fd, "tcp", host, port);
            cs_value ctx = cs_map(vm);
            cs_map_set(ctx, "_op", cs_str(vm, "tls_connect"));
            cs_map_set(ctx, "sock", sock);
            cs_map_set(ctx, "host", argv[0]);
            cs_map_set(ctx, "_ssl", cs_int((int64_t)(uintptr_t)ssl));
            cs_add_pending_io(vm, fd, CS_POLL_WRITE, promise, ctx, 0);
            cs_value_release(ctx);
            cs_value_release(sock);
            if (out) *out = promise;
            return 0;
        }
        cs_tls_close(ssl);
        cs_socket_close(fd);
        cs_error(vm, "tls_connect() handshake failed");
        return 1;
    }

#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
#else
    if (errno == EINPROGRESS) {
#endif
        cs_value promise = cs_promise_new(vm);
        cs_value sock = cs_make_socket_map(vm, fd, "tcp", host, port);
        cs_value ctx = cs_map(vm);
        cs_map_set(ctx, "_op", cs_str(vm, "tls_connect"));
        cs_map_set(ctx, "sock", sock);
        cs_map_set(ctx, "host", argv[0]);
        cs_add_pending_io(vm, fd, CS_POLL_WRITE, promise, ctx, 0);
        cs_value_release(ctx);
        cs_value_release(sock);
        if (out) *out = promise;
        return 0;
    }

    cs_socket_close(fd);
    cs_error(vm, "tls_connect() connection failed");
    return 1;
}

// Native function: socket_is_secure(sock) -> bool
static int nf_socket_is_secure(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_MAP) {
        *out = cs_bool(0);
        return 0;
    }
    cs_value secure = cs_map_get(argv[0], "_secure");
    int is_secure = (secure.type == CS_T_BOOL && secure.as.b);
    cs_value_release(secure);
    *out = cs_bool(is_secure);
    return 0;
}

// Native function: tls_upgrade(sock) -> promise<nil>
static int nf_tls_upgrade(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud;
    if (argc < 1 || argv[0].type != CS_T_MAP) {
        cs_error(vm, "tls_upgrade() requires a socket argument");
        return 1;
    }

    cs_socket_t fd = cs_socket_map_fd(argv[0]);
    if (fd == CS_INVALID_SOCKET) {
        cs_error(vm, "tls_upgrade() invalid socket");
        return 1;
    }

    cs_value secure = cs_map_get(argv[0], "_secure");
    if (secure.type == CS_T_BOOL && secure.as.b) {
        cs_value_release(secure);
        if (out) *out = cs_nil();
        return 0;
    }
    cs_value_release(secure);

    if (cs_tls_init() != 0) {
        cs_error(vm, "tls_upgrade() failed to initialize TLS");
        return 1;
    }

    cs_value host_val = cs_map_get(argv[0], "host");
    const char* host = (host_val.type == CS_T_STR) ? cs_to_cstr(host_val) : NULL;
    SSL *ssl = cs_tls_new_ssl(fd, host);
    cs_value_release(host_val);
    if (!ssl) {
        cs_error(vm, "tls_upgrade() failed to create SSL object");
        return 1;
    }

    int hs = cs_tls_do_handshake(ssl);
    if (hs == 1) {
        if (cs_tls_verify_cert(ssl) != 0) {
            cs_tls_close(ssl);
            cs_error(vm, "tls_upgrade() certificate verification failed");
            return 1;
        }
        cs_map_set(argv[0], "_tls", cs_int((int64_t)(uintptr_t)ssl));
        cs_map_set(argv[0], "_secure", cs_bool(1));
        if (out) *out = cs_nil();
        return 0;
    }
    if (hs == 0) {
        cs_value promise = cs_promise_new(vm);
        cs_value ctx = cs_map(vm);
        cs_map_set(ctx, "_op", cs_str(vm, "tls_upgrade"));
        cs_map_set(ctx, "sock", argv[0]);
        cs_map_set(ctx, "_ssl", cs_int((int64_t)(uintptr_t)ssl));
        cs_add_pending_io(vm, fd, CS_POLL_WRITE, promise, ctx, 0);
        cs_value_release(ctx);
        if (out) *out = promise;
        return 0;
    }

    cs_tls_close(ssl);
    cs_error(vm, "tls_upgrade() handshake failed");
    return 1;
}

// Native function: tls_info(sock) -> map
static int nf_tls_info(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud;
    if (!out) return 0;
    if (argc < 1 || argv[0].type != CS_T_MAP) {
        *out = cs_map(vm);
        return 0;
    }
    cs_value tls_val = cs_map_get(argv[0], "_tls");
    if (tls_val.type != CS_T_INT || tls_val.as.i == 0) {
        cs_value_release(tls_val);
        *out = cs_map(vm);
        return 0;
    }

    SSL* ssl = (SSL*)(uintptr_t)tls_val.as.i;
    cs_value info = cs_map(vm);
    if (info.as.p) {
        const char* ver = SSL_get_version(ssl);
        const char* cipher = SSL_get_cipher(ssl);
        if (ver) cs_map_set(info, "version", cs_str(vm, ver));
        if (cipher) cs_map_set(info, "cipher", cs_str(vm, cipher));

        X509* cert = SSL_get_peer_certificate(ssl);
        if (cert) {
            char buf[256];
            X509_NAME_oneline(X509_get_subject_name(cert), buf, sizeof(buf));
            cs_map_set(info, "cert_subject", cs_str(vm, buf));
            X509_free(cert);
        }
    }

    cs_value_release(tls_val);
    *out = info;
    return 0;
}

void cs_register_tls_stdlib(cs_vm *vm) {
    cs_register_native(vm, "tls_connect", nf_tls_connect, NULL);
    cs_register_native(vm, "socket_is_secure", nf_socket_is_secure, NULL);
    cs_register_native(vm, "tls_upgrade", nf_tls_upgrade, NULL);
    cs_register_native(vm, "tls_info", nf_tls_info, NULL);
}

#endif // CS_NO_TLS
