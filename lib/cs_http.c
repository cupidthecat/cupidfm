#include "cs_http.h"
#include "cs_vm.h"
#include "cs_net.h"
#include "cs_tls.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#if defined(_WIN32)
#define strcasecmp _stricmp
#endif

static char* dup_n(const char* s, size_t n) {
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

static char* dup_cstr(const char* s) {
    if (!s) return dup_n("", 0);
    return dup_n(s, strlen(s));
}

static int ensure_buf(char** buf, size_t* cap, size_t need) {
    if (need <= *cap) return 1;
    size_t nc = (*cap == 0) ? 256 : *cap * 2;
    while (nc < need) nc *= 2;
    char* nb = (char*)realloc(*buf, nc);
    if (!nb) return 0;
    *buf = nb;
    *cap = nc;
    return 1;
}

void cs_http_parser_init(cs_http_parser* p, cs_vm* vm) {
    memset(p, 0, sizeof(*p));
    p->state = HTTP_PARSE_STATUS_LINE;
    p->vm = vm;
    p->headers = cs_map(vm);
    p->body_cap = 1024;
    p->body = (char*)malloc(p->body_cap);
    p->line_cap = 256;
    p->line_buf = (char*)malloc(p->line_cap);
}

void cs_http_parser_free(cs_http_parser* p) {
    if (!p) return;
    cs_value_release(p->headers);
    free(p->body);
    free(p->line_buf);
    memset(p, 0, sizeof(*p));
}

static int parse_status_line(cs_http_parser* p, const char* line) {
    // HTTP/1.1 200 OK
    const char* sp = strchr(line, ' ');
    if (!sp) return -1;
    while (*sp == ' ') sp++;
    p->status_code = atoi(sp);
    const char* sp2 = strchr(sp, ' ');
    if (sp2) {
        while (*sp2 == ' ') sp2++;
        strncpy(p->status_text, sp2, sizeof(p->status_text) - 1);
        p->status_text[sizeof(p->status_text) - 1] = 0;
    } else {
        strncpy(p->status_text, "", sizeof(p->status_text));
    }
    return 0;
}

static void parse_header_line(cs_http_parser* p, cs_vm* vm, const char* line) {
    const char* colon = strchr(line, ':');
    if (!colon) return;
    size_t key_len = (size_t)(colon - line);
    while (key_len > 0 && isspace((unsigned char)line[key_len - 1])) key_len--;

    const char* val = colon + 1;
    while (*val && isspace((unsigned char)*val)) val++;

    char* key = (char*)malloc(key_len + 1);
    if (!key) return;
    memcpy(key, line, key_len);
    key[key_len] = 0;

    cs_map_set(p->headers, key, cs_str(vm, val));

    if (strcasecmp(key, "content-length") == 0) {
        p->content_length = (size_t)atoll(val);
    }
    if (strcasecmp(key, "transfer-encoding") == 0 && strcasecmp(val, "chunked") == 0) {
        p->chunked = 1;
    }

    free(key);
}

static int append_body(cs_http_parser* p, const char* data, size_t len) {
    if (len == 0) return 1;
    if (!ensure_buf(&p->body, &p->body_cap, p->body_len + len + 1)) return 0;
    memcpy(p->body + p->body_len, data, len);
    p->body_len += len;
    p->body[p->body_len] = 0;
    return 1;
}

static int parse_line(cs_http_parser* p, const char* line) {
    if (p->state == HTTP_PARSE_STATUS_LINE) {
        if (parse_status_line(p, line) != 0) return -1;
        p->state = HTTP_PARSE_HEADERS;
        return 0;
    }
    if (p->state == HTTP_PARSE_HEADERS) {
        if (line[0] == 0) {
            if (p->chunked || p->content_length > 0) p->state = HTTP_PARSE_BODY;
            else p->state = HTTP_PARSE_DONE;
        } else {
            parse_header_line(p, p->vm, line);
        }
    }
    return 0;
}

int cs_http_parser_feed(cs_http_parser* p, const char* data, size_t len) {
    if (!p || !data || p->state == HTTP_PARSE_ERROR || p->state == HTTP_PARSE_DONE) return 0;

    size_t i = 0;
    while (i < len) {
        if (p->state == HTTP_PARSE_STATUS_LINE || p->state == HTTP_PARSE_HEADERS) {
            char c = data[i++];
            if (c == '\n') {
                if (p->line_len > 0 && p->line_buf[p->line_len - 1] == '\r') p->line_len--;
                p->line_buf[p->line_len] = 0;
                if (parse_line(p, p->line_buf) != 0) { p->state = HTTP_PARSE_ERROR; return -1; }
                p->line_len = 0;
                if (p->state == HTTP_PARSE_DONE) return 1;
            } else {
                if (!ensure_buf(&p->line_buf, &p->line_cap, p->line_len + 2)) { p->state = HTTP_PARSE_ERROR; return -1; }
                p->line_buf[p->line_len++] = c;
            }
            continue;
        }

        if (p->state == HTTP_PARSE_BODY) {
            if (!p->chunked) {
                size_t need = (p->content_length > p->body_len) ? (p->content_length - p->body_len) : 0;
                size_t take = (need > 0 && need < (len - i)) ? need : (len - i);
                if (!append_body(p, data + i, take)) { p->state = HTTP_PARSE_ERROR; return -1; }
                i += take;
                if (p->content_length > 0 && p->body_len >= p->content_length) {
                    p->state = HTTP_PARSE_DONE;
                    return 1;
                }
                continue;
            }

            // Chunked parsing
            if (!p->in_chunk) {
                char c = data[i++];
                if (c == '\n') {
                    if (p->line_len > 0 && p->line_buf[p->line_len - 1] == '\r') p->line_len--;
                    p->line_buf[p->line_len] = 0;
                    p->chunk_remaining = (size_t)strtoul(p->line_buf, NULL, 16);
                    p->line_len = 0;
                    if (p->chunk_remaining == 0) {
                        p->state = HTTP_PARSE_DONE;
                        return 1;
                    }
                    p->in_chunk = 1;
                } else {
                    if (!ensure_buf(&p->line_buf, &p->line_cap, p->line_len + 2)) { p->state = HTTP_PARSE_ERROR; return -1; }
                    p->line_buf[p->line_len++] = c;
                }
                continue;
            }

            size_t take = p->chunk_remaining;
            if (take > (len - i)) take = (len - i);
            if (take > 0) {
                if (!append_body(p, data + i, take)) { p->state = HTTP_PARSE_ERROR; return -1; }
                i += take;
                p->chunk_remaining -= take;
            }
            if (p->chunk_remaining == 0) {
                p->in_chunk = 0;
                // Skip CRLF after chunk if present
                if (i < len && data[i] == '\r') i++;
                if (i < len && data[i] == '\n') i++;
            }
            continue;
        }
    }

    return 1;
}

int cs_http_parser_is_done(cs_http_parser* p) {
    return p && p->state == HTTP_PARSE_DONE;
}

static const char* find_str(const char* s, const char* pat) {
    return strstr(s, pat);
}

cs_value cs_url_parse(cs_vm* vm, const char* url) {
    cs_value result = cs_map(vm);
    if (!result.as.p) return cs_nil();

    if (!url) return result;

    const char* rest = url;
    const char* scheme_end = find_str(url, "://");
    if (scheme_end) {
        char* scheme = dup_n(url, (size_t)(scheme_end - url));
        if (scheme) {
            cs_map_set(result, "scheme", cs_str(vm, scheme));
            free(scheme);
        }
        rest = scheme_end + 3;
    }

    const char* frag = strchr(rest, '#');
    if (frag) {
        cs_map_set(result, "fragment", cs_str(vm, frag + 1));
    }

    const char* query = strchr(rest, '?');
    if (query && (!frag || query < frag)) {
        size_t qlen = frag ? (size_t)(frag - query - 1) : strlen(query + 1);
        char* q = dup_n(query + 1, qlen);
        if (q) {
            cs_map_set(result, "query", cs_str(vm, q));
            free(q);
        }
    }

    const char* auth_end = strpbrk(rest, "/?#");
    const char* after_auth = auth_end ? auth_end : (rest + strlen(rest));
    const char* path_start = (auth_end && *auth_end == '/') ? auth_end : NULL;

    if (path_start) {
        const char* path_end = rest + strlen(rest);
        if (query && query > path_start && query < path_end) path_end = query;
        if (frag && frag > path_start && frag < path_end) path_end = frag;
        size_t plen = (size_t)(path_end - path_start);
        char* p = dup_n(path_start, plen);
        if (p) {
            cs_map_set(result, "path", cs_str(vm, p));
            free(p);
        }
    }

    size_t auth_len = (size_t)(after_auth - rest);

    char* auth = dup_n(rest, auth_len);
    if (!auth) return result;

    char* at = strchr(auth, '@');
    char* hostport = auth;
    if (at) {
        *at = 0;
        hostport = at + 1;
        char* colon = strchr(auth, ':');
        if (colon) {
            *colon = 0;
            cs_map_set(result, "user", cs_str(vm, auth));
            cs_map_set(result, "pass", cs_str(vm, colon + 1));
        } else if (*auth) {
            cs_map_set(result, "user", cs_str(vm, auth));
        }
    }

    char* colon = strchr(hostport, ':');
    if (colon) {
        *colon = 0;
        if (*hostport) cs_map_set(result, "host", cs_str(vm, hostport));
        cs_map_set(result, "port", cs_int((int64_t)atoi(colon + 1)));
    } else if (*hostport) {
        cs_map_set(result, "host", cs_str(vm, hostport));
    }

    cs_value scheme_val = cs_map_get(result, "scheme");
    cs_value port_val = cs_map_get(result, "port");
    if (port_val.type != CS_T_INT) {
        if (scheme_val.type == CS_T_STR && strcmp(cs_to_cstr(scheme_val), "https") == 0) {
            cs_map_set(result, "port", cs_int(443));
        } else if (scheme_val.type == CS_T_STR && strcmp(cs_to_cstr(scheme_val), "http") == 0) {
            cs_map_set(result, "port", cs_int(80));
        }
    }
    cs_value_release(scheme_val);
    cs_value_release(port_val);

    free(auth);
    return result;
}

cs_value cs_url_build(cs_vm* vm, cs_value parts) {
    if (parts.type != CS_T_MAP) return cs_nil();

    cs_value scheme = cs_map_get(parts, "scheme");
    cs_value user = cs_map_get(parts, "user");
    cs_value pass = cs_map_get(parts, "pass");
    cs_value host = cs_map_get(parts, "host");
    cs_value port = cs_map_get(parts, "port");
    cs_value path = cs_map_get(parts, "path");
    cs_value query = cs_map_get(parts, "query");
    cs_value fragment = cs_map_get(parts, "fragment");

    char buf[2048];
    size_t w = 0;

    if (scheme.type == CS_T_STR) w += (size_t)snprintf(buf + w, sizeof(buf) - w, "%s://", cs_to_cstr(scheme));
    if (user.type == CS_T_STR) {
        w += (size_t)snprintf(buf + w, sizeof(buf) - w, "%s", cs_to_cstr(user));
        if (pass.type == CS_T_STR) w += (size_t)snprintf(buf + w, sizeof(buf) - w, ":%s", cs_to_cstr(pass));
        w += (size_t)snprintf(buf + w, sizeof(buf) - w, "@");
    }
    if (host.type == CS_T_STR) w += (size_t)snprintf(buf + w, sizeof(buf) - w, "%s", cs_to_cstr(host));
    if (port.type == CS_T_INT) w += (size_t)snprintf(buf + w, sizeof(buf) - w, ":%lld", (long long)port.as.i);
    if (path.type == CS_T_STR) w += (size_t)snprintf(buf + w, sizeof(buf) - w, "%s", cs_to_cstr(path));
    if (query.type == CS_T_STR) w += (size_t)snprintf(buf + w, sizeof(buf) - w, "?%s", cs_to_cstr(query));
    if (fragment.type == CS_T_STR) w += (size_t)snprintf(buf + w, sizeof(buf) - w, "#%s", cs_to_cstr(fragment));

    cs_value_release(scheme);
    cs_value_release(user);
    cs_value_release(pass);
    cs_value_release(host);
    cs_value_release(port);
    cs_value_release(path);
    cs_value_release(query);
    cs_value_release(fragment);

    return cs_str(vm, buf);
}

static int http_send_all(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int tls_send_all(SSL* ssl, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = SSL_write(ssl, data + sent, (int)(len - sent));
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

// Internal: perform HTTP request
static int nf_http_request(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 1 || argv[0].type != CS_T_MAP) {
        cs_error(vm, "http_request() requires options map");
        return 1;
    }

    cs_value url_val = cs_map_get(argv[0], "url");
    if (url_val.type != CS_T_STR) {
        cs_value_release(url_val);
        cs_error(vm, "http_request() requires url");
        return 1;
    }

    cs_value method_val = cs_map_get(argv[0], "method");
    const char* method = (method_val.type == CS_T_STR) ? cs_to_cstr(method_val) : "GET";

    cs_value parts = cs_url_parse(vm, cs_to_cstr(url_val));
    cs_value scheme = cs_map_get(parts, "scheme");
    cs_value host = cs_map_get(parts, "host");
    cs_value port = cs_map_get(parts, "port");
    cs_value path = cs_map_get(parts, "path");
    cs_value query = cs_map_get(parts, "query");

    const char* host_str = (host.type == CS_T_STR) ? cs_to_cstr(host) : "";
    int port_num = (port.type == CS_T_INT) ? (int)port.as.i : 80;
    const char* scheme_str = (scheme.type == CS_T_STR) ? cs_to_cstr(scheme) : "http";

    char pathbuf[1024];
    pathbuf[0] = 0;
    if (path.type == CS_T_STR) snprintf(pathbuf, sizeof(pathbuf), "%s", cs_to_cstr(path));
    else snprintf(pathbuf, sizeof(pathbuf), "/");
    if (query.type == CS_T_STR) {
        strncat(pathbuf, "?", sizeof(pathbuf) - strlen(pathbuf) - 1);
        strncat(pathbuf, cs_to_cstr(query), sizeof(pathbuf) - strlen(pathbuf) - 1);
    }

    cs_value headers_val = cs_map_get(argv[0], "headers");
    cs_value body_val = cs_map_get(argv[0], "body");

    char req[4096];
    size_t w = 0;
    w += (size_t)snprintf(req + w, sizeof(req) - w, "%s %s HTTP/1.1\r\n", method, pathbuf);
    w += (size_t)snprintf(req + w, sizeof(req) - w, "Host: %s\r\n", host_str);
    w += (size_t)snprintf(req + w, sizeof(req) - w, "Connection: close\r\n");
    if (body_val.type == CS_T_STR) {
        w += (size_t)snprintf(req + w, sizeof(req) - w, "Content-Length: %zu\r\n", strlen(cs_to_cstr(body_val)));
    }
    if (headers_val.type == CS_T_MAP) {
        cs_value keys = cs_map_keys(vm, headers_val);
        if (keys.type == CS_T_LIST) {
            cs_list_obj* l = (cs_list_obj*)keys.as.p;
            for (size_t i = 0; i < l->len; i++) {
                cs_value k = l->items[i];
                cs_value v = cs_map_get(headers_val, cs_to_cstr(k));
                if (k.type == CS_T_STR && v.type == CS_T_STR) {
                    w += (size_t)snprintf(req + w, sizeof(req) - w, "%s: %s\r\n", cs_to_cstr(k), cs_to_cstr(v));
                }
                cs_value_release(v);
            }
        }
        cs_value_release(keys);
    }
    w += (size_t)snprintf(req + w, sizeof(req) - w, "\r\n");

    int use_tls = (strcmp(scheme_str, "https") == 0);

    cs_event_init();

    cs_socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == CS_INVALID_SOCKET) {
        cs_error(vm, "http_request() failed to create socket");
        return 1;
    }

    struct sockaddr_in addr;
    if (cs_resolve_host(host_str, port_num, &addr) != 0) {
        cs_socket_close(fd);
        cs_error(vm, "http_request() failed to resolve host");
        return 1;
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        cs_socket_close(fd);
        cs_error(vm, "http_request() connect failed");
        return 1;
    }

    SSL* ssl = NULL;
#ifndef CS_NO_TLS
    if (use_tls) {
        if (cs_tls_init() != 0) {
            cs_socket_close(fd);
            cs_error(vm, "http_request() TLS init failed");
            return 1;
        }
        ssl = cs_tls_new_ssl(fd, host_str);
        if (!ssl) {
            cs_socket_close(fd);
            cs_error(vm, "http_request() failed to create SSL");
            return 1;
        }
        if (SSL_connect(ssl) != 1) {
            cs_tls_close(ssl);
            cs_socket_close(fd);
            cs_error(vm, "http_request() TLS handshake failed");
            return 1;
        }
        if (cs_tls_verify_cert(ssl) != 0) {
            cs_tls_close(ssl);
            cs_socket_close(fd);
            cs_error(vm, "http_request() TLS verify failed");
            return 1;
        }
    }
#else
    if (use_tls) {
        cs_socket_close(fd);
        cs_error(vm, "http_request() TLS disabled");
        return 1;
    }
#endif

    int send_ok = 0;
    if (use_tls && ssl) send_ok = (tls_send_all(ssl, req, w) == 0);
    else send_ok = (http_send_all(fd, req, w) == 0);
    if (!send_ok) {
#ifndef CS_NO_TLS
        if (ssl) cs_tls_close(ssl);
#endif
        cs_socket_close(fd);
        cs_error(vm, "http_request() send failed");
        return 1;
    }

    if (body_val.type == CS_T_STR) {
        const char* body = cs_to_cstr(body_val);
        size_t body_len = strlen(body);
        if (body_len > 0) {
            if (use_tls && ssl) {
                if (tls_send_all(ssl, body, body_len) != 0) {
#ifndef CS_NO_TLS
                    if (ssl) cs_tls_close(ssl);
#endif
                    cs_socket_close(fd);
                    cs_error(vm, "http_request() send body failed");
                    return 1;
                }
            } else {
                if (http_send_all(fd, body, body_len) != 0) {
                    cs_socket_close(fd);
                    cs_error(vm, "http_request() send body failed");
                    return 1;
                }
            }
        }
    }

    cs_http_parser parser;
    cs_http_parser_init(&parser, vm);

    char buf[4096];
    for (;;) {
        int n = 0;
        if (use_tls && ssl) {
            n = SSL_read(ssl, buf, sizeof(buf));
        } else {
            n = (int)recv(fd, buf, sizeof(buf), 0);
        }
        if (n <= 0) break;
        cs_http_parser_feed(&parser, buf, (size_t)n);
        if (cs_http_parser_is_done(&parser)) break;
    }

#ifndef CS_NO_TLS
    if (ssl) cs_tls_close(ssl);
#endif
    cs_socket_close(fd);

    cs_value resp = cs_map(vm);
    if (resp.as.p) {
        cs_map_set(resp, "status", cs_int(parser.status_code));
        cs_map_set(resp, "status_text", cs_str(vm, parser.status_text));
        cs_map_set(resp, "headers", parser.headers);
        cs_map_set(resp, "body", cs_str_take(vm, parser.body ? parser.body : dup_cstr(""), (uint64_t)parser.body_len));
        parser.body = NULL;
        parser.body_len = 0;
    }

    cs_value_release(url_val);
    cs_value_release(method_val);
    cs_value_release(parts);
    cs_value_release(scheme);
    cs_value_release(host);
    cs_value_release(port);
    cs_value_release(path);
    cs_value_release(query);
    cs_value_release(headers_val);
    cs_value_release(body_val);
    cs_http_parser_free(&parser);

    if (out) *out = resp;
    return 0;
}

static int nf_http_get(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 1 || argv[0].type != CS_T_STR) {
        cs_error(vm, "http_get() requires url string");
        return 1;
    }
    cs_value opts = cs_map(vm);
    cs_map_set(opts, "url", argv[0]);
    cs_map_set(opts, "method", cs_str(vm, "GET"));
    int ret = nf_http_request(vm, ud, 1, &opts, out);
    cs_value_release(opts);
    return ret;
}

static int nf_http_post(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 1 || argv[0].type != CS_T_STR) {
        cs_error(vm, "http_post() requires url string");
        return 1;
    }
    cs_value opts = cs_map(vm);
    cs_map_set(opts, "url", argv[0]);
    cs_map_set(opts, "method", cs_str(vm, "POST"));
    if (argc >= 2 && argv[1].type == CS_T_STR) cs_map_set(opts, "body", argv[1]);
    int ret = nf_http_request(vm, ud, 1, &opts, out);
    cs_value_release(opts);
    return ret;
}

static int nf_http_delete(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 1 || argv[0].type != CS_T_STR) {
        cs_error(vm, "http_delete() requires url string");
        return 1;
    }
    cs_value opts = cs_map(vm);
    cs_map_set(opts, "url", argv[0]);
    cs_map_set(opts, "method", cs_str(vm, "DELETE"));
    int ret = nf_http_request(vm, ud, 1, &opts, out);
    cs_value_release(opts);
    return ret;
}

// Native functions
static int nf_url_parse(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 1 || argv[0].type != CS_T_STR) {
        if (out) *out = cs_map(vm);
        return 0;
    }
    if (out) *out = cs_url_parse(vm, cs_to_cstr(argv[0]));
    return 0;
}

static int nf_url_build(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 1 || argv[0].type != CS_T_MAP) {
        if (out) *out = cs_str(vm, "");
        return 0;
    }
    if (out) *out = cs_url_build(vm, argv[0]);
    return 0;
}

void cs_register_http_stdlib(cs_vm* vm) {
    cs_register_native(vm, "url_parse", nf_url_parse, NULL);
    cs_register_native(vm, "url_build", nf_url_build, NULL);
    cs_register_native(vm, "http_get", nf_http_get, NULL);
    cs_register_native(vm, "http_post", nf_http_post, NULL);
    cs_register_native(vm, "http_delete", nf_http_delete, NULL);
    cs_register_native(vm, "http_request", nf_http_request, NULL);
}
