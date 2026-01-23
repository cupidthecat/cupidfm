#define _POSIX_C_SOURCE 200809L
#include "arc_tar.h"
#include "arc_reader.h"
#include "arc_base.h"
#include "arc_stream.h"
#include "arc_filter.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h>

#define TAR_BLOCK_SIZE 512
#define TAR_NAME_SIZE 100
#define TAR_MODE_SIZE 8
#define TAR_UID_SIZE 8
#define TAR_GID_SIZE 8
#define TAR_SIZE_SIZE 12
#define TAR_MTIME_SIZE 12
#define TAR_CHKSUM_SIZE 8
#define TAR_TYPE_SIZE 1
#define TAR_LINKNAME_SIZE 100
#define TAR_MAGIC_SIZE 6
#define TAR_VERSION_SIZE 2
#define TAR_UNAME_SIZE 32
#define TAR_GNAME_SIZE 32
#define TAR_DEVMAJOR_SIZE 8
#define TAR_DEVMINOR_SIZE 8
#define TAR_PREFIX_SIZE 155

// TAR header structure (ustar)
struct TarHeader {
    char name[TAR_NAME_SIZE];
    char mode[TAR_MODE_SIZE];
    char uid[TAR_UID_SIZE];
    char gid[TAR_GID_SIZE];
    char size[TAR_SIZE_SIZE];
    char mtime[TAR_MTIME_SIZE];
    char chksum[TAR_CHKSUM_SIZE];
    char typeflag;
    char linkname[TAR_LINKNAME_SIZE];
    char magic[TAR_MAGIC_SIZE];
    char version[TAR_VERSION_SIZE];
    char uname[TAR_UNAME_SIZE];
    char gname[TAR_GNAME_SIZE];
    char devmajor[TAR_DEVMAJOR_SIZE];
    char devminor[TAR_DEVMINOR_SIZE];
    char prefix[TAR_PREFIX_SIZE];
    char padding[12];
};

// TAR type flags
#define TAR_REGTYPE   '0'
#define TAR_AREGTYPE  '\0'
#define TAR_LNKTYPE   '1'
#define TAR_SYMTYPE   '2'
#define TAR_CHRTYPE   '3'
#define TAR_BLKTYPE   '4'
#define TAR_DIRTYPE   '5'
#define TAR_FIFOTYPE  '6'
#define TAR_CONTTYPE  '7'
#define TAR_XHDTYPE   'x'  // pax extended header
#define TAR_XGLTYPE   'g'  // pax global header


#define TAR_GNUTYPE_SPARSE 'S'  // GNU old sparse file
#define TAR_GNUTYPE_LONGNAME 'L'
#define TAR_GNUTYPE_LONGLINK 'K'
// Format types (must match arc_reader.c)
#define ARC_FORMAT_TAR 0
#define ARC_FORMAT_ZIP 1

// TAR reader structure
// Note: ArcReader is actually a TarReader (they're the same)
typedef struct PaxState {
    char *path;
    char *linkpath;
    bool has_size;
    uint64_t size;
    bool has_uid;
    uint32_t uid;
    bool has_gid;
    uint32_t gid;
    bool has_mtime;
    uint64_t mtime;
    bool has_mode;
    uint32_t mode;

    // GNU sparse support (for correct sizing/listing; expansion can be implemented later)
    bool has_sparse_realsize;
    uint64_t sparse_realsize;
    char *sparse_map;             // GNU.sparse.map (v0.1)
    bool has_sparse_numblocks;
    uint64_t sparse_numblocks;    // GNU.sparse.numblocks (v0.0)
    uint64_t *sparse_offsets;     // GNU.sparse.offset (v0.0) repeated
    uint64_t *sparse_numbytes;    // GNU.sparse.numbytes (v0.0) repeated
    size_t sparse_pairs;          // number of offset/numbytes pairs collected
    char *sparse_name;            // GNU.sparse.name (v0.1/v1.0 real filename)
    bool has_sparse_version;
    int sparse_major;
    int sparse_minor;

} PaxState;

typedef struct TarReader {
    ArcReaderBase base;  // Must be first member for safe dispatch
    ArcEntry current_entry;
    bool entry_valid;
    int64_t entry_data_offset;
    int64_t entry_data_remaining;
    bool eof;

    // GNU tar extensions: long name/link apply to NEXT entry only
    char *gnu_longname;
    char *gnu_longlink;

    // POSIX pax
    PaxState pax_global;
} TarReader;

// Helper: Parse ASCII octal number from TAR header field (traditional encoding).
static uint64_t parse_octal_ascii(const char *str, size_t len) {
    // Skip leading NUL/space
    size_t i = 0;
    while (i < len && (str[i] == '\0' || str[i] == ' ')) i++;
    uint64_t val = 0;
    for (; i < len; i++) {
        char c = str[i];
        if (c == '\0' || c == ' ') break;
        if (c >= '0' && c <= '7') {
            val = (val * 8) + (uint64_t)(c - '0');
        } else {
            break;
        }
    }
    return val;
}

// Helper: Parse a TAR numeric field that may be ASCII-octal or GNU/star base-256.
// Base-256 is indicated by setting the high bit of the first byte.
// The value is stored as a big-endian two's-complement integer in (len*8 - 1) bits
// (the top bit is just the base-256 marker). See GNU tar and related implementations.
static int64_t parse_tar_number(const char *field, size_t len) {
    if (!field || len == 0) return 0;

    const unsigned char *p = (const unsigned char *)field;

    // Base-256 (binary) encoding?
    if (p[0] & 0x80) {
        // Build unsigned value from bytes with marker bit cleared.
        uint64_t u = 0;
        for (size_t i = 0; i < len; i++) {
            unsigned char b = p[i];
            if (i == 0) b &= 0x7F; // clear base-256 marker
            u = (u << 8) | (uint64_t)b;
        }

        // Sign-extend from (len*8 - 1) bits (marker bit excluded).
        int bits = (int)(len * 8) - 1;
        if (bits <= 0 || bits >= 64) {
            // If bits >= 64, we already filled u; treat as signed 64-bit.
            return (int64_t)u;
        }
        uint64_t sign_bit = 1ULL << (bits - 1);
        if (u & sign_bit) {
            uint64_t mask = (~0ULL) << bits;
            u |= mask;
        }
        return (int64_t)u;
    }

    // Traditional ASCII octal.
    return (int64_t)parse_octal_ascii(field, len);
}


// Helper: Check if block is all zeros (end of archive)
static bool is_zero_block(const uint8_t *block) {
    for (size_t i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (block[i] != 0) {
            return false;
        }
    }
    return true;
}

// Helper: Verify TAR header checksum
static bool verify_checksum(const struct TarHeader *hdr) {
    uint32_t sum = 0;
    const uint8_t *bytes = (const uint8_t *)hdr;
    
    // Calculate sum treating chksum field as spaces
    for (size_t i = 0; i < sizeof(struct TarHeader); i++) {
        if (i >= offsetof(struct TarHeader, chksum) &&
            i < offsetof(struct TarHeader, chksum) + TAR_CHKSUM_SIZE) {
            sum += ' ';
        } else {
            sum += bytes[i];
        }
    }
    
    uint32_t stored = (uint32_t)parse_octal_ascii(hdr->chksum, TAR_CHKSUM_SIZE);
    return sum == stored;
}

static void pax_clear(PaxState *st) {
    if (!st) return;
    free(st->path);
    free(st->linkpath);
    free(st->sparse_map);
    free(st->sparse_name);
    free(st->sparse_offsets);
    free(st->sparse_numbytes);
    memset(st, 0, sizeof(*st));
}

// Apply fields from src into dst (overwriting dst fields when present in src).
static void pax_merge(PaxState *dst, const PaxState *src) {
    if (!dst || !src) return;
    if (src->path) { free(dst->path); dst->path = strdup(src->path); }
    if (src->linkpath) { free(dst->linkpath); dst->linkpath = strdup(src->linkpath); }
    if (src->has_size) { dst->has_size = true; dst->size = src->size; }
    if (src->has_uid) { dst->has_uid = true; dst->uid = src->uid; }
    if (src->has_gid) { dst->has_gid = true; dst->gid = src->gid; }
    if (src->has_mtime) { dst->has_mtime = true; dst->mtime = src->mtime; }
    if (src->has_mode) { dst->has_mode = true; dst->mode = src->mode; }

    if (src->has_sparse_realsize) { dst->has_sparse_realsize = true; dst->sparse_realsize = src->sparse_realsize; }
    if (src->sparse_map) { free(dst->sparse_map); dst->sparse_map = strdup(src->sparse_map); }
    if (src->has_sparse_numblocks) { dst->has_sparse_numblocks = true; dst->sparse_numblocks = src->sparse_numblocks; }
    if (src->sparse_pairs) {
        free(dst->sparse_offsets);
        free(dst->sparse_numbytes);
        dst->sparse_offsets = calloc(src->sparse_pairs, sizeof(uint64_t));
        dst->sparse_numbytes = calloc(src->sparse_pairs, sizeof(uint64_t));
        if (dst->sparse_offsets && dst->sparse_numbytes) {
            memcpy(dst->sparse_offsets, src->sparse_offsets, src->sparse_pairs * sizeof(uint64_t));
            memcpy(dst->sparse_numbytes, src->sparse_numbytes, src->sparse_pairs * sizeof(uint64_t));
            dst->sparse_pairs = src->sparse_pairs;
        } else {
            free(dst->sparse_offsets); dst->sparse_offsets = NULL;
            free(dst->sparse_numbytes); dst->sparse_numbytes = NULL;
            dst->sparse_pairs = 0;
        }
    }
    if (src->sparse_name) { free(dst->sparse_name); dst->sparse_name = strdup(src->sparse_name); }
    if (src->has_sparse_version) { dst->has_sparse_version = true; dst->sparse_major = src->sparse_major; dst->sparse_minor = src->sparse_minor; }
}

// Old GNU sparse header parser (typeflag 'S').
// Parses sparse map entries from the header and any extension headers.
// Returns 0 on success; on success, sets *realsize and *stored_sum (sum of chunk sizes).
static int parse_oldgnu_sparse(ArcStream *stream, const unsigned char *hdr_block, uint64_t *realsize, uint64_t *stored_sum) {
    if (realsize) *realsize = 0;
    if (stored_sum) *stored_sum = 0;

    // Offsets per GNU tar manual. 
    const size_t SP0 = 386;      // 4 entries in main header
    const size_t ISEXT = 482;    // 1 byte
    const size_t REALSZ = 483;   // 12 bytes

    uint64_t real = (uint64_t)parse_tar_number((const char *)(hdr_block + REALSZ), 12);
    if (realsize) *realsize = real;

    uint64_t sum = 0;

    // main header: 4 entries
    for (int i = 0; i < 4; i++) {
        const unsigned char *e = hdr_block + SP0 + (size_t)i * 24;
        uint64_t off = (uint64_t)parse_tar_number((const char *)e, 12);
        uint64_t nb  = (uint64_t)parse_tar_number((const char *)(e + 12), 12);
        if (nb == 0) continue;
        sum += nb;
        (void)off;
    }

    bool isext = hdr_block[ISEXT] == '1';

    // extension headers: each is a 512-byte block with 21 entries and isextended at 504. 
    while (isext) {
        unsigned char ext[TAR_BLOCK_SIZE];
        ssize_t n = arc_stream_read(stream, ext, TAR_BLOCK_SIZE);
        if (n != TAR_BLOCK_SIZE) return -1;

        for (int i = 0; i < 21; i++) {
            const unsigned char *e = ext + (size_t)i * 24;
            uint64_t off = (uint64_t)parse_tar_number((const char *)e, 12);
            uint64_t nb  = (uint64_t)parse_tar_number((const char *)(e + 12), 12);
            (void)off;
            if (nb == 0) continue;
            sum += nb;
        }
        isext = ext[504] == '1';
    }

    if (stored_sum) *stored_sum = sum;
    return 0;
}

// Helper: skip N bytes from stream (seek if possible, else read/discard)
static int tar_skip_bytes(ArcStream *stream, uint64_t nbytes) {
    if (nbytes == 0) return 0;
    if (arc_stream_seek(stream, (int64_t)nbytes, SEEK_CUR) == 0) return 0;

    char discard[8192];
    uint64_t remaining = nbytes;
    while (remaining > 0) {
        size_t to_read = (remaining > sizeof(discard)) ? sizeof(discard) : (size_t)remaining;
        ssize_t n = arc_stream_read(stream, discard, to_read);
        if (n <= 0) return -1;
        remaining -= (uint64_t)n;
    }
    return 0;
}

static int tar_skip_padding(ArcStream *stream, uint64_t size) {
    uint64_t pad = (TAR_BLOCK_SIZE - (size % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE;
    if (pad == 0) return 0;
    return tar_skip_bytes(stream, pad);
}

// Parse a POSIX pax buffer of length `len` into `st`.
// Records are: <decimal_length><space><key>=<value>\n  (length includes entire record)
static int pax_parse_buffer(const char *buf, size_t len, PaxState *st) {
    size_t pos = 0;
    while (pos < len) {
        // Parse decimal record length
        size_t rec_len = 0;
        size_t digits = 0;
        while (pos + digits < len && isdigit((unsigned char)buf[pos + digits])) {
            if (digits > 20) return -1;
            rec_len = rec_len * 10 + (size_t)(buf[pos + digits] - '0');
            digits++;
        }
        if (digits == 0) break;
        if (pos + digits >= len || buf[pos + digits] != ' ') return -1;
        if (rec_len == 0 || pos + rec_len > len) return -1;

        size_t rec_start = pos;
        size_t rec_data = pos + digits + 1; // after space
        size_t rec_end  = pos + rec_len;    // one past end

        // record must end with \n (common); tolerate missing \n but clamp
        size_t payload_len = rec_end - rec_data;
        if (payload_len == 0) return -1;

        // Extract key=value from payload (strip trailing \n)
        const char *payload = buf + rec_data;
        size_t pl = payload_len;
        if (pl > 0 && payload[pl - 1] == '\n') pl--;

        const char *eq = memchr(payload, '=', pl);
        if (eq) {
            size_t key_len = (size_t)(eq - payload);
            size_t val_len = pl - key_len - 1;
            if (key_len > 0) {
                char key[64];
                if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
                memcpy(key, payload, key_len);
                key[key_len] = '\0';

                const char *val = eq + 1;
                // Only handle common fields for browsing.
                if (strcmp(key, "path") == 0) {
                    free(st->path);
                    st->path = strndup(val, val_len);
                } else if (strcmp(key, "linkpath") == 0) {
                    free(st->linkpath);
                    st->linkpath = strndup(val, val_len);
                } else if (strcmp(key, "size") == 0) {
                    st->has_size = true;
                    st->size = strtoull(val, NULL, 10);
                } else if (strcmp(key, "uid") == 0) {
                    st->has_uid = true;
                    st->uid = (uint32_t)strtoul(val, NULL, 10);
                } else if (strcmp(key, "gid") == 0) {
                    st->has_gid = true;
                    st->gid = (uint32_t)strtoul(val, NULL, 10);
                } else if (strcmp(key, "mtime") == 0) {
                    st->has_mtime = true;
                    // allow fractional seconds
                    double t = strtod(val, NULL);
                    if (t < 0) t = 0;
                    st->mtime = (uint64_t)t;
                } else if (strcmp(key, "mode") == 0) {
                    st->has_mode = true;
                    // mode is typically an octal string in pax
                    st->mode = (uint32_t)strtoul(val, NULL, 8);
                } else if (strcmp(key, "GNU.sparse.size") == 0) {
                    // PAX sparse v0.0/v0.1 real size
                    st->has_sparse_realsize = true;
                    st->sparse_realsize = strtoull(val, NULL, 10);
                } else if (strcmp(key, "GNU.sparse.realsize") == 0) {
                    // PAX sparse v1.0 real size
                    st->has_sparse_realsize = true;
                    st->sparse_realsize = strtoull(val, NULL, 10);
                } else if (strcmp(key, "GNU.sparse.map") == 0) {
                    free(st->sparse_map);
                    st->sparse_map = strndup(val, val_len);
                } else if (strcmp(key, "GNU.sparse.numblocks") == 0) {
                    st->has_sparse_numblocks = true;
                    st->sparse_numblocks = strtoull(val, NULL, 10);
                } else if (strcmp(key, "GNU.sparse.offset") == 0) {
                    // v0.0 repeats this key; collect pairs.
                    uint64_t v = strtoull(val, NULL, 10);
                    size_t n = st->sparse_pairs;
                    uint64_t *noff = realloc(st->sparse_offsets, (n + 1) * sizeof(uint64_t));
                    uint64_t *nnb  = realloc(st->sparse_numbytes, (n + 1) * sizeof(uint64_t));
                    if (noff && nnb) {
                        st->sparse_offsets = noff;
                        st->sparse_numbytes = nnb;
                        st->sparse_offsets[n] = v;
                        st->sparse_numbytes[n] = 0; // filled when GNU.sparse.numbytes arrives
                        st->sparse_pairs = n + 1;
                    }
                } else if (strcmp(key, "GNU.sparse.numbytes") == 0) {
                    uint64_t v = strtoull(val, NULL, 10);
                    if (st->sparse_pairs > 0) {
                        st->sparse_numbytes[st->sparse_pairs - 1] = v;
                    }
                } else if (strcmp(key, "GNU.sparse.name") == 0) {
                    free(st->sparse_name);
                    st->sparse_name = strndup(val, val_len);
                } else if (strcmp(key, "GNU.sparse.major") == 0) {
                    st->has_sparse_version = true;
                    st->sparse_major = (int)strtol(val, NULL, 10);
                } else if (strcmp(key, "GNU.sparse.minor") == 0) {
                    st->has_sparse_version = true;
                    st->sparse_minor = (int)strtol(val, NULL, 10);

                }
            }
        }

        pos = rec_start + rec_len;
    }
    return 0;
}

static int pax_read_records(ArcStream *stream, uint64_t size, PaxState *st) {
    if (size == 0) return 0;
    if (size > 1024ULL * 1024ULL) { // 1 MiB sanity limit for pax headers
        errno = EOVERFLOW;
        return -1;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) return -1;

    uint64_t remaining = size;
    size_t off = 0;
    while (remaining > 0) {
        size_t chunk = (remaining > 65536) ? 65536 : (size_t)remaining;
        ssize_t n = arc_stream_read(stream, buf + off, chunk);
        if (n <= 0) {
            free(buf);
            return -1;
        }
        off += (size_t)n;
        remaining -= (uint64_t)n;
    }
    buf[size] = '\0';

    int r = pax_parse_buffer(buf, (size_t)size, st);
    free(buf);
    return r;
}

static char *tar_read_long_text(ArcStream *stream, uint64_t size) {
    if (size == 0) return strdup("");
    if (size > 1024ULL * 1024ULL) { errno = EOVERFLOW; return NULL; }
    char *buf = malloc((size_t)size + 1);
    if (!buf) return NULL;

    uint64_t remaining = size;
    size_t off = 0;
    while (remaining > 0) {
        size_t chunk = (remaining > 65536) ? 65536 : (size_t)remaining;
        ssize_t n = arc_stream_read(stream, buf + off, chunk);
        if (n <= 0) { free(buf); return NULL; }
        off += (size_t)n;
        remaining -= (uint64_t)n;
    }
    buf[size] = '\0';

    // Trim at first NUL or newline
    size_t out_len = 0;
    while (out_len < (size_t)size && buf[out_len] != '\0' && buf[out_len] != '\n') out_len++;
    buf[out_len] = '\0';

    // Shrink (optional)
    char *out = strdup(buf);
    free(buf);
    return out;
}

// Read next TAR entry
static int tar_read_entry(struct TarReader *reader) {
    if (reader->eof) {
        return 1; // Done
    }
    
    // Read header block
    struct TarHeader hdr;
    memset(&hdr, 0, sizeof(hdr)); // Initialize to zero
    ssize_t n = arc_stream_read(reader->base.stream, &hdr, sizeof(hdr));
    if (n < 0) {
        return -1; // Read error
    }
    if (n == 0) {
        // EOF reached
        reader->eof = true;
        return 1; // Done
    }
    if (n != sizeof(hdr)) {
        // Partial read - this shouldn't happen for a valid TAR
        // This can happen if the filter stream isn't reading correctly
        return -1;
    }
    
    // Check for zero block (end of archive)
    if (is_zero_block((const uint8_t *)&hdr)) {
        reader->eof = true;
        return 1; // Done
    }
    
    // Verify checksum
    // Note: Checksum verification can fail if:
    // 1. The stream position is wrong (reading from wrong offset)
    // 2. The header is corrupted
    // 3. The filter stream isn't decompressing correctly
    if (!verify_checksum(&hdr)) {
        // Checksum failed - might be corrupted or not a TAR file
        // This can happen if the stream position is wrong or the header is corrupted
        // For debugging: check if we got any valid data
        if (hdr.name[0] == '\0' && is_zero_block((const uint8_t *)&hdr)) {
            // Actually a zero block, should have been caught earlier
            reader->eof = true;
            return 1;
        }
        // Checksum mismatch - likely stream position issue or corrupted data
        return -1;
    }
    
    // Free previous entry
    arc_entry_free(&reader->current_entry);
    memset(&reader->current_entry, 0, sizeof(reader->current_entry));
    
    // Handle POSIX pax headers (x/g) and GNU longname/longlink (L/K).
    // These special entries apply to the *next* real entry.
    PaxState pax_local;
    memset(&pax_local, 0, sizeof(pax_local));

    while (hdr.typeflag == TAR_XHDTYPE || hdr.typeflag == TAR_XGLTYPE || hdr.typeflag == TAR_GNUTYPE_LONGNAME || hdr.typeflag == TAR_GNUTYPE_LONGLINK) {
        uint64_t meta_size = parse_tar_number(hdr.size, TAR_SIZE_SIZE);

        if (hdr.typeflag == TAR_XGLTYPE) {
            // Global pax: applies to all following entries (until overridden)
            PaxState tmp; memset(&tmp, 0, sizeof(tmp));
            if (pax_read_records(reader->base.stream, meta_size, &tmp) < 0) { pax_clear(&tmp); pax_clear(&pax_local); return -1; }
            if (tar_skip_padding(reader->base.stream, meta_size) < 0) { pax_clear(&tmp); pax_clear(&pax_local); return -1; }
            pax_merge(&reader->pax_global, &tmp);
            pax_clear(&tmp);
        } else if (hdr.typeflag == TAR_XHDTYPE) {
            // Per-file pax: applies to next entry only
            PaxState tmp; memset(&tmp, 0, sizeof(tmp));
            if (pax_read_records(reader->base.stream, meta_size, &tmp) < 0) { pax_clear(&tmp); pax_clear(&pax_local); return -1; }
            if (tar_skip_padding(reader->base.stream, meta_size) < 0) { pax_clear(&tmp); pax_clear(&pax_local); return -1; }
            pax_merge(&pax_local, &tmp);
            pax_clear(&tmp);
        } else if (hdr.typeflag == TAR_GNUTYPE_LONGNAME) {
            // GNU long filename for next entry
            free(reader->gnu_longname);
            reader->gnu_longname = tar_read_long_text(reader->base.stream, meta_size);
            if (!reader->gnu_longname) { pax_clear(&pax_local); return -1; }
            if (tar_skip_padding(reader->base.stream, meta_size) < 0) { pax_clear(&pax_local); return -1; }
        } else if (hdr.typeflag == TAR_GNUTYPE_LONGLINK) {
            // GNU long link name for next entry
            free(reader->gnu_longlink);
            reader->gnu_longlink = tar_read_long_text(reader->base.stream, meta_size);
            if (!reader->gnu_longlink) { pax_clear(&pax_local); return -1; }
            if (tar_skip_padding(reader->base.stream, meta_size) < 0) { pax_clear(&pax_local); return -1; }
        }

        // Read the next header (the real entry or another metadata header)
        memset(&hdr, 0, sizeof(hdr));
        n = arc_stream_read(reader->base.stream, &hdr, sizeof(hdr));
        if (n == 0) { reader->eof = true; pax_clear(&pax_local); return 1; }
        if (n != sizeof(hdr)) { pax_clear(&pax_local); return -1; }
        if (is_zero_block((const uint8_t *)&hdr)) { reader->eof = true; pax_clear(&pax_local); return 1; }
        if (!verify_checksum(&hdr)) { pax_clear(&pax_local); return -1; }
    }

    // Parse entry sizes.
// - stored_size: number of data bytes actually present in the archive for this member (what we must skip/read)
// - real_size: logical file size shown to the user (may differ for sparse files)
    uint64_t stored_size = (uint64_t)parse_tar_number(hdr.size, TAR_SIZE_SIZE);
    uint64_t real_size = stored_size;

    // Old GNU sparse ('S'): header is followed by one or more extension sparse headers before file data. 
    if (hdr.typeflag == TAR_GNUTYPE_SPARSE) {
        uint64_t rs = 0, sum = 0;
        if (parse_oldgnu_sparse(reader->base.stream, (const unsigned char *)&hdr, &rs, &sum) < 0) {
            pax_clear(&pax_local);
            return -1;
        }
        if (rs) real_size = rs;
        // stored_size should already be the condensed data size in hdr.size, but sum is available if you want to sanity-check.
        (void)sum;
    }

    // PAX sparse: real size stored in GNU.sparse.size or GNU.sparse.realsize. 
    if (pax_local.has_sparse_realsize) {
        real_size = pax_local.sparse_realsize;
    }

    // Hardlinks have no file data in the archive; treat size as 0 for skipping/open_data. 
    if (hdr.typeflag == TAR_LNKTYPE) {
        stored_size = 0;
        real_size = 0;
    }

    // Build path
    char path[TAR_PREFIX_SIZE + TAR_NAME_SIZE + 2];
    if (hdr.prefix[0] != '\0') {
        snprintf(path, sizeof(path), "%.*s/%.*s",
                 (int)TAR_PREFIX_SIZE, hdr.prefix,
                 (int)TAR_NAME_SIZE, hdr.name);
    } else {
        snprintf(path, sizeof(path), "%.*s",
                 (int)TAR_NAME_SIZE, hdr.name);
    }

    // Normalize path (remove leading ./ and //)
    char *normalized = path;
    while (normalized[0] == '.' && normalized[1] == '/') {
        normalized += 2;
    }
    while (normalized[0] == '/' && normalized[1] == '/') {
        normalized++;
    }

    // Apply global pax defaults then per-file overrides.
    const char *final_path = NULL;
    if (pax_local.path) {
        final_path = pax_local.path;
    } else if (reader->gnu_longname) {
        final_path = reader->gnu_longname;
    } else if (pax_local.sparse_name) {
        // Sparse v0.1/v1.0: real name is stored in GNU.sparse.name. 
        final_path = pax_local.sparse_name;
    } else if (reader->pax_global.path) {
        final_path = reader->pax_global.path;
    }

    reader->current_entry.path = strdup(final_path ? final_path : normalized);
    reader->current_entry.size = real_size;

    // mode/uid/gid/mtime
    uint32_t mode = (uint32_t)parse_tar_number(hdr.mode, TAR_MODE_SIZE);
    uint32_t uid  = (uint32_t)parse_tar_number(hdr.uid, TAR_UID_SIZE);
    uint32_t gid  = (uint32_t)parse_tar_number(hdr.gid, TAR_GID_SIZE);
    uint64_t mtime = parse_tar_number(hdr.mtime, TAR_MTIME_SIZE);

    if (reader->pax_global.has_mode) mode = reader->pax_global.mode;
    if (reader->pax_global.has_uid)  uid  = reader->pax_global.uid;
    if (reader->pax_global.has_gid)  gid  = reader->pax_global.gid;
    if (reader->pax_global.has_mtime) mtime = reader->pax_global.mtime;

    if (pax_local.has_mode) mode = pax_local.mode;
    if (pax_local.has_uid)  uid  = pax_local.uid;
    if (pax_local.has_gid)  gid  = pax_local.gid;
    if (pax_local.has_mtime) mtime = pax_local.mtime;

    reader->current_entry.mode = mode;
    reader->current_entry.mtime = mtime;
    reader->current_entry.uid = uid;
    reader->current_entry.gid = gid;
    
    // Determine type
    if (hdr.typeflag == TAR_DIRTYPE || hdr.typeflag == TAR_REGTYPE || hdr.typeflag == TAR_AREGTYPE || hdr.typeflag == TAR_GNUTYPE_SPARSE) {
        reader->current_entry.type = (hdr.typeflag == TAR_DIRTYPE) ? ARC_ENTRY_DIR : ARC_ENTRY_FILE;
    } else if (hdr.typeflag == TAR_SYMTYPE) {
        reader->current_entry.type = ARC_ENTRY_SYMLINK;
        const char *lt = pax_local.linkpath ? pax_local.linkpath : (reader->gnu_longlink ? reader->gnu_longlink : NULL);
        reader->current_entry.link_target = lt ? strdup(lt) : strndup(hdr.linkname, TAR_LINKNAME_SIZE);
    } else if (hdr.typeflag == TAR_LNKTYPE) {
        reader->current_entry.type = ARC_ENTRY_HARDLINK;
        const char *lt = pax_local.linkpath ? pax_local.linkpath : (reader->gnu_longlink ? reader->gnu_longlink : NULL);
        reader->current_entry.link_target = lt ? strdup(lt) : strndup(hdr.linkname, TAR_LINKNAME_SIZE);
    } else {
        reader->current_entry.type = ARC_ENTRY_OTHER;
    }
    
    reader->entry_valid = true;
    reader->entry_data_offset = arc_stream_tell(reader->base.stream);
    reader->entry_data_remaining = (int64_t)stored_size;

    // Metadata overrides were only for this entry.
    pax_clear(&pax_local);
    free(reader->gnu_longname); reader->gnu_longname = NULL;
    free(reader->gnu_longlink); reader->gnu_longlink = NULL;
    
    return 0;
}

// ArcReader vtable for TAR
int arc_tar_next(ArcReader *reader, ArcEntry *entry) {
    if (!reader || !entry) {
        return -1;
    }
    TarReader *tar = (TarReader *)reader;
    
    // If we have a valid entry with data remaining, skip it before reading next.
    if (tar->entry_valid && tar->entry_data_remaining > 0) {
        uint64_t prev = (uint64_t)tar->entry_data_remaining;
        if (tar_skip_bytes(tar->base.stream, prev) < 0) return -1;
        if (tar_skip_padding(tar->base.stream, prev) < 0) return -1;
        tar->entry_data_remaining = 0;
        tar->entry_valid = false;
    }
    
    int ret = tar_read_entry(tar);
    if (ret == 0) {
        *entry = tar->current_entry;
        // Don't clear entry_valid here - keep it valid so arc_open_data() can work
        // The entry will be invalidated when we read the next entry or skip data
        // Clear the entry structure so it's not freed when reader closes
        // But keep entry_valid = true and entry_data_* so arc_open_data() works
        memset(&tar->current_entry, 0, sizeof(tar->current_entry));
        // Note: entry_valid stays true, entry_data_offset and entry_data_remaining are preserved
    }
    return ret;
}

ArcStream *arc_tar_open_data(ArcReader *reader) {
    if (!reader) {
        return NULL;
    }
    TarReader *tar = (TarReader *)reader;
    if (!tar->entry_valid || tar->entry_data_remaining == 0) {
        return NULL;
    }
    
    // Create substream for entry data
    // Note: We don't invalidate entry_valid here - it will be invalidated
    // when arc_next() is called again or arc_skip_data() is called
    return arc_stream_substream(tar->base.stream, tar->entry_data_offset, tar->entry_data_remaining);
}

int arc_tar_skip_data(ArcReader *reader) {
    if (!reader) {
        return -1;
    }
    TarReader *tar = (TarReader *)reader;
    if (!tar->entry_valid) {
        return -1;
    }
    
    uint64_t prev = (uint64_t)tar->entry_data_remaining;
    if (tar_skip_bytes(tar->base.stream, prev) < 0) return -1;
    if (tar_skip_padding(tar->base.stream, prev) < 0) return -1;
    
    tar->entry_data_remaining = 0;
    tar->entry_valid = false; // Mark as invalid after skipping
    return 0;
}

void arc_tar_close(ArcReader *reader) {
    if (!reader) {
        return;
    }
    TarReader *tar = (TarReader *)reader;
    arc_entry_free(&tar->current_entry);
    // Close base.stream first, then owned_stream (filters don't close underlying).
    if (tar->base.stream) {
        arc_stream_close(tar->base.stream);
    }
    if (tar->base.owned_stream && tar->base.owned_stream != tar->base.stream) {
        arc_stream_close(tar->base.owned_stream);
    }
    pax_clear(&tar->pax_global);
    free(tar->gnu_longname);
    free(tar->gnu_longlink);
    free(tar);
}

// Note: Functions are now exported, no vtable needed

ArcReader *arc_tar_open(ArcStream *stream) {
    if (!stream) {
        return NULL;
    }
    TarReader *tar = calloc(1, sizeof(TarReader));
    if (!tar) {
        return NULL;
    }
    
    tar->base.format = ARC_FORMAT_TAR;
    tar->base.stream = stream;
    tar->base.owned_stream = NULL;
    tar->base.limits = NULL;
    tar->entry_valid = false;
    tar->eof = false;
    
    // Cast to ArcReader (they're the same structure)
    ArcReader *reader = (ArcReader *)tar;
    
    // Format detection already verified this is a TAR file and reset the stream position.
    // For filter streams (gzip/bzip2), we can't seek, so we trust format detection.
    // Just ensure we start reading from the beginning - tar_read_entry will handle the first read.
    // No need to verify again here since format detection already did it.
    
    return reader;
}
