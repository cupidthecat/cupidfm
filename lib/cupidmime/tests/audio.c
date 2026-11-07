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

static void le32(unsigned char *p, uint32_t v) {
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[i] = (unsigned char)(v >> (i * 8));
}

/* Table-based reference CRC, separate from the library's bit-at-a-time walk. */
static void checksum(unsigned char *p, size_t n) {
    uint32_t table[256], crc = 0;
    unsigned i, b;
    size_t j;
    memset(p + 22, 0, 4);
    for (i = 0; i < 256; ++i) {
        uint32_t v = (uint32_t)i << 24;
        for (b = 0; b < 8; ++b)
            v = v & 0x80000000u ? (v << 1) ^ 0x04c11db7u : v << 1;
        table[i] = v;
    }
    for (j = 0; j < n; ++j)
        crc = (crc << 8) ^ table[(crc >> 24) ^ p[j]];
    le32(p + 22, crc);
}

static size_t page(unsigned char *p, unsigned flags, uint32_t serial, uint32_t seq,
                   const unsigned char *packet, size_t len) {
    size_t segments = len / 255 + 1, i, size = 27 + segments + len;
    CHECK(segments <= 255);
    memset(p, 0, size);
    memcpy(p, "OggS", 4);
    p[5] = (unsigned char)flags;
    le32(p + 14, serial);
    le32(p + 18, seq);
    p[26] = (unsigned char)segments;
    for (i = 0; i < segments; ++i)
        p[27 + i] = (unsigned char)(i == segments - 1 ? len % 255 : 255);
    memcpy(p + 27 + segments, packet, len);
    checksum(p, size);
    return size;
}

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

static void prefixes(const unsigned char *p, size_t n, int ogg) {
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char *b = malloc(i ? i : 1);
        cm_result r;
        CHECK(b != NULL);
        memcpy(b, p, i);
        r = detect(b, i);
        if (ogg)
            CHECK(strcmp(r.mime, "audio/ogg"));
        CHECK(cm_detect_prefix(b, i, &r) == CM_OK);
        CHECK(r.flags & CM_PARTIAL);
        if (ogg)
            CHECK(strcmp(r.mime, "audio/ogg"));
        free(b);
    }
}

static void flacs(unsigned char *seed) {
    unsigned char p[80] = {0};
    size_t i;
    cm_result r;
    memcpy(p, "fLaC", 4);
    p[4] = 128;
    p[7] = 34;
    p[9] = 16;
    p[11] = 16;
    /* 48kHz, mono, 16 bits/sample. */
    p[18] = 0x0b;
    p[19] = 0xb8;
    p[21] = 0xf0;
    expect(p, 42, "audio/flac", 0);
    memcpy(seed, p, 42);
    prefixes(p, 42, 0);
    p[9] = 15;
    expect(p, 42, "application/octet-stream", CM_MALFORMED);
    p[9] = 16;
    p[11] = 15;
    expect(p, 42, "application/octet-stream", CM_MALFORMED);
    p[11] = 16;
    p[21] = 0;
    expect(p, 42, "application/octet-stream", CM_MALFORMED);
    p[21] = 0xf0;
    p[7] = 33;
    expect(p, 42, "application/octet-stream", CM_MALFORMED);
    p[7] = 34;
    p[4] = 127;
    expect(p, 42, "application/octet-stream", CM_MALFORMED);
    p[4] = 0;
    expect(p, 42, "audio/flac", CM_TRUNCATED);
    p[42] = 129;
    p[45] = 1;
    expect(p, 47, "audio/flac", 0);
    expect(p, 46, "audio/flac", CM_TRUNCATED);
    p[42] = 128;
    expect(p, 47, "audio/flac", CM_MALFORMED);
    p[42] = 255;
    expect(p, 47, "audio/flac", CM_MALFORMED);
    memcpy(p, seed, 42);
    p[14] = 2;
    p[17] = 1;
    expect(p, 42, "application/octet-stream", CM_MALFORMED);
    for (i = 0; i < 42; ++i) {
        memcpy(p, seed, 42);
        p[i] ^= 0x80;
        r = detect(p, 42);
        CHECK(r.mime && r.reason);
    }
}

static void oggs(unsigned char *seed, size_t *seedlen) {
    unsigned char p[2048], id[30] = {0}, payload[300] = {0};
    size_t first, total, second;
    cm_result r;
    memcpy(id, "OpusHead", 8);
    id[8] = 1;
    id[9] = 2;
    first = page(p, 2, 5, 0, id, 19);
    second = page(p + first, 4, 5, 1, payload, 2);
    total = first + second;
    expect(p, total, "audio/ogg", 0);
    memcpy(seed, p, total);
    *seedlen = total;
    prefixes(p, total, 1);
    CHECK(cm_detect_prefix(p, total, &r) == CM_OK);
    CHECK(!strcmp(r.mime, "application/ogg"));
    CHECK(r.flags == (CM_PARTIAL | CM_AMBIGUOUS));
    expect(p, first, "application/ogg", CM_AMBIGUOUS | CM_TRUNCATED);
    p[28 + 9] = 0;
    checksum(p, first);
    expect(p, total, "application/ogg", CM_AMBIGUOUS | CM_MALFORMED);
    p[28 + 9] = 2;
    p[28 + 8] = 2;
    checksum(p, first);
    expect(p, total, "application/ogg", CM_AMBIGUOUS);
    memcpy(p, seed, total);
    p[28 + 18] = 1;
    checksum(p, first);
    expect(p, total, "application/ogg", CM_AMBIGUOUS);
    memcpy(p, seed, total);
    p[28] ^= 1;
    expect(p, total, "application/ogg", CM_AMBIGUOUS | CM_MALFORMED);
    memcpy(p, seed, total);
    p[5] = 0x82;
    expect(p, total, "application/ogg", CM_AMBIGUOUS | CM_MALFORMED);
    memcpy(p, seed, total);
    le32(p + first + 18, 2);
    checksum(p + first, second);
    expect(p, total, "application/ogg", CM_AMBIGUOUS | CM_MALFORMED);
    memcpy(p, seed, total);
    p[first + 5] |= 1;
    checksum(p + first, second);
    expect(p, total, "application/ogg", CM_AMBIGUOUS | CM_MALFORMED);
    memcpy(p, seed, total);
    le32(p + first + 14, 6);
    checksum(p + first, second);
    expect(p, total, "application/ogg", CM_AMBIGUOUS); /* multiplexed */
    memcpy(p, seed, total);
    p[first + 5] = 6;
    checksum(p + first, second);
    expect(p, total, "application/ogg", CM_AMBIGUOUS); /* chained/BOS */
    memcpy(p, seed, total);
    p[4] = 1;
    expect(p, total, "application/ogg", CM_AMBIGUOUS | CM_MALFORMED);
    memcpy(p, seed, total);
    p[26] = 255;
    expect(p, total, "application/ogg", CM_AMBIGUOUS | CM_TRUNCATED);
    memcpy(p, seed, total);
    p[27] = 254;
    expect(p, total, "application/ogg", CM_AMBIGUOUS | CM_TRUNCATED);
    memcpy(p, seed, total);
    p[5] = 3;
    checksum(p, first);
    expect(p, total, "application/ogg", CM_AMBIGUOUS | CM_MALFORMED);
    memset(id, 0, sizeof(id));
    memcpy(id, "\001vorbis", 7);
    id[11] = 1;
    le32(id + 12, 48000);
    id[28] = 0xb8;
    id[29] = 1;
    first = page(p, 2, 1, 0, id, 30);
    total = first + page(p + first, 4, 1, 1, payload, 1);
    expect(p, total, "audio/ogg", 0);
    prefixes(p, total, 1);
    id[28] = 0x68;
    first = page(p, 2, 1, 0, id, 30);
    total = first + page(p + first, 4, 1, 1, payload, 1);
    expect(p, total, "application/ogg", CM_AMBIGUOUS | CM_MALFORMED);
    first = page(p, 2, 1, 0, payload, 4);
    total = first + page(p + first, 4, 1, 1, payload, 1);
    expect(p, total, "application/ogg", CM_AMBIGUOUS);
    /* A packet spanning pages requires a continuation flag and cannot end early. */
    first = page(p, 2, 1, 0, payload, 255);
    p[26] = 1;
    memmove(p + 28, p + 29, 255);
    --first;
    checksum(p, first);
    total = first + page(p + first, 5, 1, 1, payload, 1);
    expect(p, total, "application/ogg", CM_AMBIGUOUS);
    p[5] = 6;
    checksum(p, first);
    expect(p, first, "application/ogg", CM_AMBIGUOUS | CM_MALFORMED);
    for (first = 0; first < *seedlen; ++first) {
        memcpy(p, seed, *seedlen);
        p[first] ^= 0x80;
        r = detect(p, *seedlen);
        CHECK(r.mime && r.reason);
    }
}

int main(int argc, char **argv) {
    unsigned char flac[42], ogg[256];
    size_t n;
    flacs(flac);
    oggs(ogg, &n);
    if (argc == 3) {
        FILE *f = fopen(argv[1], "wb");
        CHECK(f != NULL);
        CHECK(fwrite(flac, 1, 42, f) == 42);
        CHECK(!fclose(f));
        f = fopen(argv[2], "wb");
        CHECK(f != NULL);
        CHECK(fwrite(ogg, 1, n, f) == n);
        CHECK(!fclose(f));
    }
    printf("%u audio checks passed\n", checks);
    return 0;
}
