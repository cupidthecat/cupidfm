#include "cupidmime.h"
#include "audio.h"
#include "ebml.h"
#include "font.h"
#include "zip.h"
#include <stdint.h>
#include <string.h>

typedef unsigned char byte;

static uint32_t le32(const byte *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t be32(const byte *p) {
    return (uint32_t)p[3] | (uint32_t)p[2] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[0] << 24;
}

static unsigned le16(const byte *p) {
    return p[0] | (unsigned)p[1] << 8;
}

static int at(const byte *p, size_t n, size_t off, const char *s, size_t len) {
    return off <= n && len <= n - off && memcmp(p + off, s, len) == 0;
}

static void match(cm_result *r, const char *mime, cm_evidence e, const char *why) {
    r->mime = mime;
    r->evidence = e;
    r->reason = why;
}

static void reject(cm_result *r, unsigned flag, const char *why) {
    r->flags |= flag;
    r->reason = why;
}

static int space(byte c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static byte lower(byte c) {
    return c >= 'A' && c <= 'Z' ? (byte)(c + 32) : c;
}

static int ci(const byte *p, size_t n, const char *s) {
    size_t i, len = strlen(s);
    if (n < len)
        return 0;
    for (i = 0; i < len; ++i)
        if (lower(p[i]) != (byte)s[i])
            return 0;
    return 1;
}

/* Reject overlong sequences, surrogates and values beyond U+10FFFF. */
static int utf8(const byte *p, size_t n, int partial) {
    size_t i = 0;
    while (i < n) {
        byte c = p[i++];
        unsigned need, v, min;
        if (c < 0x80) {
            if ((c < 32 && !space(c) && c != '\f') || c == 127)
                return 0;
            continue;
        }
        if (c >= 0xc2 && c <= 0xdf) {
            need = 1;
            v = c & 31u;
            min = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            need = 2;
            v = c & 15u;
            min = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            need = 3;
            v = c & 7u;
            min = 0x10000;
        } else
            return 0;
        if (need > n - i) {
            /* Validate the available continuation bytes even at a prefix boundary. */
            size_t j;
            for (j = i; j < n; ++j)
                if ((p[j] & 0xc0) != 0x80)
                    return 0;
            if (i < n && ((c == 0xe0 && p[i] < 0xa0) || (c == 0xed && p[i] >= 0xa0) ||
                          (c == 0xf0 && p[i] < 0x90) || (c == 0xf4 && p[i] >= 0x90)))
                return 0;
            return partial;
        }
        while (need--) {
            if ((p[i] & 0xc0) != 0x80)
                return 0;
            v = (v << 6) | (p[i++] & 63u);
        }
        if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff))
            return 0;
    }
    return 1;
}

typedef struct {
    const byte *p;
    size_t n, i;
} json;

static void ws(json *j) {
    while (j->i < j->n && space(j->p[j->i]))
        ++j->i;
}

static int hex(byte c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    c = lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int codeunit(json *j, unsigned *v) {
    unsigned k;
    *v = 0;
    for (k = 0; k < 4; ++k) {
        int h;
        if (j->i == j->n || (h = hex(j->p[j->i++])) < 0)
            return 0;
        *v = (*v << 4) | (unsigned)h;
    }
    return 1;
}

static int string(json *j) {
    if (j->i == j->n || j->p[j->i++] != '"')
        return 0;
    while (j->i < j->n) {
        byte c = j->p[j->i++];
        if (c == '"')
            return 1;
        if (c < 32)
            return 0;
        if (c == '\\') {
            unsigned u, v;
            if (j->i == j->n)
                return 0;
            c = j->p[j->i++];
            if (c == 'u') {
                if (!codeunit(j, &u))
                    return 0;
                if (u >= 0xdc00 && u <= 0xdfff)
                    return 0;
                if (u >= 0xd800 && u <= 0xdbff) {
                    if (j->n - j->i < 2 || j->p[j->i++] != '\\' || j->p[j->i++] != 'u' ||
                        !codeunit(j, &v) || v < 0xdc00 || v > 0xdfff)
                        return 0;
                }
            } else if (!strchr("\"\\/bfnrt", c))
                return 0;
        }
    }
    return 0;
}

static int digit(byte c) {
    return c >= '0' && c <= '9';
}

static int value(json *j, unsigned depth) {
    byte c;
    size_t start;
    if (depth > 64)
        return 0;
    ws(j);
    if (j->i == j->n)
        return 0;
    c = j->p[j->i];
    if (c == '"')
        return string(j);
    if (c == '{' || c == '[') {
        byte end = c == '{' ? '}' : ']';
        ++j->i;
        ws(j);
        if (j->i < j->n && j->p[j->i] == end) {
            ++j->i;
            return 1;
        }
        for (;;) {
            if (c == '{') {
                if (!string(j))
                    return 0;
                ws(j);
                if (j->i == j->n || j->p[j->i++] != ':')
                    return 0;
            }
            if (!value(j, depth + 1))
                return 0;
            ws(j);
            if (j->i == j->n)
                return 0;
            if (j->p[j->i] == end) {
                ++j->i;
                return 1;
            }
            if (j->p[j->i++] != ',')
                return 0;
            ws(j);
        }
    }
    if (c == 't' || c == 'f' || c == 'n') {
        const char *lit = c == 't' ? "true" : c == 'f' ? "false" : "null";
        size_t len = strlen(lit);
        if (!at(j->p, j->n, j->i, lit, len))
            return 0;
        j->i += len;
        return 1;
    }
    start = j->i;
    if (c == '-')
        ++j->i;
    if (j->i == j->n)
        return 0;
    if (j->p[j->i] == '0')
        ++j->i;
    else {
        if (j->p[j->i] < '1' || j->p[j->i] > '9')
            return 0;
        do {
            ++j->i;
        } while (j->i < j->n && digit(j->p[j->i]));
    }
    if (j->i < j->n && j->p[j->i] == '.') {
        ++j->i;
        if (j->i == j->n || !digit(j->p[j->i]))
            return 0;
        while (j->i < j->n && digit(j->p[j->i]))
            ++j->i;
    }
    if (j->i < j->n && (j->p[j->i] == 'e' || j->p[j->i] == 'E')) {
        ++j->i;
        if (j->i < j->n && (j->p[j->i] == '+' || j->p[j->i] == '-'))
            ++j->i;
        if (j->i == j->n || !digit(j->p[j->i]))
            return 0;
        while (j->i < j->n && digit(j->p[j->i]))
            ++j->i;
    }
    return j->i > start;
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

static void gzip_header(const byte *p, size_t n, cm_result *r) {
    size_t off = 10;
    unsigned flag, k;
    if (n < 10) {
        reject(r, CM_TRUNCATED, "gzip fixed header is incomplete");
        return;
    }
    if (p[2] != 8 || (p[3] & 0xe0)) {
        reject(r, CM_MALFORMED, "Invalid gzip method or reserved flags");
        return;
    }
    flag = p[3];
    if (flag & 4u) {
        size_t len;
        if (n - off < 2) {
            reject(r, CM_TRUNCATED, "gzip extra length is incomplete");
            return;
        }
        len = le16(p + off);
        off += 2;
        if (len > n - off) {
            reject(r, CM_TRUNCATED, "gzip extra field is incomplete");
            return;
        }
        off += len;
    }
    for (k = 8; k <= 16; k *= 2) {
        if (flag & k) {
            while (off < n && p[off])
                ++off;
            if (off == n) {
                reject(r, CM_TRUNCATED, "gzip name or comment lacks a terminator");
                return;
            }
            ++off;
        }
    }
    if (flag & 2u) {
        if (n - off < 2) {
            reject(r, CM_TRUNCATED, "gzip header CRC is incomplete");
            return;
        }
        if ((crc32(p, off) & 0xffffu) != le16(p + off)) {
            reject(r, CM_MALFORMED, "gzip header CRC mismatch");
            return;
        }
        off += 2;
    }
    match(r, "application/gzip", CM_STRUCTURE,
          "gzip header and optional fields checked; DEFLATE and trailer unchecked");
    if (n - off < 10)
        r->flags |= CM_TRUNCATED; /* Minimum DEFLATE stream plus trailer. */
}

/* Check every available chunk against both the outer extent and the buffer.
 * Use 64-bit arithmetic so a 32-bit length plus padding cannot wrap. */
static int riff_chunks(const byte *p, size_t n, uint32_t size, cm_result *r) {
    uint64_t end = (uint64_t)size + 8, off = 12;
    while (off < end) {
        uint64_t next;
        uint32_t len;
        if (end - off < 8) {
            reject(r, CM_MALFORMED, "RIFF ends inside a chunk header");
            return 0;
        }
        if (off > n || n - (size_t)off < 8) {
            r->flags |= CM_TRUNCATED;
            return 1;
        }
        len = le32(p + (size_t)off + 4);
        next = off + 8 + (uint64_t)len + (len & 1u);
        if (next > end) {
            reject(r, CM_MALFORMED, "RIFF chunk or padding exceeds container");
            return 0;
        }
        if (next > n) {
            r->flags |= CM_TRUNCATED;
            return 1;
        }
        off = next;
    }
    return 1;
}

static int binary(const byte *p, size_t n, cm_result *r) {
    if (cm_font_detect(p, n, r))
        return 1;
    if (cm_ebml_detect(p, n, r))
        return 1;
    if (cm_audio_detect(p, n, r))
        return 1;
    if (at(p, n, 0, "\x89PNG\r\n\x1a\n", 8)) {
        if (n < 33)
            reject(r, CM_TRUNCATED, "PNG IHDR is incomplete");
        else if (be32(p + 8) != 13 || !at(p, n, 12, "IHDR", 4) || !be32(p + 16) || !be32(p + 20) ||
                 be32(p + 16) > 0x7fffffff || be32(p + 20) > 0x7fffffff || p[26] || p[27] ||
                 p[28] > 1 ||
                 !((p[25] == 0 &&
                    (p[24] == 1 || p[24] == 2 || p[24] == 4 || p[24] == 8 || p[24] == 16)) ||
                   ((p[25] == 2 || p[25] == 4 || p[25] == 6) && (p[24] == 8 || p[24] == 16)) ||
                   (p[25] == 3 && (p[24] == 1 || p[24] == 2 || p[24] == 4 || p[24] == 8))))
            reject(r, CM_MALFORMED, "Invalid PNG IHDR fields");
        else
            match(r, "image/png", CM_STRUCTURE,
                  "PNG IHDR fields checked; CRC and image data unchecked");
        return 1;
    }
    if (at(p, n, 0, "GIF87a", 6) || at(p, n, 0, "GIF89a", 6)) {
        if (n < 13)
            reject(r, CM_TRUNCATED, "GIF screen descriptor is incomplete");
        else if (!le16(p + 6) || !le16(p + 8))
            reject(r, CM_MALFORMED, "Zero GIF dimensions");
        else
            match(r, "image/gif", CM_STRUCTURE, "GIF logical screen dimensions checked");
        return 1;
    }
    if (at(p, n, 0, "\xff\xd8\xff", 3)) {
        if (n < 4)
            reject(r, CM_TRUNCATED, "JPEG marker is incomplete");
        else if (p[3] == 0 || p[3] == 0xff)
            reject(r, CM_MALFORMED, "Invalid initial JPEG marker");
        else
            match(r, "image/jpeg", CM_SIGNATURE, "JPEG start and marker prefix");
        return 1;
    }
    if (at(p, n, 0, "RIFF", 4)) {
        uint32_t size;
        const char *mime = NULL;
        if (n < 12) {
            reject(r, CM_TRUNCATED, "RIFF header is incomplete");
            return 1;
        }
        size = le32(p + 4);
        if (size < 4) {
            reject(r, CM_MALFORMED, "RIFF size excludes form type");
            return 1;
        }
        if ((uint64_t)size + 8 > n)
            r->flags |= CM_TRUNCATED;
        if (at(p, n, 8, "WEBP", 4)) {
            if (size < 12) {
                reject(r, CM_MALFORMED, "WebP has no chunk header");
                return 1;
            }
            if (n < 20) {
                reject(r, CM_TRUNCATED, "WebP chunk header is incomplete");
                return 1;
            }
            if (!at(p, n, 12, "VP8 ", 4) && !at(p, n, 12, "VP8L", 4) && !at(p, n, 12, "VP8X", 4)) {
                reject(r, CM_MALFORMED, "Unrecognized WebP first chunk");
                return 1;
            }
            if ((uint64_t)le32(p + 16) + 12 > size) {
                reject(r, CM_MALFORMED, "WebP chunk exceeds RIFF size");
                return 1;
            }
            mime = "image/webp";
        } else if (at(p, n, 8, "WAVE", 4))
            mime = "audio/wav";
        else if (at(p, n, 8, "AVI ", 4))
            mime = "video/x-msvideo";
        if (!riff_chunks(p, n, size, r))
            return 1;
        if (mime)
            match(r, mime, CM_STRUCTURE, "RIFF form, available chunk extents and padding checked");
        else
            r->flags |= CM_AMBIGUOUS;
        return 1;
    }
    if (at(p, n, 0, "PK\003\004", 4) || at(p, n, 0, "PK\005\006", 4)) {
        cm_zip_detect(p, n, r);
        return 1;
    }
    if (at(p, n, 0, "\x1f\x8b", 2)) {
        gzip_header(p, n, r);
        return 1;
    }
    if (at(p, n, 0, "%PDF-", 5)) {
        if (n < 9)
            reject(r, CM_TRUNCATED, "PDF version line is incomplete");
        else if ((p[5] != '1' && p[5] != '2') || p[6] != '.' || !digit(p[7]) ||
                 (p[8] != '\r' && p[8] != '\n'))
            reject(r, CM_MALFORMED, "Invalid PDF version line");
        else
            match(r, "application/pdf", CM_SIGNATURE, "PDF version line; body unchecked");
        return 1;
    }
    if (at(p, n, 0, "\177ELF", 4)) {
        size_t len;
        if (n < 16) {
            reject(r, CM_TRUNCATED, "ELF identification is incomplete");
            return 1;
        }
        len = p[4] == 1 ? 52 : 64;
        if ((p[4] != 1 && p[4] != 2) || (p[5] != 1 && p[5] != 2) || p[6] != 1)
            reject(r, CM_MALFORMED, "Invalid ELF class, endian or version");
        else if (n < len)
            reject(r, CM_TRUNCATED, "ELF file header is incomplete");
        else
            match(r, "application/x-elf", CM_STRUCTURE,
                  "ELF identification and header length checked");
        return 1;
    }
    if (at(p, n, 0, "SQLite format 3\0", 16)) {
        if (n < 100)
            reject(r, CM_TRUNCATED, "SQLite database header is incomplete");
        else {
            unsigned pagesize = (unsigned)p[16] * 256 + p[17];
            if (!((pagesize >= 512 && pagesize <= 32768 && !(pagesize & (pagesize - 1))) ||
                  pagesize == 1) ||
                (p[18] != 1 && p[18] != 2) || (p[19] != 1 && p[19] != 2) || p[21] != 64 ||
                p[22] != 32 || p[23] != 32)
                reject(r, CM_MALFORMED, "Invalid SQLite page size or format fields");
            else
                match(r, "application/vnd.sqlite3", CM_STRUCTURE,
                      "SQLite database header fields checked");
        }
        return 1;
    }
    if (at(p, n, 0, "\0asm", 4)) {
        if (n < 8)
            reject(r, CM_TRUNCATED, "WebAssembly version is incomplete");
        else if (le32(p + 4) != 1)
            reject(r, CM_MALFORMED, "Unsupported WebAssembly version");
        else
            match(r, "application/wasm", CM_STRUCTURE, "WebAssembly version 1 header");
        return 1;
    }
    if (n >= 8 && at(p, n, 4, "ftyp", 4)) {
        uint32_t size = be32(p);
        const char *mime = "application/octet-stream";
        size_t i, end;
        if (size < 16 || size % 4) {
            reject(r, CM_MALFORMED, "Invalid ftyp box size");
            return 1;
        }
        if (size > n) {
            reject(r, CM_TRUNCATED, "ftyp box is incomplete");
            return 1;
        }
        end = size;
        for (i = 8; i < end; i += 4) {
            if (i == 12)
                continue; /* minor version is not a brand */
            if (at(p, n, i, "avif", 4) || at(p, n, i, "avis", 4)) {
                mime = "image/avif";
                break;
            }
            if (at(p, n, i, "M4A ", 4))
                mime = "audio/mp4";
            else if (strcmp(mime, "application/octet-stream") == 0 &&
                     (at(p, n, i, "isom", 4) || at(p, n, i, "iso2", 4) || at(p, n, i, "mp41", 4) ||
                      at(p, n, i, "mp42", 4)))
                mime = "video/mp4";
        }
        match(r, mime, CM_STRUCTURE, "ISO BMFF ftyp brands checked; tracks unchecked");
        if (strcmp(mime, "application/octet-stream") == 0)
            r->flags |= CM_AMBIGUOUS;
        return 1;
    }
    if (n >= 265 && (at(p, n, 257, "ustar\0", 6) || at(p, n, 257, "ustar ", 6))) {
        size_t i;
        unsigned sum = 0, declared = 0;
        int seen = 0, ended = 0;
        if (n < 512) {
            reject(r, CM_TRUNCATED, "tar header is incomplete");
            return 1;
        }
        for (i = 0; i < 512; ++i)
            sum += i >= 148 && i < 156 ? 32u : p[i];
        for (i = 148; i < 156; ++i) {
            if (p[i] >= '0' && p[i] <= '7' && !ended) {
                declared = declared * 8 + p[i] - '0';
                seen = 1;
            } else if (p[i] == ' ' || p[i] == 0) {
                if (seen)
                    ended = 1;
            } else {
                reject(r, CM_MALFORMED, "Invalid tar checksum field");
                return 1;
            }
        }
        if (!seen || declared != sum)
            reject(r, CM_MALFORMED, "tar header checksum mismatch");
        else
            match(r, "application/x-tar", CM_STRUCTURE, "ustar header checksum checked");
        return 1;
    }
    return 0;
}

static cm_status detect(const void *data, size_t size, cm_result *out, int prefix) {
    const byte *p = data;
    size_t n = size, off = 0;
    cm_result r;
    if (!out || (!data && size))
        return CM_INVALID_ARGUMENT;
    if (n > CM_SCAN_LIMIT) {
        n = CM_SCAN_LIMIT;
        prefix = 1;
    }
    r.mime = "application/octet-stream";
    r.reason = "No supported format matched";
    r.evidence = CM_FALLBACK;
    r.flags = prefix ? CM_PARTIAL : 0;
    r.examined = n;
    if (!n) {
        r.reason = "Empty input";
        *out = r;
        return CM_OK;
    }
    if (binary(p, n, &r)) {
        *out = r;
        return CM_OK;
    }
    /* Preserve binary fallback for incomplete strong signatures. */
    {
        static const struct {
            const char *s;
            size_t n;
        } sigs[] = {{"\x89PNG\r\n\x1a\n", 8},
                    {"GIF87a", 6},
                    {"GIF89a", 6},
                    {"%PDF-", 5},
                    {"RIFF", 4},
                    {"\177ELF", 4},
                    {"PK\003\004", 4},
                    {"\0asm", 4},
                    {"SQLite format 3\0", 16}};

        size_t i;
        for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); ++i)
            if (n >= 2 && n < sigs[i].n && !memcmp(p, sigs[i].s, n)) {
                reject(&r, CM_TRUNCATED, "Incomplete known signature");
                *out = r;
                return CM_OK;
            }
    }
    if (!utf8(p, n, prefix)) {
        *out = r;
        return CM_OK;
    }
    match(&r, "text/plain", CM_TEXT, "Printable ASCII or well-formed UTF-8 text");
    if (at(p, n, 0, "\xef\xbb\xbf", 3))
        off = 3;
    while (off < n && space(p[off]))
        ++off;
    if (!prefix && off < n) {
        json j = {p, n, off};
        /* Scalars are deliberately left as text: 123 is weak format evidence. */
        if ((p[off] == '{' || p[off] == '[') && value(&j, 0)) {
            ws(&j);
            if (j.i == n)
                match(&r, "application/json", CM_STRUCTURE,
                      "Complete JSON object or array; depth at most 64");
        }
    }
    if (off < n) {
        static const char *tags[] = {"<!doctype html", "<html",   "<head",
                                     "<body",          "<script", "<iframe"};
        size_t i;
        for (i = 0; i < sizeof(tags) / sizeof(tags[0]); ++i) {
            size_t len = strlen(tags[i]);
            if (ci(p + off, n - off, tags[i]) && n - off > len &&
                (space(p[off + len]) || p[off + len] == '>')) {
                match(&r, "text/html", CM_SIGNATURE, "HTML leading token with delimiter");
                break;
            }
        }
        if (at(p, n, off, "<?xml", 5) && n - off > 5 && space(p[off + 5]))
            match(&r, "application/xml", CM_SIGNATURE,
                  "XML declaration prefix; document unchecked");
    }
    *out = r;
    return CM_OK;
}

cm_status cm_detect(const void *p, size_t n, cm_result *r) {
    return detect(p, n, r, 0);
}

cm_status cm_detect_prefix(const void *p, size_t n, cm_result *r) {
    return detect(p, n, r, 1);
}

const char *cm_version(void) {
    return CM_VERSION;
}
