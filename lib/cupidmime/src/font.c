#include "font.h"
#include <stdint.h>
#include <string.h>

typedef unsigned char byte;

typedef struct {
    uint64_t start, end;
} range;

typedef struct {
    uint32_t tag, offset, stored, original;
} table;

static unsigned be16(const byte *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static uint32_t be32(const byte *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int fail(cm_result *r, unsigned flag, const char *why) {
    r->flags |= flag;
    r->reason = why;
    return 0;
}

static uint64_t padded(uint32_t n) {
    return ((uint64_t)n + 3) & ~UINT64_C(3);
}

static int overlap(range a, range b) {
    return a.start < a.end && b.start < b.end && a.start < b.end && b.start < a.end;
}

static int flavor(uint32_t v) {
    return v == 0x00010000u || v == 0x4f54544fu;
}

static int count(unsigned n, cm_result *r) {
    if (!n || n > 4095)
        return fail(r, CM_MALFORMED, "Invalid SFNT table count");
    if (n > 128)
        return fail(r, CM_AMBIGUOUS, "Font table count exceeds bounded inspection policy");
    return 1;
}

static int shape(const byte *p, size_t n, size_t off, unsigned *tables, range *dir, cm_result *r) {
    unsigned power = 1, exponent = 0;
    if (off > n || n - off < 12)
        return fail(r, CM_TRUNCATED, "SFNT header is incomplete");
    if (!flavor(be32(p + off)))
        return fail(r, CM_AMBIGUOUS, "Unsupported SFNT flavor");
    *tables = be16(p + off + 4);
    if (!count(*tables, r))
        return 0;
    while (power <= *tables / 2) {
        power *= 2;
        ++exponent;
    }
    if (be16(p + off + 6) != power * 16 || be16(p + off + 8) != exponent ||
        be16(p + off + 10) != *tables * 16 - power * 16)
        return fail(r, CM_MALFORMED, "SFNT search fields disagree with table count");
    dir->start = off;
    dir->end = (uint64_t)off + 12 + 16u * (*tables);
    if (dir->end > n)
        return fail(r, CM_TRUNCATED, "SFNT table directory is incomplete");
    return 1;
}

static int readable(const table *t, size_t n, size_t bytes) {
    return t && t->stored == t->original && t->offset <= n && bytes <= n - t->offset;
}

static int validate(const byte *p, size_t n, const table *t, unsigned num, int cff, int woff,
                    const range *guards, unsigned protected, cm_result *r) {
    unsigned i, j, mask = 0, units = 0, glyphs = 0, format = 0, metrics = 0;
    const table *head = NULL, *maxp = NULL, *hhea = NULL, *hmtx = NULL, *loca = NULL;
    for (i = 0; i < num; ++i) {
        range data = {t[i].offset,
                      (uint64_t)t[i].offset + (woff ? padded(t[i].stored) : t[i].stored)};
        uint32_t minimum = 0;
        for (j = 0; j < 4; ++j) {
            unsigned c = (t[i].tag >> (8 * (3 - j))) & 255u;
            if (c < 32 || c > 126)
                return fail(r, CM_MALFORMED, "Non-ASCII SFNT table tag");
        }
        if ((i && t[i - 1].tag >= t[i].tag) || (t[i].offset & 3u))
            return fail(r, CM_MALFORMED, "Unsorted, duplicate or unaligned font table");
        if (t[i].stored > t[i].original || (t[i].original && !t[i].stored))
            return fail(r, CM_MALFORMED, "Invalid stored/original font table lengths");
        for (j = 0; j < protected; ++j)
            if (overlap(data, guards[j]))
                return fail(r, CM_MALFORMED,
                            "Font table overlaps header, directory or optional block");
        for (j = 0; j < i; ++j) {
            range prior = {t[j].offset,
                           (uint64_t)t[j].offset + (woff ? padded(t[j].stored) : t[j].stored)};
            if (overlap(data, prior))
                return fail(r, CM_MALFORMED, "Font table extents overlap");
        }
        if (t[i].stored && data.end > n)
            r->flags |= CM_TRUNCATED;
        switch (t[i].tag) {
            case 0x4f532f32:
                mask |= 1;
                minimum = 78;
                break; /* OS/2 */
            case 0x636d6170:
                mask |= 2;
                minimum = 4;
                break;
            case 0x68656164:
                mask |= 4;
                minimum = 54;
                head = &t[i];
                break;
            case 0x68686561:
                mask |= 8;
                minimum = 36;
                hhea = &t[i];
                break;
            case 0x686d7478:
                mask |= 16;
                minimum = 4;
                hmtx = &t[i];
                break;
            case 0x6d617870:
                mask |= 32;
                minimum = cff ? 6u : 32u;
                maxp = &t[i];
                break;
            case 0x6e616d65:
                mask |= 64;
                minimum = 6;
                break;
            case 0x706f7374:
                mask |= 128;
                minimum = 32;
                break;
            case 0x676c7966:
                mask |= 256;
                break;
            case 0x6c6f6361:
                mask |= 512;
                loca = &t[i];
                break;
            case 0x43464620:
            case 0x43464632:
                mask |= 1024;
                minimum = 4;
                break;
            default:
                break;
        }
        if (t[i].original < minimum)
            return fail(r, CM_MALFORMED, "Required font table is too short");
    }
    if ((mask & 255u) != 255u)
        return fail(r, CM_MALFORMED, "Required OpenType tables are missing");
    if (cff ? !(mask & 1024u) : (mask & 768u) != 768u)
        return fail(r, CM_AMBIGUOUS, "Supported font outline tables are absent");
    if (readable(head, n, 54)) {
        const byte *h = p + head->offset;
        units = be16(h + 18);
        format = be16(h + 50);
        if (be32(h) != 0x00010000u || be32(h + 12) != 0x5f0f3cf5u || units < 16 || units > 16384 ||
            format > 1 || be16(h + 52))
            return fail(r, CM_MALFORMED, "Invalid font head fields");
    }
    if (readable(maxp, n, 6)) {
        const byte *m = p + maxp->offset;
        glyphs = be16(m + 4);
        if (be32(m) != (cff ? 0x00005000u : 0x00010000u) || !glyphs)
            return fail(r, CM_MALFORMED, "Invalid font maxp fields");
    }
    if (glyphs && readable(hhea, n, 36)) {
        metrics = be16(p + hhea->offset + 34);
        if (!metrics || metrics > glyphs ||
            hmtx->original < (uint64_t)metrics * 4 + (glyphs - metrics) * 2)
            return fail(r, CM_MALFORMED, "Font horizontal metrics disagree with glyph count");
    }
    if (!cff && glyphs && units && loca->original < (uint64_t)(glyphs + 1) * (format ? 4u : 2u))
        return fail(r, CM_MALFORMED, "Font loca extent is too short for glyph count");
    return 1;
}

static int sfnt(const byte *p, size_t n, size_t off, unsigned num, const range *guards,
                unsigned protected, cm_result *r) {
    table t[128];
    unsigned i;
    for (i = 0; i < num; ++i) {
        const byte *e = p + off + 12 + i * 16u;
        t[i].tag = be32(e);
        t[i].offset = be32(e + 8);
        t[i].original = be32(e + 12);
        t[i].stored = t[i].original;
    }
    return validate(p, n, t, num, be32(p + off) == 0x4f54544fu, 0, guards, protected, r);
}

static void collection(const byte *p, size_t n, cm_result *r) {
    range guards[34];
    unsigned counts[32], i, j, total;
    uint32_t version;
    size_t header;
    if (n < 12) {
        (void)fail(r, CM_TRUNCATED, "Font collection header is incomplete");
        return;
    }
    version = be32(p + 4);
    total = be32(p + 8);
    if ((version != 0x10000u && version != 0x20000u) || !total) {
        (void)fail(r, CM_MALFORMED, "Invalid collection version or font count");
        return;
    }
    if (total > 32) {
        (void)fail(r, CM_AMBIGUOUS, "Collection exceeds 32-face inspection policy");
        return;
    }
    header = 12 + 4u * total + (version == 0x20000u ? 12u : 0u);
    if (header > n) {
        (void)fail(r, CM_TRUNCATED, "Collection offsets/signature header is incomplete");
        return;
    }
    guards[0].start = 0;
    guards[0].end = header;
    for (i = 0; i < total; ++i) {
        size_t off = be32(p + 12 + i * 4u);
        if ((off & 3u) || off < header) {
            (void)fail(r, CM_MALFORMED, "Invalid collection face offset");
            return;
        }
        if (!shape(p, n, off, &counts[i], &guards[i + 1], r))
            return;
        for (j = 1; j <= i; ++j)
            if (overlap(guards[i + 1], guards[j])) {
                (void)fail(r, CM_MALFORMED, "Collection face directories overlap");
                return;
            }
    }
    if (version == 0x20000u) {
        const byte *d = p + 12 + 4u * total;
        uint32_t tag = be32(d), len = be32(d + 4), off = be32(d + 8);
        if (!tag && (len || off)) {
            (void)fail(r, CM_MALFORMED, "Collection signature fields disagree");
            return;
        }
        if (tag) {
            range signature = {off, (uint64_t)off + len};
            if (tag != 0x44534947u || !len || (off & 3u)) {
                (void)fail(r, CM_MALFORMED, "Invalid collection signature extent");
                return;
            }
            for (j = 0; j <= total; ++j)
                if (overlap(signature, guards[j])) {
                    (void)fail(r, CM_MALFORMED, "Collection signature overlaps a directory");
                    return;
                }
            guards[total + 1] = signature;
            if (signature.end > n)
                r->flags |= CM_TRUNCATED;
        } else
            guards[total + 1].start = guards[total + 1].end = 0;
    } else
        guards[total + 1].start = guards[total + 1].end = 0;
    for (i = 0; i < total; ++i)
        if (!sfnt(p, n, (size_t)guards[i + 1].start, counts[i], guards, total + 2, r))
            return;
    /* A table can be shared across faces, but partial or contradictory sharing
     * must not hide collisions. Counts are capped before these comparisons. */
    for (i = 0; i < total; ++i)
        for (j = 0; j < i; ++j) {
            unsigned a, b;
            for (a = 0; a < counts[i]; ++a)
                for (b = 0; b < counts[j]; ++b) {
                    const byte *x = p + (size_t)guards[i + 1].start + 12 + a * 16u;
                    const byte *y = p + (size_t)guards[j + 1].start + 12 + b * 16u;
                    range xr = {be32(x + 8), (uint64_t)be32(x + 8) + be32(x + 12)};
                    range yr = {be32(y + 8), (uint64_t)be32(y + 8) + be32(y + 12)};
                    if (overlap(xr, yr) && memcmp(x, y, 16)) {
                        (void)fail(r, CM_MALFORMED,
                                   "Collection tables overlap without identical sharing metadata");
                        return;
                    }
                }
        }
    r->mime = "font/collection";
    r->evidence = CM_STRUCTURE;
    r->reason = "Font collection face directories and supported tables checked; glyphs/signatures "
                "unchecked";
}

static void woff(const byte *p, size_t n, cm_result *r) {
    table t[128];
    range guards[3];
    unsigned num, i;
    uint32_t length, v;
    uint64_t expanded, end;
    if (n < 44) {
        (void)fail(r, CM_TRUNCATED, "WOFF fixed header is incomplete");
        return;
    }
    length = be32(p + 8);
    num = be16(p + 12);
    v = be32(p + 4);
    if (!flavor(v)) {
        (void)fail(r, CM_AMBIGUOUS, "Unsupported WOFF SFNT flavor");
        return;
    }
    if (!count(num, r))
        return;
    end = 44 + 20u * num;
    expanded = 12 + 16u * num;
    if (be16(p + 14) || length < end || length < n) {
        (void)fail(r, CM_MALFORMED, "WOFF reserved field or declared length is invalid");
        return;
    }
    if (end > n) {
        (void)fail(r, CM_TRUNCATED, "WOFF directory is incomplete");
        return;
    }
    if (length > n)
        r->flags |= CM_TRUNCATED;
    guards[0].start = 0;
    guards[0].end = end;
    for (i = 0; i < num; ++i) {
        const byte *e = p + 44 + i * 20u;
        uint64_t tableend;
        t[i].tag = be32(e);
        t[i].offset = be32(e + 4);
        t[i].stored = be32(e + 8);
        t[i].original = be32(e + 12);
        tableend = (uint64_t)t[i].offset + padded(t[i].stored);
        if (t[i].stored && tableend > length) {
            (void)fail(r, CM_MALFORMED, "WOFF table exceeds declared file length");
            return;
        }
        if (t[i].stored && tableend > end)
            end = tableend;
        expanded += padded(t[i].original);
    }
    if (expanded != be32(p + 16)) {
        (void)fail(r, CM_MALFORMED, "WOFF reconstruction size disagrees with tables");
        return;
    }
    for (i = 0; i < 2; ++i) {
        uint32_t off = be32(p + (i ? 36 : 24)), len = be32(p + (i ? 40 : 28));
        guards[i + 1].start = off;
        guards[i + 1].end = (uint64_t)off + len;
        if ((!off && len) ||
            (off && (!len || (off & 3u) || off < end || guards[i + 1].end > length)) ||
            (!i && ((!off && be32(p + 32)) || (off && !be32(p + 32))))) {
            (void)fail(r, CM_MALFORMED, "WOFF metadata/private extent is invalid");
            return;
        }
        if (off) {
            end = guards[i + 1].end;
            if (end > n)
                r->flags |= CM_TRUNCATED;
        }
    }
    if (!validate(p, n, t, num, v == 0x4f54544fu, 1, guards, 3, r))
        return;
    r->mime = "font/woff";
    r->evidence = CM_STRUCTURE;
    r->reason = "WOFF directory, lengths and readable uncompressed headers checked; compressed "
                "data unchecked";
}

int cm_font_detect(const byte *p, size_t n, cm_result *r) {
    if (n < 4)
        return 0;
    if (flavor(be32(p))) {
        range dir;
        unsigned num;
        if (shape(p, n, 0, &num, &dir, r) && sfnt(p, n, 0, num, &dir, 1, r)) {
            r->mime = be32(p) == 0x10000u ? "font/ttf" : "font/otf";
            r->evidence = CM_STRUCTURE;
            r->reason =
                "SFNT directory and supported readable headers checked; glyphs/checksums unchecked";
        }
        return 1;
    }
    if (!memcmp(p, "ttcf", 4)) {
        collection(p, n, r);
        return 1;
    }
    if (!memcmp(p, "wOFF", 4)) {
        woff(p, n, r);
        return 1;
    }
    if (!memcmp(p, "wOF2", 4)) {
        (void)fail(r, CM_AMBIGUOUS, "WOFF2 directory inspection is not implemented");
        return 1;
    }
    return 0;
}
