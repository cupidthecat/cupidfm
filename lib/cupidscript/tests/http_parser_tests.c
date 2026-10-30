#define _POSIX_C_SOURCE 200809L
#include "cs_http.h"
#include "cs_net.h"
#include "cs_tls.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#include <pthread.h>
#endif

static void valid(cs_vm *vm, const char *message, size_t size, const char *body, size_t length, int head) {
    for (size_t split = 0; split <= size; split++) {
        cs_http_parser parser; cs_http_parser_init(&parser, vm); parser.no_body = head;
        assert(cs_http_parser_feed(&parser, message, split) >= 0);
        assert(cs_http_parser_feed(&parser, message + split, size - split) >= 0);
        assert(cs_http_parser_finish(&parser) == 0 && cs_http_parser_is_done(&parser));
        assert(parser.body_len == length && !memcmp(parser.body, body, length));
        cs_http_parser_free(&parser);
    }
    cs_http_parser parser; cs_http_parser_init(&parser, vm); parser.no_body = head;
    for (size_t i = 0; i < size; i++) assert(cs_http_parser_feed(&parser, message+i, 1) >= 0);
    assert(cs_http_parser_finish(&parser) == 0);
    assert(parser.body_len == length && !memcmp(parser.body, body, length));
    cs_http_parser_free(&parser);
}
static void invalid(cs_vm *vm, const char *message) {
    cs_http_parser parser; cs_http_parser_init(&parser, vm);
    int result = cs_http_parser_feed(&parser, message, strlen(message));
    assert(result < 0 || cs_http_parser_finish(&parser) < 0);
    assert(parser.state == HTTP_PARSE_ERROR);
    assert(cs_http_parser_feed(&parser, "x", 1) < 0);
    cs_http_parser_free(&parser);
}
#define VALID(message,body) valid(vm,message,sizeof(message)-1,body,sizeof(body)-1,0)
static void transports(void) {
#ifndef _WIN32
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    close(pair[1]); errno = 0;
    assert(cs_socket_send(pair[0], "x", 1) < 0 && errno == EPIPE); close(pair[0]);
#ifndef CS_NO_TLS
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    SSL *ssl = cs_tls_new_ssl(pair[0], "example.test"); assert(ssl);
    assert(!strcmp(X509_VERIFY_PARAM_get0_host(SSL_get0_param(ssl), 0), "example.test"));
    close(pair[1]);
    sigset_t before, after; assert(pthread_sigmask(SIG_SETMASK, NULL, &before) == 0);
    assert(cs_tls_connect_raw(ssl) <= 0);
    assert(pthread_sigmask(SIG_SETMASK, NULL, &after) == 0);
    assert(sigismember(&before, SIGPIPE) == sigismember(&after, SIGPIPE));
    cs_tls_close(ssl); close(pair[0]);
#if defined(__linux__)
    sigset_t set, previous, pending; sigemptyset(&set); sigaddset(&set, SIGPIPE);
    assert(pthread_sigmask(SIG_BLOCK, &set, &previous) == 0); assert(raise(SIGPIPE) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    ssl = cs_tls_new_ssl(pair[0], "example.test"); assert(ssl); close(pair[1]);
    assert(cs_tls_connect_raw(ssl) <= 0); cs_tls_close(ssl); close(pair[0]);
    assert(sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE));
    int received; assert(sigwait(&set, &received) == 0 && received == SIGPIPE);
    assert(pthread_sigmask(SIG_SETMASK, &previous, NULL) == 0);
#endif
    cs_tls_cleanup();
#endif
#endif
}
int main(void) {
    cs_vm *vm = cs_vm_new(); assert(vm);
    VALID("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", "hello");
    VALID("HTTP/1.0 200 OK\r\nX-Name: value\r\n\r\nclose body", "close body");
    VALID("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", "");
    VALID("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\na\0b", "a\0b");
    VALID("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5;extension=yes\r\npedia\r\n0\r\nChecksum: ok\r\n\r\n", "Wikipedia");
    VALID("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\nLink: x\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", "ok");
    VALID("HTTP/1.1 204 No Content\r\nContent-Length: 999\r\n\r\n", "");
    VALID("HTTP/1.1 304 Not Modified\r\nContent-Length: 999\r\n\r\n", "");
    VALID("HTTP/1.1 200 OK\r\nContent-Length: 2, 2\r\ncontent-length: 2\r\n\r\nok", "ok");
    const char head[] = "HTTP/1.1 200 OK\r\nContent-Length: 999\r\n\r\n";
    valid(vm, head, sizeof(head)-1, "", 0, 1);
    invalid(vm, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nabc");
    invalid(vm, "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n");
    invalid(vm, "HTTP/1.1 200 OK\r\nContent-Length: 2, 3\r\n\r\nabc");
    invalid(vm, "HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n");
    invalid(vm, "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n");
    invalid(vm, "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n");
    invalid(vm, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nG\r\n");
    invalid(vm, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nxX\n0\r\n\r\n");
    invalid(vm, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n");
    invalid(vm, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 0\r\n\r\n");
    invalid(vm, "HTTP/1.1 101 Switching Protocols\r\n\r\n");
    invalid(vm, "HTTP/1.1 200bad\r\n\r\n");
    invalid(vm, "HTTP/1.1 200 OK\r\nBad Header: x\r\n\r\n");
    invalid(vm, "HTTP/1.1 200 OK\r\nX: x");
    cs_vm_free(vm); transports();
    puts("HTTP parser splits, framing, binary bodies, EOF, peer names, and transport signal state passed");
    return 0;
}
