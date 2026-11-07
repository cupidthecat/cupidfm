#include "zip.h"
#include <stdint.h>
#include <string.h>

typedef unsigned char byte;

static unsigned u16(const byte *p) {
    return p[0] | (unsigned)p[1] << 8;
}

static uint32_t u32(const byte *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int sig(const byte *p, size_t n, size_t off, const char *s) {
    return off <= n && n - off >= 4 && !memcmp(p + off, s, 4);
}

static void condition(cm_result *r, unsigned flag, const char *why) {
    r->flags |= flag;
    r->reason = why;
}

/* 0: ordinary fields; 1: ZIP64; -1: broken field extent. */
static int extras(const byte *p, size_t n) {
    size_t off = 0;
    int zip64 = 0;
    while (off < n) {
        unsigned id, len;
        if (n - off < 4)
            return -1;
        id = u16(p + off);
        len = u16(p + off + 2);
        off += 4;
        if (len > n - off)
            return -1;
        if (id == 1)
            zip64 = 1;
        off += len;
    }
    return zip64;
}

static uint32_t crc32(const byte *p, size_t n) {
    uint32_t crc = 0xffffffffu;
    size_t i;
    unsigned bit;
    for (i = 0; i < n; ++i) {
        crc ^= p[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    }
    return crc ^ 0xffffffffu;
}

static int descriptor(const byte *p, size_t limit, size_t off, const byte *central, size_t *end) {
    /* A CRC can equal the optional signature; try both representations. */
    size_t skip;
    for (skip = 0; skip <= 4; skip += 4) {
        size_t pos = off + skip;
        if (skip && !sig(p, limit, off, "PK\007\010"))
            continue;
        if (pos <= limit && limit - pos >= 12 && !memcmp(p + pos, central + 16, 12)) {
            *end = pos + 12;
            return 1;
        }
    }
    return 0;
}

void cm_zip_detect(const byte *p, size_t n, cm_result *r) {
    size_t off, eocd = 0, floor, cd, cdend, cursor, nextlocal = 0;
    unsigned candidates = 0, count, i, mimetypes = 0;
    int eligible = 1, epub = 0;
    if (sig(p, n, 0, "PK\003\004")) {
        size_t header;
        if (n < 30) {
            condition(r, CM_TRUNCATED, "ZIP local header is incomplete");
            return;
        }
        if (!u16(p + 26)) {
            condition(r, CM_MALFORMED, "ZIP entry has an empty filename");
            return;
        }
        header = 30u + (size_t)u16(p + 26) + u16(p + 28);
        r->mime = "application/zip";
        r->evidence = CM_STRUCTURE;
        r->flags |= CM_AMBIGUOUS;
        if (header > n || (!(u16(p + 6) & 8u) && u32(p + 18) != UINT32_MAX &&
                           (uint64_t)header + u32(p + 18) > n)) {
            condition(r, CM_TRUNCATED, "ZIP first entry exceeds inspection window");
            return;
        }
    } else {
        if (n < 22) {
            condition(r, CM_TRUNCATED, "ZIP end record is incomplete");
            return;
        }
        r->mime = "application/zip";
        r->evidence = CM_STRUCTURE;
        r->flags |= CM_AMBIGUOUS;
    }
    if (r->flags & CM_PARTIAL) {
        r->reason = "ZIP prefix; complete directory required for subtype detection";
        return;
    }
    if (n < 22) {
        condition(r, CM_TRUNCATED, "ZIP end record is absent");
        return;
    }
    floor = n > 65557u ? n - 65557u : 0;
    off = n - 22;
    for (;;) {
        if (sig(p, n, off, "PK\005\006") && u16(p + off + 20) == n - off - 22) {
            eocd = off;
            ++candidates;
        }
        if (off == floor)
            break;
        --off;
    }
    if (!candidates) {
        condition(r, CM_TRUNCATED, "ZIP end record or comment is incomplete");
        return;
    }
    if (candidates != 1) {
        r->reason = "Multiple ZIP end-record candidates; subtype unresolved";
        return;
    }
    count = u16(p + eocd + 10);
    if (u16(p + eocd + 4) || u16(p + eocd + 6) || count == 0xffffu ||
        u16(p + eocd + 8) == 0xffffu || u32(p + eocd + 12) == UINT32_MAX ||
        u32(p + eocd + 16) == UINT32_MAX || (eocd >= 20 && sig(p, n, eocd - 20, "PK\006\007"))) {
        r->reason = "ZIP64 or split ZIP is not inspected for subtypes";
        return;
    }
    if (u16(p + eocd + 8) != count) {
        condition(r, CM_MALFORMED, "ZIP entry counts disagree");
        return;
    }
    cd = u32(p + eocd + 16);
    if (cd > eocd || u32(p + eocd + 12) > eocd - cd) {
        condition(r, CM_MALFORMED, "ZIP directory exceeds end-record boundary");
        return;
    }
    cdend = cd + u32(p + eocd + 12);
    if (cdend != eocd) {
        r->reason = "ZIP directory gap or extension; subtype unresolved";
        return;
    }
    cursor = cd;
    for (i = 0; i < count; ++i) {
        const byte *c, *l;
        size_t record, local, header, dataend, localend;
        unsigned name, extra, flags, method;
        uint32_t compressed, uncompressed;
        int x;
        if (cursor > cdend || cdend - cursor < 46 || !sig(p, cdend, cursor, "PK\001\002")) {
            condition(r, CM_MALFORMED, "ZIP directory entry is missing or incomplete");
            return;
        }
        c = p + cursor;
        name = u16(c + 28);
        extra = u16(c + 30);
        record = 46u + (size_t)name + extra + u16(c + 32);
        if (!name || record > cdend - cursor) {
            condition(r, CM_MALFORMED, "ZIP directory variable fields exceed record extent");
            return;
        }
        flags = u16(c + 8);
        method = u16(c + 10);
        compressed = u32(c + 20);
        uncompressed = u32(c + 24);
        x = extras(c + 46 + name, extra);
        if (x < 0) {
            condition(r, CM_MALFORMED, "Invalid ZIP directory extra field");
            return;
        }
        if (x || compressed == UINT32_MAX || uncompressed == UINT32_MAX ||
            u32(c + 42) == UINT32_MAX || u16(c + 34) || (flags & ~0x080eu) ||
            (method != 0 && method != 8)) {
            r->reason = "ZIP entry uses unsupported metadata, encryption or compression";
            return;
        }
        local = u32(c + 42);
        /* Avoid rescanning overlapping local extra fields. Ordered, contiguous
         * local records also make the inspection cost linear without allocation. */
        if (local != nextlocal) {
            r->reason =
                "ZIP entries are reordered, overlapping or separated by gaps; subtype unresolved";
            return;
        }
        if (local > cd || cd - local < 30 || !sig(p, cd, local, "PK\003\004")) {
            condition(r, CM_MALFORMED, "ZIP directory references an invalid local header");
            return;
        }
        l = p + local;
        header = 30u + (size_t)u16(l + 26) + u16(l + 28);
        if (header > cd - local || u16(l + 26) != name || memcmp(l + 30, c + 46, name) ||
            u16(l + 6) != flags || u16(l + 8) != method) {
            condition(r, CM_MALFORMED, "ZIP local and directory metadata disagree");
            return;
        }
        x = extras(l + 30 + name, u16(l + 28));
        if (x < 0) {
            condition(r, CM_MALFORMED, "Invalid ZIP local extra field");
            return;
        }
        if (x || u32(l + 18) == UINT32_MAX || u32(l + 22) == UINT32_MAX) {
            r->reason = "ZIP64 local metadata; subtype unresolved";
            return;
        }
        if ((uint64_t)local + header + compressed > cd ||
            (method == 0 && compressed != uncompressed)) {
            condition(r, CM_MALFORMED, "ZIP entry size crosses directory or stored sizes disagree");
            return;
        }
        dataend = local + header + compressed;
        localend = dataend;
        if (flags & 8u) {
            unsigned k;
            for (k = 0; k < 3; ++k)
                if (u32(l + 14 + k * 4u) && u32(l + 14 + k * 4u) != u32(c + 16 + k * 4u)) {
                    condition(r, CM_MALFORMED, "ZIP local descriptor placeholders disagree");
                    return;
                }
            if (!descriptor(p, cd, dataend, c, &localend)) {
                condition(r, CM_MALFORMED, "ZIP data descriptor disagrees with directory");
                return;
            }
            eligible = 0;
        } else if (memcmp(l + 14, c + 16, 12)) {
            condition(r, CM_MALFORMED, "ZIP CRC or sizes disagree between headers");
            return;
        }
        nextlocal = localend;
        if (name == 8 && !memcmp(c + 46, "mimetype", 8)) {
            ++mimetypes;
            if (local == 0 && method == 0 && !(flags & ~0x0800u) && !u16(l + 28) && !extra &&
                compressed == 20 && !memcmp(l + header, "application/epub+zip", 20) &&
                u32(c + 16) == crc32(l + header, 20))
                epub = 1;
        }
        cursor += record;
    }
    if (cursor != cdend) {
        condition(r, CM_MALFORMED, "ZIP directory length and entry count disagree");
        return;
    }
    if (nextlocal != cd) {
        r->reason =
            "ZIP entries are reordered, overlapping or separated by gaps; subtype unresolved";
        return;
    }
    r->reason = "ZIP directory and local metadata checked; subtype unresolved";
    if (epub && mimetypes == 1 && eligible) {
        r->mime = "application/epub+zip";
        r->flags &= ~(unsigned)CM_AMBIGUOUS;
        r->reason = "EPUB stored mimetype and ZIP directory checked; publication content unchecked";
    }
}
