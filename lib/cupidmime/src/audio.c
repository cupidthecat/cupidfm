#include "audio.h"
#include "mpeg.h"
#include <stdint.h>
#include <string.h>

typedef unsigned char byte;

static unsigned be16(const byte *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static uint32_t be24(const byte *p) {
    return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
}

static uint32_t le32(const byte *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void condition(cm_result *r, unsigned flag, const char *why) {
    r->flags |= flag;
    r->reason = why;
}

static void flac(const byte *p, size_t n, cm_result *r) {
    size_t off = 4;
    unsigned block = 0;
    for (;;) {
        unsigned type, last;
        size_t len;
        if (n - off < 4) {
            condition(r, CM_TRUNCATED, "FLAC metadata header is incomplete");
            return;
        }
        type = p[off] & 127u;
        last = p[off] & 128u;
        len = be24(p + off + 1);
        off += 4;
        if ((!block && (type || len != 34)) || (block && type == 0) || type == 127) {
            condition(r, CM_MALFORMED, "Invalid FLAC STREAMINFO order, length or metadata type");
            return;
        }
        if (len > n - off) {
            condition(r, CM_TRUNCATED, "FLAC metadata block exceeds inspection window");
            return;
        }
        if (!block) {
            unsigned min = be16(p + off), max = be16(p + off + 2);
            unsigned bits = ((p[off + 12] & 1u) << 4 | p[off + 13] >> 4) + 1;
            uint32_t minframe = be24(p + off + 4), maxframe = be24(p + off + 7);
            if (min < 16 || max < min || bits < 4 ||
                (minframe && maxframe && minframe > maxframe)) {
                condition(r, CM_MALFORMED, "Invalid FLAC block, frame-size or sample-depth fields");
                return;
            }
            r->mime = "audio/flac";
            r->evidence = CM_STRUCTURE;
        }
        off += len;
        ++block;
        if (last) {
            r->reason = "FLAC STREAMINFO and metadata extents checked; frames unchecked";
            return;
        }
    }
}

/* Ogg uses an MSB-first, non-reflected CRC with no initial/final XOR. */
static uint32_t page_crc(const byte *p, size_t n) {
    uint32_t crc = 0;
    size_t i;
    unsigned bit;
    for (i = 0; i < n; ++i) {
        byte c = i >= 22 && i < 26 ? 0 : p[i];
        crc ^= (uint32_t)c << 24;
        for (bit = 0; bit < 8; ++bit)
            crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04c11db7u : 0);
    }
    return crc;
}

/* 1: supported audio header, 0: unknown or unsupported, -1: invalid known header. */
static int codec(const byte *p, size_t n) {
    if (n >= 8 && !memcmp(p, "OpusHead", 8)) {
        if (n < 19 || !p[9])
            return -1;
        if (p[8] != 1 || p[18] != 0)
            return 0; /* Future versions/mappings stay generic. */
        if (n != 19 || p[9] > 2)
            return -1;
        return 1;
    }
    if (n >= 7 && !memcmp(p, "\001vorbis", 7)) {
        unsigned small, large;
        if (n < 30)
            return -1;
        if (le32(p + 7))
            return 0;
        small = p[28] & 15u;
        large = p[28] >> 4;
        if (n != 30 || !p[11] || !le32(p + 12) || small < 6 || large > 13 || small > large ||
            !(p[29] & 1u))
            return -1;
        return 1;
    }
    return 0;
}

static void ogg(const byte *p, size_t n, cm_result *r) {
    size_t off = 0;
    uint32_t serial = 0, seq = 0;
    int continued = 0, audio = 0, ended = 0;
    r->mime = "application/ogg";
    r->evidence = CM_SIGNATURE;
    r->flags |= CM_AMBIGUOUS;
    while (off < n) {
        const byte *page = p + off;
        size_t header, payload = 0, len;
        unsigned segments, flags, i;
        if (n - off < 27) {
            condition(r, CM_TRUNCATED, "Ogg page header is incomplete");
            return;
        }
        if (memcmp(page, "OggS", 4) || page[4] || (page[5] & ~7u)) {
            condition(r, CM_MALFORMED, "Invalid Ogg capture, version or reserved flags");
            return;
        }
        flags = page[5];
        segments = page[26];
        header = 27u + segments;
        if (header > n - off) {
            condition(r, CM_TRUNCATED, "Ogg segment table is incomplete");
            return;
        }
        for (i = 0; i < segments; ++i)
            payload += page[27 + i];
        len = header + payload;
        if (len > n - off) {
            condition(r, CM_TRUNCATED, "Ogg page payload is incomplete");
            return;
        }
        if (page_crc(page, len) != le32(page + 22)) {
            condition(r, CM_MALFORMED, "Ogg page CRC mismatch");
            return;
        }
        r->evidence = CM_STRUCTURE;
        if (off && (ended || le32(page + 14) != serial || (flags & 2u))) {
            r->reason = "Chained or multiplexed Ogg; audio subtype unresolved";
            return;
        }
        if (!off) {
            if (!(flags & 2u) || (flags & 1u) || le32(page + 18)) {
                condition(r, CM_MALFORMED, "Ogg beginning page has invalid flags or sequence");
                return;
            }
            serial = le32(page + 14);
            /* Supported identification packets occupy the beginning page alone. */
            if (segments == 1 && page[27] < 255) {
                audio = codec(page + header, payload);
                if (audio < 0) {
                    condition(r, CM_MALFORMED, "Invalid Ogg audio identification packet");
                    return;
                }
            }
        } else if (le32(page + 18) != seq || !!(flags & 1u) != continued) {
            condition(r, CM_MALFORMED, "Ogg page sequence or packet continuation disagrees");
            return;
        }
        ++seq;
        if (segments)
            continued = page[26 + segments] == 255;
        ended = !!(flags & 4u);
        if (ended && continued) {
            condition(r, CM_MALFORMED, "Ogg ends inside a packet");
            return;
        }
        off += len;
    }
    if (r->flags & CM_PARTIAL) {
        r->reason = "Ogg prefix; audio-only stream is unproven";
        return;
    }
    if (!ended) {
        condition(r, CM_TRUNCATED, "Ogg logical stream has no end page");
        return;
    }
    r->reason = "Ogg single-stream pages and CRCs checked; codec unresolved";
    if (audio) {
        r->mime = "audio/ogg";
        r->flags &= ~(unsigned)CM_AMBIGUOUS;
        r->reason = "Ogg Opus or Vorbis identification and single-stream pages checked; codec "
                    "payload unchecked";
    }
}

int cm_audio_detect(const byte *p, size_t n, cm_result *r) {
    if (cm_mpeg_detect(p, n, r))
        return 1;
    if (n >= 4 && !memcmp(p, "fLaC", 4)) {
        flac(p, n, r);
        return 1;
    }
    if (n >= 4 && !memcmp(p, "OggS", 4)) {
        ogg(p, n, r);
        return 1;
    }
    if (n >= 2 && n < 4 && (!memcmp(p, "fLaC", n) || !memcmp(p, "OggS", n))) {
        condition(r, CM_TRUNCATED, "Incomplete audio container signature");
        return 1;
    }
    return 0;
}
