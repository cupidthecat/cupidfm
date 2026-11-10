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

static size_t id(unsigned char *p, uint32_t value) {
    size_t width = value > 0xffffffu ? 4 : value > 0xffffu ? 3 : value > 0xffu ? 2 : 1, i;
    for (i = 0; i < width; ++i)
        p[i] = (unsigned char)(value >> (8 * (width - i - 1)));
    return width;
}

static size_t size(unsigned char *p, uint64_t value, unsigned width, int unknown) {
    unsigned i;
    for (i = 0; i < width; ++i)
        p[width - i - 1] = unknown ? 255 : (unsigned char)(value >> (8 * i));
    p[0] &= (unsigned char)((1u << (8 - width)) - 1);
    p[0] |= (unsigned char)(1u << (8 - width));
    return width;
}

static size_t element(unsigned char *p, uint32_t value, const void *data, size_t n) {
    size_t off = id(p, value);
    off += size(p + off, n, n < 127 ? 1 : 2, 0);
    memcpy(p + off, data, n);
    return off + n;
}

static size_t number(unsigned char *p, uint32_t value, unsigned n) {
    unsigned char v = (unsigned char)n;
    return element(p, value, &v, 1);
}

static size_t track(unsigned char *p, unsigned num, unsigned role, const char *codec) {
    unsigned char body[256];
    size_t off = number(body, 0xd7, num);
    off += number(body + off, 0x83, role);
    off += element(body + off, 0x86, codec, strlen(codec));
    return element(p, 0xae, body, off);
}

static size_t document(unsigned char *p, const char *doctype, const unsigned char *extra,
                       size_t extras, const unsigned char *entries, size_t count, unsigned width,
                       int unknown) {
    unsigned char h[256], body[8192];
    size_t n = element(h, 0x4282, doctype, strlen(doctype));
    size_t off, length = element(body, 0x1654ae6b, entries, count);
    memcpy(h + n, extra, extras);
    n += extras;
    off = element(p, 0x1a45dfa3, h, n);
    off += id(p + off, 0x18538067);
    off += size(p + off, length, width, unknown);
    memcpy(p + off, body, length);
    return off + length;
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

static size_t find(const unsigned char *p, size_t n, const void *needle, size_t len) {
    size_t i;
    for (i = 0; i + len <= n; ++i)
        if (!memcmp(p + i, needle, len))
            return i;
    CHECK(0);
    return 0;
}

static void prefixes(const unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char *b = malloc(i ? i : 1);
        cm_result r;
        CHECK(b != NULL);
        memcpy(b, p, i);
        r = detect(b, i);
        CHECK(strcmp(r.mime, "audio/webm") && strcmp(r.mime, "audio/matroska"));
        CHECK(cm_detect_prefix(b, i, &r) == CM_OK);
        CHECK(r.flags & CM_PARTIAL);
        CHECK(strcmp(r.mime, "audio/webm") && strcmp(r.mime, "audio/matroska"));
        free(b);
    }
}

static void profiles(unsigned char *seed, size_t *seedlen) {
    unsigned char p[8192], entries[4096], extra[128];
    size_t tracks, n, k;
    unsigned width;
    cm_result r;
    tracks = track(entries, 1, 2, "A_OPUS");
    for (width = 1; width <= 8; ++width) {
        n = document(p, "webm", extra, 0, entries, tracks, width, 0);
        expect(p, n, "audio/webm", 0);
        prefixes(p, n);
        if (width == 8) {
            memcpy(seed, p, n);
            *seedlen = n;
        }
        CHECK(cm_detect_prefix(p, n, &r) == CM_OK);
        CHECK(!strcmp(r.mime, "application/octet-stream"));
        CHECK(r.flags == (CM_PARTIAL | CM_AMBIGUOUS));
        n = document(p, "webm", extra, 0, entries, tracks, width, 1);
        expect(p, n, "audio/webm", 0);
    }
    tracks = track(entries, 1, 1, "V_VP9");
    n = document(p, "webm", extra, 0, entries, tracks, 8, 0);
    expect(p, n, "video/webm", 0);
    CHECK(cm_detect_prefix(p, n, &r) == CM_OK);
    CHECK(!strcmp(r.mime, "video/webm"));
    CHECK(r.flags == CM_PARTIAL);
    tracks += track(entries + tracks, 2, 2, "A_VORBIS");
    n = document(p, "webm", extra, 0, entries, tracks, 8, 0);
    expect(p, n, "video/webm", 0);
    tracks = track(entries, 1, 2, "A_FLAC");
    n = document(p, "matroska", extra, 0, entries, tracks, 2, 0);
    expect(p, n, "audio/matroska", 0);
    prefixes(p, n);
    tracks = track(entries, 1, 1, "V_MPEG4/ISO/ASP");
    n = document(p, "matroska", extra, 0, entries, tracks, 2, 0);
    expect(p, n, "video/matroska", 0);
    tracks = track(entries, 1, 2, "A_OPUS");
    n = document(p, "unknown", extra, 0, entries, tracks, 1, 0);
    expect(p, n, "application/octet-stream", CM_AMBIGUOUS);
    k = number(extra, 0x42f7, 2);
    n = document(p, "webm", extra, k, entries, tracks, 1, 0);
    expect(p, n, "application/octet-stream", CM_MALFORMED); /* read version exceeds writer */
    k += number(extra + k, 0x4286, 2);
    n = document(p, "webm", extra, k, entries, tracks, 1, 0);
    expect(p, n, "application/octet-stream", CM_AMBIGUOUS);
    k = number(extra, 0x42f3, 9);
    n = document(p, "webm", extra, k, entries, tracks, 1, 0);
    expect(p, n, "application/octet-stream", CM_AMBIGUOUS);
    k = number(extra, 0x42f2, 3);
    n = document(p, "webm", extra, k, entries, tracks, 1, 0);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    k = element(extra, 0x42f7, "", 0);
    n = document(p, "webm", extra, k, entries, tracks, 1, 0);
    expect(p, n, "audio/webm", 0); /* empty field uses its schema default */
    k = element(extra, 0x4282, "webm", 4);
    n = document(p, "webm", extra, k, entries, tracks, 1, 0);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
}

static void damage(const unsigned char *seed, size_t len) {
    unsigned char p[8192], entries[4096], extra[128];
    size_t at, n, tracks, k;
    cm_result r;
    memcpy(p, seed, len);
    p[4] = 0;
    expect(p, len, "application/octet-stream", CM_MALFORMED);
    memcpy(p, seed, len);
    p[4] = 255;
    expect(p, len, "application/octet-stream", CM_MALFORMED);
    memcpy(p, seed, len);
    at = find(p, len, "\xd7\x81\x01", 3);
    p[at + 2] = 0;
    expect(p, len, "application/octet-stream", CM_MALFORMED);
    memcpy(p, seed, len);
    at = find(p, len, "\x83\x81\x02", 3);
    p[at + 1] = 0x88;
    expect(p, len, "application/octet-stream",
           CM_MALFORMED); /* crosses TrackEntry, despite more file bytes */
    tracks = track(entries, 1, 2, "A_OPUS");
    tracks += track(entries + tracks, 1, 1, "V_VP8");
    n = document(p, "webm", extra, 0, entries, tracks, 1, 0);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    tracks = track(entries, 1, 1, "V_AV1");
    n = document(p, "webm", extra, 0, entries, tracks, 1, 0);
    expect(p, n, "application/octet-stream", CM_AMBIGUOUS);
    tracks = track(entries, 1, 1, "A_OPUS");
    n = document(p, "matroska", extra, 0, entries, tracks, 1, 0);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    tracks = track(entries, 1, 2, "A_OPUS");
    entries[1] += 2;
    entries[tracks++] = 0xe0;
    entries[tracks++] = 0x80;
    n = document(p, "webm", extra, 0, entries, tracks, 1, 0);
    expect(p, n, "application/octet-stream", CM_MALFORMED);
    tracks = track(entries, 1, 17, "D_WEBVTT/SUBTITLES");
    n = document(p, "webm", extra, 0, entries, tracks, 1, 0);
    expect(p, n, "application/octet-stream", CM_AMBIGUOUS);
    tracks = track(entries, 1, 2, "A_OPUS");
    n = document(p, "webm", extra, 0, entries, tracks, 8, 1);
    /* A finite child extending beyond an unknown-sized Segment's available bytes is truncated. */
    memcpy(p + n, "\xec\xfe", 2);
    expect(p, n + 2, "application/octet-stream", CM_TRUNCATED | CM_AMBIGUOUS);
    tracks = track(entries, 1, 1, "V_VP8");
    n = document(p, "webm", extra, 0, entries, tracks, 8, 1);
    memcpy(p + n, "\x1f\x43\xb6\x75\xff", 5);
    expect(p, n + 5, "video/webm", CM_AMBIGUOUS); /* unknown-sized Cluster stops inspection */
    n = document(p, "webm", extra, 0, entries, tracks, 8, 0);
    at = find(p, n, "\x18\x53\x80\x67", 4);
    (void)size(p + at + 4, n - at - 12 + 100, 8, 0);
    expect(p, n, "video/webm", CM_TRUNCATED | CM_AMBIGUOUS);
    n = document(p, "webm", extra, 0, entries, tracks, 1, 0);
    p[n] = 0;
    expect(p, n + 1, "video/webm", CM_AMBIGUOUS); /* trailing resource bytes */
    /* Track count cap keeps repeated-number checks bounded. */
    tracks = 0;
    for (k = 1; k <= 129; ++k)
        tracks += track(entries + tracks, (unsigned)k, 2, "A_OPUS");
    n = document(p, "webm", extra, 0, entries, tracks, 2, 0);
    expect(p, n, "application/octet-stream", CM_AMBIGUOUS);
    for (k = 0; k < len; ++k) {
        memcpy(p, seed, len);
        p[k] ^= 128;
        r = detect(p, len);
        CHECK(r.mime && r.reason);
    }
}

int main(int argc, char **argv) {
    unsigned char seed[128];
    size_t n;
    profiles(seed, &n);
    damage(seed, n);
    if (argc == 2) {
        FILE *f = fopen(argv[1], "wb");
        CHECK(f != NULL);
        CHECK(fwrite(seed, 1, n, f) == n);
        CHECK(!fclose(f));
    }
    printf("%u EBML checks passed\n", checks);
    return 0;
}
