#ifndef CS_TLS_H
#define CS_TLS_H

#include "cupidscript.h"
#include "cs_event_loop.h"

// TLS support can be disabled at compile time
#ifndef CS_NO_TLS

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>

typedef struct cs_tls_state {
    SSL_CTX *ctx;
    SSL *ssl;
    int handshake_complete;
} cs_tls_state;

// Initialize/cleanup OpenSSL
int cs_tls_init(void);
void cs_tls_cleanup(void);

// Get global SSL context
SSL_CTX* cs_tls_get_ctx(void);

// TLS socket operations
SSL* cs_tls_new_ssl(cs_socket_t fd, const char *hostname);
int cs_tls_do_handshake(SSL *ssl);
int cs_tls_read(SSL *ssl, char *buf, int len);
int cs_tls_write(SSL *ssl, const char *buf, int len);
void cs_tls_close(SSL *ssl);

// Certificate verification
int cs_tls_verify_cert(SSL *ssl);

// Register TLS stdlib functions
void cs_register_tls_stdlib(cs_vm *vm);

#else

static inline int cs_tls_init(void) { return -1; }
static inline void cs_tls_cleanup(void) {}
static inline void cs_register_tls_stdlib(cs_vm *vm) { (void)vm; }

#endif // CS_NO_TLS

#endif // CS_TLS_H
