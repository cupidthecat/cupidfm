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

static unsigned u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void b16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static void b32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static size_t pad(size_t n) {
    return (n + 3) & ~(size_t)3;
}

/* Synthetic metadata fixtures, not complete renderable fonts. */
static size_t sfnt(unsigned char *p, int cff) {
    static const uint32_t tags[] = {0x43464620, 0x4f532f32, 0x636d6170, 0x676c7966,
                                    0x68656164, 0x68686561, 0x686d7478, 0x6c6f6361,
                                    0x6d617870, 0x6e616d65, 0x706f7374};
    static const unsigned lengths[] = {4, 78, 4, 10, 54, 36, 4, 4, 32, 6, 32};
    unsigned i, num = cff ? 9u : 10u, record = 0;
    size_t off = 12 + 16u * num;
    memset(p, 0, 1024);
    b32(p, cff ? 0x4f54544fu : 0x10000u);
    b16(p + 4, num);
    b16(p + 6, 128);
    b16(p + 8, 3);
    b16(p + 10, num * 16 - 128);
    for (i = 0; i < 11; ++i) {
        unsigned len = lengths[i];
        unsigned char *e;
        if ((!cff && !i) || (cff && (i == 3 || i == 7)))
            continue;
        if (cff && i == 8)
            len = 6;
        e = p + 12 + record++ * 16u;
        b32(e, tags[i]);
        b32(e + 8, (uint32_t)off);
        b32(e + 12, len);
        if (i == 0) {
            p[off] = 1;
            p[off + 2] = 4;
            p[off + 3] = 1;
        }
        if (i == 4) {
            b32(p + off, 0x10000);
            b32(p + off + 12, 0x5f0f3cf5);
            b16(p + off + 18, 1000);
        }
        if (i == 5)
            b16(p + off + 34, 1);
        if (i == 7)
            b16(p + off + 2, 5);
        if (i == 8) {
            b32(p + off, cff ? 0x5000u : 0x10000u);
            b16(p + off + 4, 1);
        }
        if (i == 9)
            b16(p + off + 4, 6);
        if (i == 10)
            b32(p + off, 0x30000);
        off += pad(len);
    }
    CHECK(record == num);
    return off;
}

static size_t record(const unsigned char *p, uint32_t tag) {
    unsigned i;
    for (i = 0; i < u16(p + 4); ++i)
        if (u32(p + 12 + i * 16u) == tag)
            return 12 + i * 16u;
    CHECK(0);
    return 0;
}

static size_t woff(unsigned char *p, const unsigned char *raw) {
    unsigned i, num = u16(raw + 4);
    size_t off = 44 + 20u * num;
    uint32_t total = 12 + 16u * num;
    memset(p, 0, 2048);
    memcpy(p, "wOFF", 4);
    memcpy(p + 4, raw, 4);
    b16(p + 12, num);
    for (i = 0; i < num; ++i) {
        const unsigned char *r = raw + 12 + i * 16u;
        unsigned char *e = p + 44 + i * 20u;
        uint32_t len = u32(r + 12);
        memcpy(e, r, 4);
        b32(e + 4, (uint32_t)off);
        b32(e + 8, len);
        b32(e + 12, len);
        memcpy(p + off, raw + u32(r + 8), len);
        off += pad(len);
        total += (uint32_t)pad(len);
    }
    b32(p + 8, (uint32_t)off);
    b32(p + 16, total);
    return off;
}

static size_t collection(unsigned char *p, const unsigned char *raw, size_t n, int v2) {
    unsigned i, j, num = u16(raw + 4);
    size_t dir = 12 + 16u * num, header = v2 ? 32u : 20u, base = header + dir * 2;
    memset(p, 0, 2048);
    memcpy(p, "ttcf", 4);
    b32(p + 4, v2 ? 0x20000u : 0x10000u);
    b32(p + 8, 2);
    b32(p + 12, (uint32_t)header);
    b32(p + 16, (uint32_t)(header + dir));
    for (j = 0; j < 2; ++j) {
        unsigned char *face = p + header + j * dir;
        memcpy(face, raw, dir);
        for (i = 0; i < num; ++i) {
            unsigned char *e = face + 12 + i * 16u;
            b32(e + 8, (uint32_t)(base + u32(e + 8) - dir));
        }
    }
    memcpy(p + base, raw + dir, n - dir);
    return base + n - dir;
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

static void prefixes(const unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char *b = malloc(i ? i : 1);
        cm_result r;
        CHECK(b != NULL);
        memcpy(b, p, i);
        r = detect(b, i);
        CHECK(r.mime && r.reason);
        CHECK(cm_detect_prefix(b, i, &r) == CM_OK);
        CHECK(r.flags & CM_PARTIAL);
        free(b);
    }
}

static void raw_cases(unsigned char *seed, size_t *seedlen) {
    unsigned char p[2048], otf[1024];
    size_t n = sfnt(seed, 0), m = sfnt(otf, 1), at, i;
    cm_result r;
    *seedlen = n;
    expect(seed, n, "font/ttf", 0);
    expect(otf, m, "font/otf", 0);
    prefixes(seed, n);
    prefixes(otf, m);
    expect(seed, 172, "font/ttf", CM_TRUNCATED);
#define RESET() memcpy(p, seed, n)
    RESET();
    b16(p + 6, 0);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    b16(p + 4, 0);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    b16(p + 4, 129);
    expect(p, n, "application/octet-stream", CM_AMBIGUOUS);
    RESET();
    memcpy(p + 28, p + 12, 4);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    b32(p + 20, 1);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    b32(p + 20, 4);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    b32(p + 36, u32(p + 20));
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    at = record(p, 0x68656164);
    p[u32(p + at + 8) + 12] ^= 1;
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    at = record(p, 0x68656164);
    b16(p + u32(p + at + 8) + 18, 1);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    at = record(p, 0x6d617870);
    b16(p + u32(p + at + 8) + 4, 0);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    at = record(p, 0x68686561);
    b16(p + u32(p + at + 8) + 34, 2);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    at = record(p, 0x6c6f6361);
    b32(p + at + 12, 2);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    at = record(p, 0x68656164);
    b32(p + at + 8, 0xfffffffcu);
    expect(p, n, "font/ttf", CM_TRUNCATED);
    for (i = 0; i < n; ++i) {
        RESET();
        p[i] ^= 128;
        r = detect(p, n);
        CHECK(r.mime && r.reason);
    }
#undef RESET
}

static void wrapper_cases(const unsigned char *raw, size_t rawlen, unsigned char *seed,
                          size_t *seedlen) {
    unsigned char p[4096];
    size_t n = woff(seed, raw), m, at, face;
    unsigned char *head;
    *seedlen = n;
    expect(seed, n, "font/woff", 0);
    prefixes(seed, n);
#define RESET() memcpy(p, seed, n)
    RESET();
    b16(p + 14, 1);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    b32(p + 8, (uint32_t)n - 1);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    b32(p + 16, 0);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    b32(p + 44 + 8, 79);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    b32(p + 44 + 12, 0xfffffff0u);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    at = record(raw, 0x68656164);
    at = 44 + ((at - 12) / 16) * 20;
    b32(p + at + 8, 1);
    head = p + u32(p + at + 4);
    head[0] = 0xff;
    expect(p, n, "font/woff", 0); /* Compressed head payload is not read as raw fields. */
    RESET();
    b32(p + 24, 4);
    b32(p + 28, 4);
    b32(p + 32, 10);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    RESET();
    b32(p + 8, (uint32_t)n + 4);
    b32(p + 24, (uint32_t)n);
    b32(p + 28, 4);
    b32(p + 32, 10);
    memset(p + n, 0, 4);
    expect(p, n + 4, "font/woff", 0);
    b32(p + 36, (uint32_t)n);
    b32(p + 40, 4);
    expect(p, n + 4, "application/octet-stream", CM_MALFORMED);
    m = collection(p, raw, rawlen, 0);
    expect(p, m, "font/collection", 0);
    prefixes(p, m);
    face = u32(p + 16);
    b32(p + face + 12 + 4, 1);
    expect(p, m, "application/octet-stream", CM_MALFORMED);
    m = collection(p, raw, rawlen, 0);
    b32(p + 16, u32(p + 12));
    expect(p, m, "application/octet-stream", CM_MALFORMED);
    m = collection(p, raw, rawlen, 1);
    expect(p, m, "font/collection", 0);
    prefixes(p, m);
    b32(p + 20, 0x44534947u);
    b32(p + 24, 4);
    b32(p + 28, (uint32_t)m);
    memset(p + m, 0, 4);
    expect(p, m + 4, "font/collection", 0);
    expect(p, m, "font/collection", CM_TRUNCATED);
    b32(p + 28, u32(p + 12));
    expect(p, m + 4, "application/octet-stream", CM_MALFORMED);
    m = collection(p, raw, rawlen, 0);
    b32(p + 8, 33);
    expect(p, m, "application/octet-stream", CM_AMBIGUOUS);
    expect("wOF2", 4, "application/octet-stream", CM_AMBIGUOUS);
#undef RESET
}

int main(int argc, char **argv) {
    unsigned char raw[1024], wrapped[2048];
    size_t n, m;
    raw_cases(raw, &n);
    wrapper_cases(raw, n, wrapped, &m);
    if (argc == 3 || argc == 4) {
        FILE *f = fopen(argv[1], "wb");
        CHECK(f != NULL);
        CHECK(fwrite(raw, 1, n, f) == n);
        CHECK(!fclose(f));
        if (argc == 4) {
            unsigned char collected[2048];
            size_t size = collection(collected, raw, n, 1);
            f = fopen(argv[3], "wb");
            CHECK(f != NULL);
            CHECK(fwrite(collected, 1, size, f) == size);
            CHECK(!fclose(f));
        }
        f = fopen(argv[2], "wb");
        CHECK(f != NULL);
        CHECK(fwrite(wrapped, 1, m, f) == m);
        CHECK(!fclose(f));
    }
    printf("%u font checks passed\n", checks);
    return 0;
}
