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

static void expect(const void *p, size_t n, const char *mime, unsigned flags) {
    cm_result r = detect(p, n);
    CHECK(!strcmp(r.mime, mime));
    CHECK(r.flags == flags);
}

static void ss(unsigned char *p, uint32_t n) {
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[3 - i] = (unsigned char)((n >> (i * 7)) & 127u);
}

/* Explicit known frame lengths keep fixture construction independent of the parser's tables. */
static size_t frames(unsigned char *p, unsigned char version, const unsigned char *rates,
                     unsigned char mode, const size_t *lengths) {
    unsigned i;
    size_t off = 0;
    for (i = 0; i < 3; ++i) {
        memset(p + off, 0, lengths[i]);
        p[off] = 255;
        p[off + 1] = version;
        p[off + 2] = rates[i];
        p[off + 3] = mode;
        off += lengths[i];
    }
    return off;
}

static void prefixes(const unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char *b = malloc(i ? i : 1);
        cm_result r;
        CHECK(b != NULL);
        memcpy(b, p, i);
        r = detect(b, i);
        CHECK(strcmp(r.mime, "audio/mpeg"));
        CHECK(cm_detect_prefix(b, i, &r) == CM_OK);
        CHECK(r.flags & CM_PARTIAL);
        CHECK(strcmp(r.mime, "audio/mpeg"));
        free(b);
    }
}

static size_t raw(unsigned char *seed) {
    static const size_t constant[3] = {417, 417, 417}, vbr[3] = {417, 523, 365},
                        lower[3] = {208, 208, 208};
    static const unsigned char cbr[3] = {0x90, 0x90, 0x90}, variable[3] = {0x90, 0xa2, 0x80},
                               low[3] = {0x80, 0x80, 0x80};
    unsigned char p[2048];
    size_t n, i;
    cm_result r;
    {
        static const size_t freq_lengths[6] = {384, 576, 192, 288, 384, 576};
        unsigned k;
        for (k = 0; k < 6; ++k) {
            size_t lengths[3] = {freq_lengths[k], freq_lengths[k], freq_lengths[k]};
            unsigned char v = k < 2 ? 0xfb : k < 4 ? 0xf3 : 0xe3;
            unsigned char index = (unsigned char)((k < 2 ? 0x90u : 0x80u) | (k % 2 ? 8u : 4u));
            unsigned char indices[3] = {index, index, index};
            n = frames(p, v, indices, 0xc0, lengths);
            expect(p, n, "audio/mpeg", 0);
        }
    }
    n = frames(p, 0xfb, cbr, 0, constant);
    expect(p, n, "audio/mpeg", 0);
    prefixes(p, n);
    expect(p, 4, "application/octet-stream", CM_TRUNCATED | CM_AMBIGUOUS);
    expect(p, 417, "application/octet-stream", CM_TRUNCATED | CM_AMBIGUOUS);
    CHECK(cm_detect_prefix(p, n, &r) == CM_OK);
    CHECK(!strcmp(r.mime, "audio/mpeg"));
    CHECK(r.flags == CM_PARTIAL);
    p[100] = 255;
    p[101] = 0xfb;
    expect(p, n, "audio/mpeg", 0); /* interior sync does not shift boundaries */
    n = frames(p, 0xfa, cbr, 0, constant);
    expect(p, n, "audio/mpeg", 0); /* CRC-bearing frames */
    n = frames(p, 0xfb, variable, 0, vbr);
    expect(p, n, "audio/mpeg", 0);
    memcpy(seed, p, n);
    n = frames(p, 0xf3, low, 0xc0, lower);
    expect(p, n, "audio/mpeg", 0);
    prefixes(p, n);
    n = frames(p, 0xe3, low, 0xc0, constant);
    expect(p, n, "audio/mpeg", 0);
    prefixes(p, n);
    n = frames(p, 0xfb, cbr, 0, constant);
    p[1] = 0xeb;
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    p[1] = 0xfb;
    p[2] = 0xf0;
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    p[2] = 0x9c;
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    p[2] = 0;
    expect(p, n, "application/octet-stream", CM_AMBIGUOUS);
    p[2] = 0x90;
    p[3] = 2;
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    p[3] = 0;
    p[417 + 3] = 0xc0;
    expect(p, n, "application/octet-stream", CM_AMBIGUOUS);
    p[417 + 3] = 0;
    p[417 + 2] = 0x94;
    expect(p, n, "application/octet-stream", CM_AMBIGUOUS);
    p[417 + 2] = 0x90;
    p[417 + 1] = 0xf3;
    expect(p, n, "application/octet-stream", CM_AMBIGUOUS);
    p[417 + 1] = 0xfb;
    p[417] = 0;
    p[418] = 255;
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    n = frames(p, 0xfb, cbr, 0, constant);
    p[1] = 0xfd;
    CHECK(strcmp(detect(p, n).mime, "audio/mpeg")); /* other MPEG layers are not Layer III */
    for (i = 0; i < n; ++i) {
        (void)frames(p, 0xfb, cbr, 0, constant);
        p[i] ^= 0x80;
        r = detect(p, n);
        CHECK(r.mime && r.reason);
    }
    return 1305;
}

static void tags(const unsigned char *raw, size_t n) {
    unsigned char p[4096] = {0};
    size_t i, offset;
    cm_result r;
    for (i = 2; i <= 4; ++i) {
        memset(p, 0, sizeof(p));
        memcpy(p, "ID3", 3);
        p[3] = (unsigned char)i;
        ss(p + 6, 8);
        memcpy(p + 18, raw, n);
        expect(p, n + 18, "audio/mpeg", 0);
        prefixes(p, n + 18);
    }
    memset(p, 0, sizeof(p));
    memcpy(p, "ID3", 3);
    p[3] = 4;
    p[5] = 16;
    ss(p + 6, 8);
    memcpy(p + 18, "3DI", 3);
    memcpy(p + 21, p + 3, 7);
    memcpy(p + 28, raw, n);
    expect(p, n + 28, "audio/mpeg", 0);
    prefixes(p, n + 28);
    p[21] = 3;
    expect(p, n + 28, "application/octet-stream", CM_MALFORMED);
    p[21] = 4;
    p[18] = 'x';
    expect(p, n + 28, "application/octet-stream", CM_MALFORMED);
    p[18] = '3';
    expect(p, 27, "application/octet-stream", CM_TRUNCATED);
    p[6] = 128;
    expect(p, n + 28, "application/octet-stream", CM_MALFORMED);
    p[6] = 0;
    p[5] = 1;
    expect(p, n + 28, "application/octet-stream", CM_MALFORMED);
    p[5] = 16;
    p[3] = 255;
    expect(p, n + 28, "application/octet-stream", CM_MALFORMED);
    p[3] = 5;
    expect(p, n + 28, "application/octet-stream", CM_AMBIGUOUS);
    memset(p, 0, sizeof(p));
    memcpy(p, "ID3", 3);
    p[3] = 3;
    p[5] = 64;
    ss(p + 6, 10);
    p[13] = 6;
    memcpy(p + 20, raw, n);
    expect(p, n + 20, "audio/mpeg", 0);
    p[13] = 7;
    expect(p, n + 20, "application/octet-stream", CM_MALFORMED);
    p[13] = 6;
    ss(p + 6, 9);
    expect(p, n + 20, "application/octet-stream", CM_MALFORMED);
    ss(p + 6, 10);
    p[3] = 4;
    ss(p + 10, 6);
    expect(p, n + 20, "audio/mpeg", 0);
    ss(p + 10, 11);
    expect(p, n + 20, "application/octet-stream", CM_MALFORMED);
    memset(p, 0, sizeof(p));
    memcpy(p, "ID3", 3);
    p[3] = 3;
    expect(p, 10, "application/octet-stream", CM_AMBIGUOUS);
    memcpy(p + 10, raw, n);
    ss(p + 6, 0x0fffffffu);
    expect(p, n + 10, "application/octet-stream", CM_TRUNCATED);
    /* Tag payload sync bytes cannot substitute for frames at the outer boundary. */
    ss(p + 6, (uint32_t)n);
    expect(p, n + 10, "application/octet-stream", CM_AMBIGUOUS);
    ss(p + 6, 0);
    memcpy(p + 10, "fLaC", 4);
    expect(p, 14, "application/octet-stream", CM_AMBIGUOUS);
    /* Several empty tags still make forward progress without allocation. */
    memset(p, 0, sizeof(p));
    for (offset = 0; offset < 1000; offset += 10) {
        memcpy(p + offset, "ID3", 3);
        p[offset + 3] = 4;
    }
    memcpy(p + 1000, raw, n);
    expect(p, n + 1000, "audio/mpeg", 0);
    CHECK(cm_detect_prefix(p, n + 1000, &r) == CM_OK);
    CHECK(r.flags == CM_PARTIAL);
}

static void limits(void) {
    unsigned char *p = malloc(CM_SCAN_LIMIT + 1);
    cm_result r;
    CHECK(p != NULL);
    memset(p, 0, CM_SCAN_LIMIT + 1);
    memcpy(p, "ID3", 3);
    p[3] = 4;
    ss(p + 6, (uint32_t)CM_SCAN_LIMIT);
    r = detect(p, CM_SCAN_LIMIT + 1);
    CHECK(r.flags == (CM_PARTIAL | CM_TRUNCATED));
    CHECK(strcmp(r.mime, "audio/mpeg"));
    free(p);
}

int main(int argc, char **argv) {
    unsigned char seed[2048];
    size_t n = raw(seed);
    tags(seed, n);
    limits();
    if (argc == 2) {
        FILE *f = fopen(argv[1], "wb");
        CHECK(f != NULL);
        CHECK(fwrite(seed, 1, n, f) == n);
        CHECK(!fclose(f));
    }
    printf("%u MPEG checks passed\n", checks);
    return 0;
}
