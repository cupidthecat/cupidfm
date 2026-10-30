#include "cs_http.h"
#include "cs_vm.h"
#include "cs_net.h"
#include "cs_tls.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stdarg.h>
#include <ctype.h>
#if !defined(_WIN32)
#include <strings.h>
#endif

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

static int ensure_buf(char** buf, size_t* cap, size_t need) {
    if (*buf && need <= *cap) return 1;
    size_t next = *buf && *cap ? *cap : 256;
    while (next < need) {
        if (next > SIZE_MAX / 2) { next = need; break; }
        next *= 2;
    }
    char* data = (char*)realloc(*buf, next);
    if (!data) return 0;
    *buf = data; *cap = next; return 1;
}

void cs_http_parser_init(cs_http_parser* p, cs_vm* vm) {
    memset(p, 0, sizeof(*p));
    p->vm = vm;
    p->headers = cs_map(vm);
    p->state = HTTP_PARSE_STATUS_LINE;
    if (!p->headers.as.p || !ensure_buf(&p->body, &p->body_cap, 1) ||
        !ensure_buf(&p->line_buf, &p->line_cap, 1)) p->state = HTTP_PARSE_ERROR;
    if (p->body) p->body[0] = 0;
}
void cs_http_parser_free(cs_http_parser* p) {
    if (!p) return;
    cs_value_release(p->headers); free(p->body); free(p->line_buf);
    memset(p, 0, sizeof(*p));
}
static int http_token(const char *s) {
    if (!s || !*s) return 0;
    for (const unsigned char *p = (const unsigned char*)s; *p; p++) {
        if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= 'a' && *p <= 'z') || strchr("!#$%&'*+-.^_`|~", *p)) continue;
        return 0;
    }
    return 1;
}
static int field_value(const char *s) {
    for (const unsigned char *p = (const unsigned char*)s; *p; p++)
        if ((*p < 32 && *p != '\t') || *p == 127) return 0;
    return 1;
}
static int decimal_lengths(const char *s, size_t *result) {
    int seen = 0;
    for (;;) {
        while (*s == ' ' || *s == '\t') s++;
        if (*s < '0' || *s > '9') return -1;
        size_t value = 0;
        do {
            unsigned digit = (unsigned)(*s++ - '0');
            if (value > (SIZE_MAX - digit) / 10) return -1;
            value = value * 10 + digit;
        } while (*s >= '0' && *s <= '9');
        if (seen && value != *result) return -1;
        *result = value; seen = 1;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) return 0;
        if (*s++ != ',') return -1;
    }
}
static int parse_status_line(cs_http_parser *p, const char *line) {
    size_t length = strlen(line);
    if (length < 12 || (strncmp(line, "HTTP/1.0 ", 9) && strncmp(line, "HTTP/1.1 ", 9)) ||
        line[9] < '1' || line[9] > '5' || line[10] < '0' || line[10] > '9' ||
        line[11] < '0' || line[11] > '9' || (line[12] && line[12] != ' ')) return -1;
    p->status_code = (line[9]-'0')*100+(line[10]-'0')*10+line[11]-'0';
    const char *reason = length > 12 ? line + 13 : "";
    if (!field_value(reason)) return -1;
    snprintf(p->status_text, sizeof(p->status_text), "%s", reason);
    return 0;
}
static int parse_header_line(cs_http_parser *p, char *line, int trailer) {
    char *colon = strchr(line, ':');
    if (!colon) return -1;
    *colon = 0;
    if (!http_token(line)) return -1;
    char *value = colon + 1;
    while (*value == ' ' || *value == '\t') value++;
    size_t size = strlen(value);
    while (size && (value[size-1] == ' ' || value[size-1] == '\t')) value[--size] = 0;
    if (!field_value(value)) return -1;
    if (!strcasecmp(line, "content-length")) {
        size_t length = 0;
        if (trailer || decimal_lengths(value, &length) ||
            (p->has_content_length && p->content_length != length)) return -1;
        p->has_content_length = 1; p->content_length = length;
    } else if (!strcasecmp(line, "transfer-encoding")) {
        if (trailer || p->chunked || strcasecmp(value, "chunked")) return -1;
        p->chunked = 1;
    }
    cs_value text = cs_str(p->vm, value);
    if (!text.as.p) return -1;
    return cs_map_set_owned(p->headers, line, text);
}
static int append_body(cs_http_parser *p, const char *data, size_t length) {
    if (length > SIZE_MAX - p->body_len - 1 ||
        !ensure_buf(&p->body, &p->body_cap, p->body_len + length + 1)) return 0;
    memcpy(p->body + p->body_len, data, length);
    p->body_len += length; p->body[p->body_len] = 0; return 1;
}
static int parse_line(cs_http_parser *p, char *line) {
    if (p->state == HTTP_PARSE_STATUS_LINE) {
        if (parse_status_line(p, line)) return -1;
        p->state = HTTP_PARSE_HEADERS; return 0;
    }
    if (p->state == HTTP_PARSE_HEADERS) {
        if (*line) return parse_header_line(p, line, 0);
        if (p->status_code < 200) {
            if (p->status_code == 101) return -1; /* Protocol upgrades need another transport API. */
            cs_value_release(p->headers); p->headers = cs_map(p->vm);
            if (!p->headers.as.p) return -1;
            p->has_content_length = 0; p->content_length = 0; p->chunked = 0;
            p->state = HTTP_PARSE_STATUS_LINE; return 0;
        }
        if (p->no_body || p->status_code == 204 || p->status_code == 304) {
            p->state = HTTP_PARSE_DONE; return 0;
        }
        if (p->chunked && p->has_content_length) return -1;
        if (p->has_content_length && !p->content_length) p->state = HTTP_PARSE_DONE;
        else { p->close_delimited = !p->chunked && !p->has_content_length; p->state = HTTP_PARSE_BODY; }
        return 0;
    }
    if (p->chunk_state == 4) {
        if (!*line) { p->state = HTTP_PARSE_DONE; return 0; }
        return parse_header_line(p, line, 1);
    }
    size_t length = 0, digits = 0;
    while (isxdigit((unsigned char)line[digits])) {
        unsigned char c = (unsigned char)line[digits++];
        unsigned value = c <= '9' ? (unsigned)(c-'0') : (unsigned)(tolower(c)-'a'+10);
        if (length > (SIZE_MAX-value)/16) return -1;
        length = length*16+value;
    }
    if (!digits) return -1;
    const char *rest = line + digits;
    while (*rest == ' ' || *rest == '\t') rest++;
    if ((*rest && *rest != ';') || !field_value(rest)) return -1;
    p->chunk_remaining = length;
    p->chunk_state = length ? 1 : 4;
    p->in_chunk = length != 0;
    return 0;
}
int cs_http_parser_feed(cs_http_parser *p, const char *data, size_t length) {
    if (!p || (!data && length)) return -1;
    if (p->state == HTTP_PARSE_ERROR) return -1;
    size_t index = 0;
    while (index < length && p->state != HTTP_PARSE_DONE) {
        if (p->state == HTTP_PARSE_STATUS_LINE || p->state == HTTP_PARSE_HEADERS ||
            (p->chunked && (p->chunk_state == 0 || p->chunk_state == 4))) {
            unsigned char c = (unsigned char)data[index++];
            if (!c) goto error;
            if (c == '\n') {
                int had_cr = p->line_len && p->line_buf[p->line_len-1] == '\r';
                if (p->state == HTTP_PARSE_BODY && !had_cr) goto error;
                if (had_cr) p->line_len--;
                p->line_buf[p->line_len] = 0;
                if (parse_line(p, p->line_buf)) goto error;
                p->line_len = 0;
            } else {
                if (p->line_len > SIZE_MAX-2 || !ensure_buf(&p->line_buf, &p->line_cap, p->line_len+2)) goto error;
                p->line_buf[p->line_len++] = (char)c;
            }
        } else if (!p->chunked) {
            size_t take = length-index;
            if (p->has_content_length && take > p->content_length-p->body_len) take = p->content_length-p->body_len;
            if (!append_body(p, data+index, take)) goto error;
            index += take;
            if (p->has_content_length && p->body_len == p->content_length) p->state = HTTP_PARSE_DONE;
        } else if (p->chunk_state == 1) {
            size_t take = p->chunk_remaining;
            if (take > length-index) take = length-index;
            if (!append_body(p, data+index, take)) goto error;
            index += take; p->chunk_remaining -= take;
            if (!p->chunk_remaining) {p->chunk_state = 2; p->in_chunk = 0;}
        } else if (p->chunk_state == 2) {
            if (data[index++] != '\r') goto error;
            p->chunk_state = 3;
        } else if (p->chunk_state == 3) {
            if (data[index++] != '\n') goto error;
            p->chunk_state = 0;
        } else goto error;
    }
    return 1;
error:
    p->state = HTTP_PARSE_ERROR; return -1;
}
int cs_http_parser_finish(cs_http_parser *p) {
    if (!p) return -1;
    if (p->state == HTTP_PARSE_DONE) return 0;
    if (p->state == HTTP_PARSE_BODY && p->close_delimited) {p->state = HTTP_PARSE_DONE; return 0;}
    p->state = HTTP_PARSE_ERROR; return -1;
}
int cs_http_parser_is_done(cs_http_parser *p) { return p && p->state == HTTP_PARSE_DONE; }

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
            for(char *at=scheme;*at;at++)*at=(char)tolower((unsigned char)*at);
            cs_map_set_owned(result, "scheme", cs_str(vm, scheme));
            free(scheme);
        }
        rest = scheme_end + 3;
    }

    const char* frag = strchr(rest, '#');
    if (frag) {
        cs_map_set_owned(result, "fragment", cs_str(vm, frag + 1));
    }

    const char* query = strchr(rest, '?');
    if (query && (!frag || query < frag)) {
        size_t qlen = frag ? (size_t)(frag - query - 1) : strlen(query + 1);
        char* q = dup_n(query + 1, qlen);
        if (q) {
            cs_map_set_owned(result, "query", cs_str(vm, q));
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
            cs_map_set_owned(result, "path", cs_str(vm, p));
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
            cs_map_set_owned(result, "user", cs_str(vm, auth));
            cs_map_set_owned(result, "pass", cs_str(vm, colon + 1));
        } else if (*auth) {
            cs_map_set_owned(result, "user", cs_str(vm, auth));
        }
    }

    char *port_text=NULL;
    if(*hostport=='[') {
        char *closing=strchr(hostport+1,']');
        if(!closing || (closing[1] && closing[1]!=':'))goto invalid_authority;
        *closing=0;hostport++;
        if(closing[1]==':')port_text=closing+2;
    } else {
        char *colon=strchr(hostport,':');
        if(colon) {*colon=0;port_text=colon+1;}
    }
    if(*hostport)cs_map_set_owned(result,"host",cs_str(vm,hostport));
    if(port_text) {
        if(!*port_text)goto invalid_authority;
        unsigned value=0;
        for(const unsigned char *at=(const unsigned char*)port_text;*at;at++) {
            if(*at<'0' || *at>'9' || value>(65535U-(*at-'0'))/10U)goto invalid_authority;
            value=value*10U+(*at-'0');
        }
        cs_map_set(result,"port",cs_int(value));
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
invalid_authority:
    free(auth);cs_value_release(result);cs_error(vm,"url_parse() invalid authority or port");return cs_nil();
}

static int request_append(char **buffer, size_t *capacity, size_t *length, const char *format, ...);
cs_value cs_url_build(cs_vm *vm, cs_value parts) {
    if (parts.type != CS_T_MAP) return cs_nil();
    const char *names[]={"scheme","user","pass","host","port","path","query","fragment"};
    cs_value values[8];
    for(size_t i=0;i<8;i++)values[i]=cs_map_get(parts,names[i]);
    char *buffer=NULL;size_t capacity=0,length=0;int good=1;
    for(size_t i=0;i<8;i++)if(values[i].type==CS_T_STR &&
        (!values[i].as.p || ((cs_string*)values[i].as.p)->len!=strlen(cs_to_cstr(values[i]))))good=0;
    if(!good)cs_error(vm,"url_build() parts contain invalid strings");
    if(good && values[0].type==CS_T_STR)good=request_append(&buffer,&capacity,&length,"%s://",cs_to_cstr(values[0]));
    if(good && values[1].type==CS_T_STR) {
        good=request_append(&buffer,&capacity,&length,"%s",cs_to_cstr(values[1]));
        if(good && values[2].type==CS_T_STR)good=request_append(&buffer,&capacity,&length,":%s",cs_to_cstr(values[2]));
        if(good)good=request_append(&buffer,&capacity,&length,"@");
    }
    if(good && values[3].type==CS_T_STR) {
        const char *host=cs_to_cstr(values[3]);
        good=request_append(&buffer,&capacity,&length,strchr(host,':') && *host!='[' ? "[%s]" : "%s",host);
    }
    if(good && values[4].type==CS_T_INT) {
        if(values[4].as.i<0 || values[4].as.i>65535) {cs_error(vm,"url_build() invalid port");good=0;}
        else good=request_append(&buffer,&capacity,&length,":%lld",(long long)values[4].as.i);
    }
    if(good && values[5].type==CS_T_STR)good=request_append(&buffer,&capacity,&length,"%s",cs_to_cstr(values[5]));
    if(good && values[6].type==CS_T_STR)good=request_append(&buffer,&capacity,&length,"?%s",cs_to_cstr(values[6]));
    if(good && values[7].type==CS_T_STR)good=request_append(&buffer,&capacity,&length,"#%s",cs_to_cstr(values[7]));
    for(size_t i=0;i<8;i++)cs_value_release(values[i]);
    if(!good) {free(buffer);if(!*cs_vm_last_error(vm))cs_error(vm,"url_build() out of memory");return cs_nil();}
    return buffer ? cs_str_take(vm,buffer,(uint64_t)length) : cs_str(vm,"");
}

static int http_send_all(cs_socket_t fd, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = cs_socket_send(fd, data+sent, length-sent);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}
#ifndef CS_NO_TLS
static int tls_send_all(SSL *ssl, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        size_t amount = length-sent;
        if (amount > INT_MAX) amount = INT_MAX;
        int n = cs_tls_write(ssl, data+sent, (int)amount);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}
#endif
static int request_append(char **buffer, size_t *capacity, size_t *length, const char *format, ...) {
    va_list args, copy;
    va_start(args, format); va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, format, copy); va_end(copy);
    if (needed < 0 || (size_t)needed > SIZE_MAX-*length-1 ||
        !ensure_buf(buffer, capacity, *length+(size_t)needed+1)) {va_end(args); return 0;}
    int written = vsnprintf(*buffer+*length, *capacity-*length, format, args);
    va_end(args);
    if (written != needed) return 0;
    *length += (size_t)written; return 1;
}
static int request_string(cs_value value) {
    return value.type == CS_T_STR && value.as.p &&
           ((cs_string*)value.as.p)->len == strlen(cs_to_cstr(value));
}
static int nf_http_request(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud;
    if (out) *out = cs_nil();
    if (argc < 1 || argv[0].type != CS_T_MAP) {cs_error(vm,"http_request() requires options map"); return 1;}
    cs_value url = cs_nil(), method = cs_nil(), parts = cs_nil(), scheme = cs_nil(), host = cs_nil();
    cs_value port = cs_nil(), path = cs_nil(), query = cs_nil(), headers = cs_nil(), body = cs_nil();
    cs_value keys = cs_nil(), value = cs_nil(), response = cs_nil();
    cs_socket_t fd = CS_INVALID_SOCKET;
#ifndef CS_NO_TLS
    SSL *ssl = NULL;
#endif
    cs_http_parser parser; int parser_ready = 0, result = 1;
    char *request = NULL; size_t capacity = 0, written = 0;
    url = cs_map_get(argv[0],"url");
    if (!request_string(url)) {cs_error(vm,"http_request() requires a URL without embedded NUL bytes"); goto cleanup;}
    method = cs_map_get(argv[0],"method");
    const char *method_text = method.type == CS_T_NIL ? "GET" : cs_to_cstr(method);
    if ((method.type != CS_T_NIL && !request_string(method)) || !http_token(method_text) || !strcmp(method_text,"CONNECT")) {
        cs_error(vm,"http_request() invalid or unsupported method"); goto cleanup;
    }
    parts = cs_url_parse(vm,cs_to_cstr(url));
    scheme = cs_map_get(parts,"scheme"); host = cs_map_get(parts,"host"); port = cs_map_get(parts,"port");
    path = cs_map_get(parts,"path"); query = cs_map_get(parts,"query");
    const char *scheme_text = scheme.type == CS_T_STR ? cs_to_cstr(scheme) : "http";
    const char *host_text = host.type == CS_T_STR ? cs_to_cstr(host) : "";
    const char *path_text = path.type == CS_T_STR ? cs_to_cstr(path) : "/";
    const char *query_text = query.type == CS_T_STR ? cs_to_cstr(query) : "";
    int use_tls = !strcmp(scheme_text,"https");
    if (strcmp(scheme_text,"http") && !use_tls) {cs_error(vm,"http_request() unsupported URL scheme"); goto cleanup;}
    if (!*host_text || strpbrk(host_text," \t\r\n") || !field_value(host_text) ||
        strpbrk(path_text," \t\r\n") || !field_value(path_text) ||
        strpbrk(query_text," \t\r\n") || !field_value(query_text)) {
        cs_error(vm,"http_request() invalid URL"); goto cleanup;
    }
    int port_number = use_tls ? 443 : 80;
    if (port.type == CS_T_INT) {
        if (port.as.i < 1 || port.as.i > 65535) {cs_error(vm,"http_request() invalid port"); goto cleanup;}
        port_number = (int)port.as.i;
    }
#ifdef CS_NO_TLS
    if (use_tls) {cs_error(vm,"http_request() TLS disabled"); goto cleanup;}
#endif
    headers = cs_map_get(argv[0],"headers"); body = cs_map_get(argv[0],"body");
    if (headers.type != CS_T_NIL && headers.type != CS_T_MAP) {cs_error(vm,"http_request() headers must be a map"); goto cleanup;}
    if (body.type == CS_T_STR && !body.as.p) goto memory;
    if (body.type != CS_T_NIL && body.type != CS_T_STR) {cs_error(vm,"http_request() body must be a string"); goto cleanup;}
    size_t body_length = body.type == CS_T_STR ? (size_t)((cs_string*)body.as.p)->len : 0;
    if (!request_append(&request,&capacity,&written,"%s %s%s%s HTTP/1.1\r\nConnection: close\r\n",method_text,path_text,
                        query.type == CS_T_STR ? "?" : "",query_text)) goto memory;
    if (body.type == CS_T_STR && !request_append(&request,&capacity,&written,"Content-Length: %zu\r\n",body_length)) goto memory;
    int has_host = 0;
    if (headers.type == CS_T_MAP) {
        keys = cs_map_keys(vm,headers);
        if (keys.type != CS_T_LIST || !keys.as.p) goto memory;
        cs_list_obj *list = (cs_list_obj*)keys.as.p;
        for (size_t i=0;i<list->len;i++) {
            cs_value key = list->items[i];
            if (!request_string(key) || !http_token(cs_to_cstr(key))) {cs_error(vm,"http_request() invalid header name"); goto cleanup;}
            value = cs_map_get(headers,cs_to_cstr(key));
            if (!request_string(value) || !field_value(cs_to_cstr(value))) {cs_error(vm,"http_request() invalid header value"); goto cleanup;}
            const char *name = cs_to_cstr(key);
            if (!strcasecmp(name,"content-length") || !strcasecmp(name,"transfer-encoding") || !strcasecmp(name,"connection")) {
                cs_error(vm,"http_request() manages framing headers"); goto cleanup;
            }
            if (!strcasecmp(name,"host") && has_host++) {cs_error(vm,"http_request() duplicate Host header"); goto cleanup;}
            if (!request_append(&request,&capacity,&written,"%s: %s\r\n",name,cs_to_cstr(value))) goto memory;
            cs_value_release(value); value = cs_nil();
        }
    }
    if (!has_host) {
        if (port_number == (use_tls ? 443 : 80)) {
            if (!request_append(&request,&capacity,&written,"Host: %s\r\n",host_text)) goto memory;
        } else if (!request_append(&request,&capacity,&written,"Host: %s:%d\r\n",host_text,port_number)) goto memory;
    }
    if (!request_append(&request,&capacity,&written,"\r\n")) goto memory;
    if (cs_event_init()) {cs_error(vm,"http_request() network initialization failed"); goto cleanup;}
    fd = socket(AF_INET,SOCK_STREAM,0);
    if (fd == CS_INVALID_SOCKET) {cs_error(vm,"http_request() failed to create socket"); goto cleanup;}
    struct sockaddr_in address;
    if (cs_resolve_host(host_text,port_number,&address)) {cs_error(vm,"http_request() failed to resolve host"); goto cleanup;}
    if (connect(fd,(struct sockaddr*)&address,sizeof(address))) {cs_error(vm,"http_request() connect failed"); goto cleanup;}
#ifndef CS_NO_TLS
    if (use_tls) {
        if (cs_tls_init() || !(ssl=cs_tls_new_ssl(fd,host_text))) {cs_error(vm,"http_request() TLS initialization failed"); goto cleanup;}
        if (cs_tls_connect_raw(ssl) != 1) {cs_error(vm,"http_request() TLS handshake failed"); goto cleanup;}
        if (cs_tls_verify_cert(ssl)) {cs_error(vm,"http_request() TLS verification failed"); goto cleanup;}
    }
    if (ssl ? tls_send_all(ssl,request,written) : http_send_all(fd,request,written)) {
#else
    if (http_send_all(fd,request,written)) {
#endif
        cs_error(vm,"http_request() send failed"); goto cleanup;
    }
    if (body_length) {
#ifndef CS_NO_TLS
        int error = ssl ? tls_send_all(ssl,cs_to_cstr(body),body_length) : http_send_all(fd,cs_to_cstr(body),body_length);
#else
        int error = http_send_all(fd,cs_to_cstr(body),body_length);
#endif
        if (error) {cs_error(vm,"http_request() body send failed"); goto cleanup;}
    }
    cs_http_parser_init(&parser,vm); parser_ready = 1;
    if (parser.state == HTTP_PARSE_ERROR) goto memory;
    parser.no_body = !strcmp(method_text,"HEAD");
    char buffer[4096];
    while (!cs_http_parser_is_done(&parser)) {
        int received;
#ifndef CS_NO_TLS
        if (ssl) {
            received = cs_tls_read(ssl,buffer,sizeof(buffer));
            if (received <= 0 && SSL_get_error(ssl,received) != SSL_ERROR_ZERO_RETURN) {
                cs_error(vm,"http_request() TLS read failed"); goto cleanup;
            }
        } else
#endif
        {
            received = (int)recv(fd,buffer,sizeof(buffer),0);
#ifndef _WIN32
            if (received < 0 && errno == EINTR) continue;
#else
            if (received < 0 && WSAGetLastError() == WSAEINTR) continue;
#endif
            if (received < 0) {cs_error(vm,"http_request() read failed"); goto cleanup;}
        }
        if (!received) {
            if (cs_http_parser_finish(&parser)) {cs_error(vm,"http_request() truncated response"); goto cleanup;}
            break;
        }
        if (cs_http_parser_feed(&parser,buffer,(size_t)received) < 0) {cs_error(vm,"http_request() invalid response"); goto cleanup;}
    }
    response = cs_map(vm);
    value = cs_str(vm,parser.status_text);
    if (!response.as.p || !value.as.p || cs_map_set(response,"status",cs_int(parser.status_code)) ||
        cs_map_set(response,"status_text",value) || cs_map_set(response,"headers",parser.headers)) goto memory;
    cs_value_release(value); value = cs_nil();
    char *body_data = parser.body; size_t body_size = parser.body_len;
    parser.body = NULL; parser.body_len = parser.body_cap = 0;
    cs_value body_result = cs_str_take(vm,body_data,(uint64_t)body_size);
    if (!body_result.as.p) goto memory;
    if (cs_map_set_owned(response,"body",body_result)) goto memory;
    if (out) {*out=response; response=cs_nil();}
    result = 0; goto cleanup;
memory:
    cs_error(vm,"http_request() out of memory or oversized request");
cleanup:
#ifndef CS_NO_TLS
    if (ssl) cs_tls_close(ssl);
#endif
    if (fd != CS_INVALID_SOCKET) cs_socket_close(fd);
    if (parser_ready) cs_http_parser_free(&parser);
    free(request);
    cs_value_release(response); cs_value_release(value); cs_value_release(keys);
    cs_value_release(body); cs_value_release(headers); cs_value_release(query); cs_value_release(path);
    cs_value_release(port); cs_value_release(host); cs_value_release(scheme); cs_value_release(parts);
    cs_value_release(method); cs_value_release(url);
    return result;
}

static int nf_http_get(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 1 || argv[0].type != CS_T_STR) {
        cs_error(vm, "http_get() requires url string");
        return 1;
    }
    cs_value opts = cs_map(vm);
    cs_map_set(opts, "url", argv[0]);
    cs_map_set_owned(opts, "method", cs_str(vm, "GET"));
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
    cs_map_set_owned(opts, "method", cs_str(vm, "POST"));
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
    cs_map_set_owned(opts, "method", cs_str(vm, "DELETE"));
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
    cs_value value=cs_url_parse(vm,cs_to_cstr(argv[0]));
    if(out)*out=value;else cs_value_release(value);
    return value.type==CS_T_MAP ? 0 : 1;
}

static int nf_url_build(cs_vm* vm, void* ud, int argc, const cs_value* argv, cs_value* out) {
    (void)ud;
    if (argc < 1 || argv[0].type != CS_T_MAP) {
        if (out) *out = cs_str(vm, "");
        return 0;
    }
    cs_value value=cs_url_build(vm,argv[0]);
    if(out)*out=value;else cs_value_release(value);
    return value.type==CS_T_STR && value.as.p ? 0 : 1;
}

void cs_register_http_stdlib(cs_vm* vm) {
    cs_register_native(vm, "url_parse", nf_url_parse, NULL);
    cs_register_native(vm, "url_build", nf_url_build, NULL);
    cs_register_native(vm, "http_get", nf_http_get, NULL);
    cs_register_native(vm, "http_post", nf_http_post, NULL);
    cs_register_native(vm, "http_delete", nf_http_delete, NULL);
    cs_register_native(vm, "http_request", nf_http_request, NULL);
}
