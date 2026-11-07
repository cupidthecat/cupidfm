#include "cupidmime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static cm_result detect(const void *p, size_t n) {
    cm_result r;
    CHECK(cm_detect(p, n, &r) == CM_OK);
    return r;
}

static void type(const void *p, size_t n, const char *mime) {
    size_t i;
    cm_result r = detect(p, n);
    CHECK(!strcmp(r.mime, mime));
    CHECK(r.examined == n);
    for (i = 1; i < n; ++i) {
        unsigned char *b = malloc(i);
        CHECK(b != NULL);
        memcpy(b, p, i);
        (void)detect(b, i);
        CHECK(cm_detect_prefix(b, i, &r) == CM_OK);
        CHECK(r.flags & CM_PARTIAL);
        free(b);
    }
}

static void text(const char *s, const char *mime) {
    type(s, strlen(s), mime);
}

static void be(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void le(unsigned char *p, uint32_t v) {
    p[3] = (unsigned char)(v >> 24);
    p[2] = (unsigned char)(v >> 16);
    p[1] = (unsigned char)(v >> 8);
    p[0] = (unsigned char)v;
}

static void binary_formats(void) {
    unsigned char p[600] = {0};
    cm_result r;
    size_t i;
    unsigned sum;
    memcpy(p, "\x89PNG\r\n\x1a\n", 8);
    be(p + 8, 13);
    memcpy(p + 12, "IHDR", 4);
    be(p + 16, 1);
    be(p + 20, 1);
    p[24] = 8;
    p[25] = 6;
    type(p, 33, "image/png");
    p[24] = 3;
    r = detect(p, 33);
    CHECK(r.flags & CM_MALFORMED);
    for (i = 0; i < 33; ++i) {
        r = detect(p, i);
        CHECK(strcmp(r.mime, "image/png"));
    }
    memset(p, 0, sizeof(p));
    memcpy(p, "GIF89a", 6);
    p[6] = 1;
    p[8] = 1;
    type(p, 13, "image/gif");
    p[8] = 0;
    CHECK(detect(p, 13).flags & CM_MALFORMED);
    type("\xff\xd8\xff\xe0", 4, "image/jpeg");
    memset(p, 0, sizeof(p));
    memcpy(p, "RIFF", 4);
    le(p + 4, 12);
    memcpy(p + 8, "WAVE", 4);
    type(p, 20, "audio/wav");
    CHECK(detect(p, 12).flags & CM_TRUNCATED);
    le(p + 4, 4);
    memcpy(p + 8, "WEBP", 4);
    CHECK(detect(p, 20).flags & CM_MALFORMED);
    le(p + 4, 12);
    memcpy(p + 12, "VP8L", 4);
    type(p, 20, "image/webp");
    le(p + 16, 0xffffffffu);
    CHECK(detect(p, 20).flags & CM_MALFORMED);
    memset(p, 0, sizeof(p));
    memcpy(p, "PK\003\004", 4);
    p[26] = 1;
    p[30] = 'x';
    type(p, 31, "application/zip");
    CHECK(detect(p, 31).flags & CM_AMBIGUOUS);
    le(p + 18, 0xffffffffu);
    CHECK(detect(p, 31).flags & CM_TRUNCATED);
    memset(p, 0, sizeof(p));
    memcpy(p, "PK\005\006", 4);
    type(p, 22, "application/zip");
    p[10] = 1;
    CHECK(detect(p, 22).flags & CM_MALFORMED);
    type("\x1f\x8b\x08\0\0\0\0\0\0\0", 10, "application/gzip");
    type("\0asm\1\0\0\0", 8, "application/wasm");
    CHECK(detect("\0asm\2\0\0\0", 8).flags & CM_MALFORMED);
    text("%PDF-1.7\n", "application/pdf");
    CHECK(detect("%PDF-1.x\n", 9).flags & CM_MALFORMED);
    memset(p, 0, sizeof(p));
    memcpy(p, "\177ELF", 4);
    p[4] = 2;
    p[5] = 1;
    p[6] = 1;
    type(p, 64, "application/x-elf");
    CHECK(detect(p, 63).flags & CM_TRUNCATED);
    memset(p, 0, sizeof(p));
    memcpy(p, "SQLite format 3\0", 16);
    p[16] = 16;
    p[18] = 1;
    p[19] = 1;
    p[21] = 64;
    p[22] = 32;
    p[23] = 32;
    type(p, 100, "application/vnd.sqlite3");
    p[17] = 1;
    CHECK(detect(p, 100).flags & CM_MALFORMED);
    memset(p, 0, sizeof(p));
    be(p, 24);
    memcpy(p + 4, "ftypisom", 8);
    memcpy(p + 16, "avif", 4);
    type(p, 24, "image/avif");
    CHECK(detect(p, 23).flags & CM_TRUNCATED);
    be(p, 0xffffffffu);
    CHECK(detect(p, 24).flags & CM_MALFORMED);
    memset(p, 0, sizeof(p));
    memcpy(p + 257, "ustar\0", 6);
    memset(p + 148, ' ', 8);
    sum = 0;
    for (i = 0; i < 512; ++i)
        sum += p[i];
    (void)snprintf((char *)p + 148, 8, "%06o", sum);
    p[155] = ' ';
    type(p, 512, "application/x-tar");
    p[0] = 1;
    CHECK(detect(p, 512).flags & CM_MALFORMED);
}

static void texts(void) {
    static const char *good[] = {"{}", "[]", " {\"a\":[true,false,null,-0.5e+2]} \r\n",
                                 "{\"x\":\"\\uD834\\uDD1E\"}"};
    static const char *bad[] = {"{",
                                "[1,]",
                                "{\"x\":01}",
                                "[1.]",
                                "[1e]",
                                "{} garbage",
                                "{\"x\":\"\\ud800\"}",
                                "[NaN]",
                                "{\"a\":true,}",
                                "[+1]",
                                "[.1]",
                                "[\"\\x20\"]"};
    size_t i;
    cm_result r;
    for (i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
        text(good[i], "application/json");
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        text(bad[i], "text/plain");
    text("123", "text/plain");
    text("hello\n", "text/plain");
    text("caf\xc3\xa9", "text/plain");
    text("<HTML>", "text/html");
    text("<htmlish>", "text/plain");
    text("<!doctype html>", "text/html");
    text("<?xml version=\"1.0\"?>", "application/xml");
    text("<?xmlish>", "text/plain");
    type("\xc0\xaf", 2, "application/octet-stream");
    type("\xed\xa0\x80", 3, "application/octet-stream");
    type("\xf4\x90\x80\x80", 4, "application/octet-stream");
    type("\xe2\x82", 2, "application/octet-stream");
    CHECK(cm_detect_prefix("\xe2\x82", 2, &r) == CM_OK);
    CHECK(!strcmp(r.mime, "text/plain"));
    CHECK(cm_detect_prefix("\xed\xa0", 2, &r) == CM_OK);
    CHECK(!strcmp(r.mime, "application/octet-stream"));
    CHECK(cm_detect_prefix("{}", 2, &r) == CM_OK);
    CHECK(!strcmp(r.mime, "text/plain"));
    CHECK(r.flags & CM_PARTIAL);
    {
        char deep[200];
        memset(deep, '[', 100);
        memset(deep + 100, ']', 100);
        type(deep, 200, "text/plain");
    }
}

static void containers(void) {
    unsigned char p[100] = {0};
    cm_result r;
    size_t i;
    memcpy(p, "\x1f\x8b\x08", 3);
    p[3] = 4;
    p[10] = 3;
    CHECK(detect(p, 11).flags & CM_TRUNCATED);
    CHECK(detect(p, 14).flags & CM_TRUNCATED);
    type(p, 25, "application/gzip");
    CHECK(detect(p, 25).flags == 0);
    p[3] = 8;
    memset(p + 10, 'x', 90);
    r = detect(p, 100);
    CHECK(r.flags & CM_TRUNCATED);
    CHECK(r.evidence == CM_FALLBACK);
    p[15] = 0;
    type(p, 26, "application/gzip");
    p[3] = 16;
    type(p, 26, "application/gzip");
    p[3] = 0xe0;
    CHECK(detect(p, 26).flags & CM_MALFORMED);
    memset(p, 0, sizeof(p));
    memcpy(p, "\x1f\x8b\x08\x02", 4);
    p[9] = 3;
    /* Header CRC fixture is generated independently from the implementation. */
    p[10] = 0;
    p[11] = 0;
    CHECK(detect(p, 22).flags & CM_MALFORMED);
    p[10] = 0xa7;
    p[11] = 0x77;
    type(p, 22, "application/gzip");
    CHECK(detect(p, 22).flags == 0);
    p[10] ^= 1;
    CHECK(detect(p, 22).flags & CM_MALFORMED);
    memset(p, 0, sizeof(p));
    memcpy(p, "RIFF", 4);
    memcpy(p + 8, "WAVE", 4);
    le(p + 4, 14);
    memcpy(p + 12, "JUNK", 4);
    le(p + 16, 1);
    p[20] = 42;
    type(p, 22, "audio/wav");
    CHECK(detect(p, 22).flags == 0);
    CHECK(detect(p, 21).flags & CM_TRUNCATED);
    le(p + 4, 13);
    CHECK(detect(p, 22).flags & CM_MALFORMED); /* padding outside outer extent */
    le(p + 4, 14);
    le(p + 16, 0xffffffffu);
    CHECK(detect(p, 22).flags & CM_MALFORMED);
    le(p + 4, 5);
    CHECK(detect(p, 22).flags & CM_MALFORMED);
    le(p + 4, 14);
    le(p + 16, 1);
    /* Each prefix is a separate allocation, not an oversized backing array. */
    for (i = 1; i <= 22; ++i) {
        unsigned char *b = malloc(i);
        CHECK(b != NULL);
        memcpy(b, p, i);
        (void)detect(b, i);
        free(b);
    }
}

static void boundaries(void) {
    uint32_t state = 0x12345678u;
    unsigned k;
    size_t i, n;
    unsigned char p[1024];
    cm_result r, before;
    r = detect(NULL, 0);
    CHECK(!strcmp(r.mime, "application/octet-stream"));
    before = r;
    CHECK(cm_detect(NULL, 1, &r) == CM_INVALID_ARGUMENT);
    CHECK(!memcmp(&r, &before, sizeof(r)));
    CHECK(cm_detect("x", 1, NULL) == CM_INVALID_ARGUMENT);
    for (k = 0; k < 2000; ++k) {
        n = k % sizeof(p);
        for (i = 0; i < n; ++i) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            p[i] = (unsigned char)state;
        }
        r = detect(p, n);
        CHECK(r.mime && r.reason);
        CHECK(r.examined == n);
    }
    /* Exact-sized allocations make sanitizer prefix tests catch overreads. */
    for (n = 1; n <= 128; ++n) {
        unsigned char *b = malloc(n);
        CHECK(b != NULL);
        memset(b, 0, n);
        memcpy(b, "SQLite format 3\0", n < 16 ? n : 16);
        (void)detect(b, n);
        free(b);
    }
    {
        unsigned char *b = malloc(CM_SCAN_LIMIT + 1);
        CHECK(b != NULL);
        memset(b, 'x', CM_SCAN_LIMIT + 1);
        r = detect(b, CM_SCAN_LIMIT + 1);
        CHECK(r.flags & CM_PARTIAL);
        CHECK(r.examined == CM_SCAN_LIMIT);
        free(b);
    }
}

int main(void) {
    binary_formats();
    texts();
    containers();
    boundaries();
    printf("%u checks passed\n", checks);
    return 0;
}
