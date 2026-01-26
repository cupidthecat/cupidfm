#ifndef CS_HTTP_H
#define CS_HTTP_H

#include "cupidscript.h"

// HTTP response parser state
typedef enum {
    HTTP_PARSE_STATUS_LINE,
    HTTP_PARSE_HEADERS,
    HTTP_PARSE_BODY,
    HTTP_PARSE_DONE,
    HTTP_PARSE_ERROR
} cs_http_parse_state;

typedef struct {
    cs_http_parse_state state;
    cs_vm* vm;
    int status_code;
    char status_text[64];
    cs_value headers;
    char* body;
    size_t body_len;
    size_t body_cap;
    size_t content_length;
    int chunked;
    size_t chunk_remaining;
    int in_chunk;
    char* line_buf;
    size_t line_len;
    size_t line_cap;
} cs_http_parser;

// Parser functions
void cs_http_parser_init(cs_http_parser* p, cs_vm* vm);
void cs_http_parser_free(cs_http_parser* p);
int cs_http_parser_feed(cs_http_parser* p, const char* data, size_t len);
int cs_http_parser_is_done(cs_http_parser* p);

// URL parsing
cs_value cs_url_parse(cs_vm* vm, const char* url);
cs_value cs_url_build(cs_vm* vm, cs_value parts);

// Register HTTP stdlib functions
void cs_register_http_stdlib(cs_vm* vm);

#endif
