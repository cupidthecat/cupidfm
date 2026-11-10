#include "ebml.h"
#include <stdint.h>
#include <string.h>

typedef unsigned char byte;

typedef struct {
    uint32_t id;
    size_t data;
    uint64_t end;
    int unknown, complete;
} element;

typedef struct {
    unsigned maxsize, webm, video, audio, other, tracks;
    uint64_t numbers[128];
    size_t count;
} profile;

static int fail(cm_result *r, unsigned flag, const char *why) {
    r->flags |= flag;
    r->reason = why;
    return 0;
}

/* Identifier marker bits are retained; size marker bits are removed. */
static int vint(const byte *p, size_t n, size_t *off, uint64_t bound, unsigned max, int id,
                uint64_t *value, unsigned *width, cm_result *r) {
    byte marker = 128;
    unsigned k = 1, i;
    uint64_t data;
    size_t available = bound < n ? (size_t)bound : n;
    if (*off >= available)
        return fail(r, bound <= n ? CM_MALFORMED : CM_TRUNCATED, "EBML VINT is incomplete");
    if (!p[*off])
        return fail(r, CM_MALFORMED, "EBML VINT has no length marker");
    while (!(p[*off] & marker)) {
        marker >>= 1;
        ++k;
    }
    if (k > max)
        return fail(r, CM_MALFORMED, "EBML VINT exceeds supported or declared width");
    if (k > available - *off)
        return fail(r, bound <= n ? CM_MALFORMED : CM_TRUNCATED, "EBML VINT crosses boundary");
    data = p[*off] & (marker - 1u);
    for (i = 1; i < k; ++i)
        data = (data << 8) | p[*off + i];
    if (id) {
        if (data == ((UINT64_C(1) << (7 * k)) - 1) ||
            (k > 1 && data < ((UINT64_C(1) << (7 * (k - 1))) - 1)))
            return fail(r, CM_MALFORMED, "Reserved or nonminimal EBML identifier");
        data |= (uint64_t)marker << (8 * (k - 1));
    }
    *value = data;
    *width = k;
    *off += k;
    return 1;
}

static int read_element(const byte *p, size_t n, size_t off, uint64_t bound, unsigned maxsize,
                        element *e, cm_result *r) {
    uint64_t id, size;
    unsigned width;
    if (!vint(p, n, &off, bound, 4, 1, &id, &width, r) ||
        !vint(p, n, &off, bound, maxsize, 0, &size, &width, r))
        return 0;
    e->id = (uint32_t)id;
    e->data = off;
    e->unknown = size == ((UINT64_C(1) << (7 * width)) - 1);
    if (!e->unknown && size > bound - off)
        return fail(r, CM_MALFORMED, "EBML element exceeds its parent extent");
    e->end = e->unknown ? bound : (uint64_t)off + size;
    e->complete = !e->unknown && e->end <= n;
    if (!e->unknown && !e->complete)
        (void)fail(r, CM_TRUNCATED, "EBML element exceeds inspection window");
    return 1;
}

static int integer(const byte *p, const element *e, uint64_t *v, cm_result *r) {
    size_t i;
    if (e->unknown || !e->complete || e->end - e->data > 8)
        return fail(r, CM_MALFORMED, "Invalid EBML unsigned-integer extent");
    *v = 0;
    for (i = e->data; i < (size_t)e->end; ++i)
        *v = (*v << 8) | p[i];
    return 1;
}

/* Accept normal null padding, but do not hide nonzero data behind a terminator. */
static int text(const byte *p, const element *e, size_t *len, cm_result *r) {
    size_t i, end = (size_t)e->end;
    *len = 0;
    for (i = e->data; i < end; ++i) {
        if (!p[i])
            break;
        if (p[i] < 32 || p[i] > 126)
            return fail(r, CM_MALFORMED, "Invalid EBML ASCII metadata");
        ++*len;
    }
    for (; i < end; ++i)
        if (p[i])
            return fail(r, CM_AMBIGUOUS, "Nonzero data after EBML string terminator");
    return 1;
}

static int header(const byte *p, size_t n, element *root, profile *f, cm_result *r) {
    size_t off = root->data;
    unsigned seen = 0, doctype = 0;
    uint64_t version = 1, read = 1, maxid = 4, maxsize = 8, docversion = 1, docread = 1;
    while (off < root->end) {
        element e;
        unsigned bit = 0;
        uint64_t *target = NULL;
        if (!read_element(p, n, off, root->end, 8, &e, r))
            return 0;
        if (e.unknown)
            return fail(r, CM_MALFORMED, "Unknown-sized EBML header child");
        switch (e.id) {
            case 0x4286:
                bit = 1;
                target = &version;
                break;
            case 0x42f7:
                bit = 2;
                target = &read;
                break;
            case 0x42f2:
                bit = 4;
                target = &maxid;
                break;
            case 0x42f3:
                bit = 8;
                target = &maxsize;
                break;
            case 0x4287:
                bit = 16;
                target = &docversion;
                break;
            case 0x4285:
                bit = 32;
                target = &docread;
                break;
            case 0x4282:
                bit = 64;
                break;
            default:
                break;
        }
        if (bit && (seen & bit))
            return fail(r, CM_MALFORMED, "Duplicate EBML header field");
        seen |= bit;
        if (target && e.end != e.data && !integer(p, &e, target, r))
            return 0;
        if (e.id == 0x4282) {
            size_t len;
            if (!text(p, &e, &len, r))
                return 0;
            if (len == 4 && !memcmp(p + e.data, "webm", 4))
                doctype = 1;
            else if (len == 8 && !memcmp(p + e.data, "matroska", 8))
                doctype = 2;
        }
        off = (size_t)e.end;
    }
    if (!(seen & 64u) || !version || !read || read > version || !docversion || !docread ||
        docread > docversion || maxid < 4 || !maxsize)
        return fail(r, CM_MALFORMED, "Missing or contradictory EBML profile fields");
    if (!doctype || read > 1 || maxid > 4 || maxsize > 8 || docread > (doctype == 1 ? 2u : 4u))
        return fail(r, CM_AMBIGUOUS, "Unsupported EBML document type or reader profile");
    f->webm = doctype == 1;
    f->maxsize = (unsigned)maxsize;
    r->evidence = CM_STRUCTURE;
    return 1;
}

static int track(const byte *p, size_t n, const element *entry, profile *f, cm_result *r) {
    size_t off = entry->data, code = 0, length = 0, i;
    uint64_t number = 0, type = 0;
    unsigned seen = 0;
    while (off < entry->end) {
        element e;
        unsigned bit = 0;
        if (!read_element(p, n, off, entry->end, f->maxsize, &e, r))
            return 0;
        if (e.unknown)
            return fail(r, CM_AMBIGUOUS, "Unknown-sized TrackEntry child");
        if (e.id == 0xd7)
            bit = 1;
        else if (e.id == 0x83)
            bit = 2;
        else if (e.id == 0x86)
            bit = 4;
        else if (e.id == 0xe0)
            bit = 8;
        else if (e.id == 0xe1)
            bit = 16;
        if (seen & bit)
            return fail(r, CM_MALFORMED, "Duplicate track identity, role or codec");
        seen |= bit;
        if (bit == 1 && !integer(p, &e, &number, r))
            return 0;
        if (bit == 2 && !integer(p, &e, &type, r))
            return 0;
        if (bit == 4) {
            if (!text(p, &e, &length, r))
                return 0;
            code = e.data;
        }
        off = (size_t)e.end;
    }
    if ((seen & 7u) != 7 || !number || !type || !length)
        return fail(r, CM_MALFORMED, "TrackEntry lacks required identification metadata");
    if ((type == 1 && (seen & 16u)) || (type == 2 && (seen & 8u)) ||
        (length >= 2 && p[code + 1] == '_' &&
         ((type == 1 && p[code] == 'A') || (type == 2 && p[code] == 'V'))))
        return fail(r, CM_MALFORMED, "Track role contradicts codec or audio/video settings");
    if (f->count == 128)
        return fail(r, CM_AMBIGUOUS, "Track count exceeds bounded inspection policy");
    for (i = 0; i < f->count; ++i)
        if (f->numbers[i] == number)
            return fail(r, CM_MALFORMED, "Conflicting duplicate track number");
    f->numbers[f->count++] = number;
    if (type == 1) {
        if (f->webm &&
            !((length == 5 && (!memcmp(p + code, "V_VP8", 5) || !memcmp(p + code, "V_VP9", 5)))))
            return fail(r, CM_AMBIGUOUS, "Unsupported WebM video codec declaration");
        ++f->video;
    } else if (type == 2) {
        if (f->webm && !((length == 6 && !memcmp(p + code, "A_OPUS", 6)) ||
                         (length == 8 && !memcmp(p + code, "A_VORBIS", 8))))
            return fail(r, CM_AMBIGUOUS, "Unsupported WebM audio codec declaration");
        ++f->audio;
    } else
        ++f->other;
    return 1;
}

static int tracks(const byte *p, size_t n, const element *list, profile *f, cm_result *r) {
    size_t off = list->data;
    if (++f->tracks != 1)
        return fail(r, CM_MALFORMED, "Duplicate Tracks element");
    while (off < list->end) {
        element e;
        if (!read_element(p, n, off, list->end, f->maxsize, &e, r))
            return 0;
        if (e.unknown)
            return fail(r, CM_AMBIGUOUS, "Unknown-sized Tracks child");
        if (e.id == 0xae) {
            if (!track(p, n, &e, f, r))
                return 0;
        } else if (e.id != 0xec && e.id != 0xbf)
            return fail(r, CM_AMBIGUOUS, "Unsupported Tracks child");
        off = (size_t)e.end;
    }
    if (!f->count)
        return fail(r, CM_MALFORMED, "Empty Tracks element");
    return 1;
}

int cm_ebml_detect(const byte *p, size_t n, cm_result *r) {
    static const byte magic[4] = {0x1a, 0x45, 0xdf, 0xa3};
    element root, segment;
    profile f = {0};
    size_t off;
    int complete = 1;
    if (n < 4) {
        if (n >= 2 && !memcmp(p, magic, n)) {
            (void)fail(r, CM_TRUNCATED, "Incomplete EBML signature");
            return 1;
        }
        return 0;
    }
    if (memcmp(p, magic, 4))
        return 0;
    if (!read_element(p, n, 0, UINT64_MAX, 8, &root, r))
        return 1;
    if (root.unknown) {
        (void)fail(r, CM_MALFORMED, "Unknown-sized EBML header");
        return 1;
    }
    if (!root.complete || !header(p, n, &root, &f, r))
        return 1;
    off = (size_t)root.end;
    for (;;) {
        if (off == n) {
            (void)fail(r, CM_TRUNCATED | CM_AMBIGUOUS, "EBML Segment is missing");
            return 1;
        }
        if (!read_element(p, n, off, UINT64_MAX, f.maxsize, &segment, r))
            return 1;
        if (segment.id == 0x18538067)
            break;
        if ((segment.id != 0xec && segment.id != 0xbf) || !segment.complete) {
            (void)fail(r, CM_AMBIGUOUS, "Unsupported element before EBML Segment");
            return 1;
        }
        off = (size_t)segment.end;
    }
    if (segment.unknown)
        segment.end = n; /* EOF closes the supported unknown-sized Segment. */
    off = segment.data;
    while (off < segment.end && off < n) {
        element e;
        if (!read_element(p, n, off, segment.unknown ? UINT64_MAX : segment.end, f.maxsize, &e, r))
            return 1;
        if (e.id == 0x1a45dfa3 || e.id == 0x18538067) {
            (void)fail(r, CM_AMBIGUOUS, "Multiple EBML documents or segments are unresolved");
            return 1;
        }
        if (e.unknown) {
            complete = 0;
            break;
        }
        if (!e.complete) {
            complete = 0;
            break;
        }
        if (e.id == 0x1654ae6b && !tracks(p, n, &e, &f, r))
            return 1;
        off = (size_t)e.end;
    }
    if (off < segment.end || (!segment.unknown && segment.end != n))
        complete = 0;
    if (f.video) {
        r->mime = f.webm ? "video/webm" : "video/matroska";
        r->reason = "EBML document and video track metadata checked; media blocks unchecked";
        if (!complete)
            r->flags |= CM_AMBIGUOUS;
    } else if (f.audio && !f.other && complete && !(r->flags & (CM_PARTIAL | CM_TRUNCATED))) {
        r->mime = f.webm ? "audio/webm" : "audio/matroska";
        r->reason = "EBML complete Segment and audio-only Tracks checked; media blocks unchecked";
    } else
        (void)fail(r, CM_AMBIGUOUS, "EBML audio/video role is unresolved in the available layout");
    return 1;
}
