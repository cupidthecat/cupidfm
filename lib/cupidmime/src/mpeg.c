#include "mpeg.h"
#include <stdint.h>
#include <string.h>

typedef unsigned char byte;

typedef struct {
    size_t length;
    unsigned version, rate, channels;
} frame;

static void condition(cm_result *r, unsigned flag, const char *why) {
    r->flags |= flag;
    r->reason = why;
}

static int sync(const byte *p, size_t n) {
    return n >= 2 && p[0] == 255 && (p[1] & 0xe6u) == 0xe2u;
}

static int synchsafe(const byte *p, uint32_t *out) {
    unsigned i;
    uint32_t v = 0;
    for (i = 0; i < 4; ++i) {
        if (p[i] & 128u)
            return 0;
        v = (v << 7) | p[i];
    }
    *out = v;
    return 1;
}

/* The caller establishes the complete ten-byte fixed header first. */
static int tag(const byte *p, size_t n, size_t *length, cm_result *r) {
    uint32_t body;
    size_t total;
    unsigned version = p[3], flags = p[5], mask;
    if (version == 255 || p[4] == 255 || !synchsafe(p + 6, &body)) {
        condition(r, CM_MALFORMED, "Invalid ID3 version or synchsafe tag size");
        return 0;
    }
    if (version < 2 || version > 4) {
        condition(r, CM_AMBIGUOUS, "Unsupported ID3 version; audio is unproven");
        return 0;
    }
    mask = version == 2 ? 0xc0u : version == 3 ? 0xe0u : 0xf0u;
    if (flags & ~mask) {
        condition(r, CM_MALFORMED, "ID3 header has reserved flag bits");
        return 0;
    }
    total = 10u + (size_t)body + ((version == 4 && (flags & 16u)) ? 10u : 0u);
    if (total > n) {
        condition(r, CM_TRUNCATED, "ID3 tag or footer exceeds inspection window");
        return 0;
    }
    if (version >= 3 && (flags & 64u)) {
        uint32_t ext;
        if (body < (version == 3 ? 10u : 6u)) {
            condition(r, CM_MALFORMED, "ID3 extended header is too short");
            return 0;
        }
        if (version == 4) {
            if (!synchsafe(p + 10, &ext) || ext < 6 || ext > body) {
                condition(r, CM_MALFORMED, "Invalid ID3v2.4 extended-header extent");
                return 0;
            }
        } else {
            ext = (uint32_t)p[10] << 24 | (uint32_t)p[11] << 16 | (uint32_t)p[12] << 8 | p[13];
            if (ext < 6 || ext > body - 4) {
                condition(r, CM_MALFORMED, "Invalid ID3v2.3 extended-header extent");
                return 0;
            }
        }
    }
    if (version == 4 && (flags & 16u)) {
        const byte *footer = p + 10 + body;
        if (memcmp(footer, "3DI", 3) || memcmp(footer + 3, p + 3, 7)) {
            condition(r, CM_MALFORMED, "ID3v2.4 footer disagrees with header");
            return 0;
        }
    }
    *length = total;
    return 1;
}

/* 1: supported Layer III header; 0: malformed; -1: unsupported free bitrate. */
static int header(const byte *p, frame *f) {
    static const unsigned rates[3] = {44100, 48000, 32000};
    static const unsigned mpeg1[15] = {0,   32,  40,  48,  56,  64,  80, 96,
                                       112, 128, 160, 192, 224, 256, 320};
    static const unsigned lower[15] = {0,  8,  16, 24,  32,  40,  48, 56,
                                       64, 80, 96, 112, 128, 144, 160};
    unsigned version = (p[1] >> 3) & 3u, index = p[2] >> 4;
    unsigned rate = (p[2] >> 2) & 3u, bitrate, scale, side;
    if (!sync(p, 4) || version == 1 || index == 15 || rate == 3 || (p[3] & 3u) == 2)
        return 0;
    if (!index)
        return -1;
    f->version = version;
    f->rate = rates[rate] / (version == 3 ? 1u : version == 2 ? 2u : 4u);
    f->channels = (p[3] >> 6) == 3 ? 1u : 2u;
    bitrate = (version == 3 ? mpeg1[index] : lower[index]) * 1000u;
    scale = version == 3 ? 144u : 72u;
    f->length = (size_t)bitrate * scale / f->rate + ((p[2] >> 1) & 1u);
    side = version == 3 ? (f->channels == 1 ? 17u : 32u) : (f->channels == 1 ? 9u : 17u);
    return f->length >= 4u + ((p[1] & 1u) ? 0u : 2u) + side;
}

int cm_mpeg_detect(const byte *p, size_t n, cm_result *r) {
    size_t off = 0;
    unsigned i;
    int tagged = 0;
    frame first = {0, 0, 0, 0};
    if (n >= 2 && n < 3 && !memcmp(p, "ID3", n)) {
        condition(r, CM_TRUNCATED, "Incomplete ID3 signature");
        return 1;
    }
    if (!(n >= 3 && !memcmp(p, "ID3", 3)) && !sync(p, n))
        return 0;
    while (n - off >= 3 && !memcmp(p + off, "ID3", 3)) {
        size_t length;
        tagged = 1;
        if (n - off < 10) {
            condition(r, CM_TRUNCATED, "ID3 fixed header is incomplete");
            return 1;
        }
        if (!tag(p + off, n - off, &length, r))
            return 1;
        off += length;
    }
    if (tagged && !sync(p + off, n - off)) {
        condition(r, CM_AMBIGUOUS, "ID3 metadata without confirmed Layer III audio");
        return 1;
    }
    for (i = 0; i < 3; ++i) {
        frame current;
        int status;
        if (n - off < 4) {
            condition(r, CM_TRUNCATED | CM_AMBIGUOUS,
                      "Three complete Layer III frames are not available");
            return 1;
        }
        status = header(p + off, &current);
        if (status <= 0) {
            condition(r, status < 0 ? CM_AMBIGUOUS : CM_MALFORMED,
                      status < 0 ? "Free-format Layer III bitrate is unsupported"
                                 : "Invalid Layer III candidate frame header");
            return 1;
        }
        if (i && (current.version != first.version || current.rate != first.rate ||
                  current.channels != first.channels)) {
            condition(r, CM_AMBIGUOUS, "Layer III candidate frames have incompatible parameters");
            return 1;
        }
        if (current.length > n - off) {
            condition(r, CM_TRUNCATED | CM_AMBIGUOUS,
                      "Layer III candidate frame exceeds inspection window");
            return 1;
        }
        if (!i)
            first = current;
        off += current.length;
    }
    r->mime = "audio/mpeg";
    r->evidence = CM_STRUCTURE;
    r->reason =
        "Three compatible Layer III frame extents checked; payload and later frames unchecked";
    return 1;
}
