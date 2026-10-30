#define _POSIX_C_SOURCE 200809L
#include "arc_tar.h"
#include "arc_base.h"
#include "arc_reader.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK 512

typedef struct Extent {
    uint64_t offset, length, packed;
} Extent;

enum PaxKey {
    PATH,
    LINK,
    SIZE,
    UID,
    GID,
    MTIME,
    MODE,
    SP_SIZE,
    SP_REAL,
    SP_NAME,
    SP_COUNT,
    SP_MAP,
    SP_OFFSET,
    SP_LENGTH,
    SP_MAJOR,
    SP_MINOR,
    KEY_COUNT
};

static const char *const pax_names[] = {"path",
                                        "linkpath",
                                        "size",
                                        "uid",
                                        "gid",
                                        "mtime",
                                        "mode",
                                        "GNU.sparse.size",
                                        "GNU.sparse.realsize",
                                        "GNU.sparse.name",
                                        "GNU.sparse.numblocks",
                                        "GNU.sparse.map",
                                        "GNU.sparse.offset",
                                        "GNU.sparse.numbytes",
                                        "GNU.sparse.major",
                                        "GNU.sparse.minor"};

typedef struct PaxItem {
    unsigned key;
    char *value;
} PaxItem;

typedef struct Pax {
    PaxItem *items;
    size_t count, capacity;
} Pax;

typedef struct TarReader {
    ArcReaderBase base;
    ArcEntry current;
    Pax global;
    Extent *map;
    size_t map_count, map_capacity;
    uint64_t real_size;
    int64_t data_start, entry_end;
    bool sparse, entry_valid, eof;
} TarReader;

static int invalid(void) {
    errno = EINVAL;
    return -1;
}

static int overflow(void) {
    errno = EOVERFLOW;
    return -1;
}

static const ArcLimits *limits(TarReader *r) {
    return r->base.limits ? r->base.limits : arc_default_limits();
}

/* Parse all digits, rejecting overflow and ignored suffixes. */
static int decimal(const char *text, unsigned base, uint64_t *out) {
    if (!text || !*text)
        return invalid();
    uint64_t value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p < '0' || *p > '9' || (unsigned)(*p - '0') >= base)
            return invalid();
        unsigned digit = (unsigned)(*p - '0');
        if (value > (UINT64_MAX - digit) / base)
            return overflow();
        value = value * base + digit;
    }
    *out = value;
    return 0;
}

static int header_number(const uint8_t *field, size_t size, bool negative, int64_t *out) {
    if (field[0] & 128) {
        int64_t value = field[0] & 127;
        if (field[0] & 64)
            value -= 128;
        for (size_t i = 1; i < size; i++) {
            if (value < (INT64_MIN / 256) || value > (INT64_MAX - field[i]) / 256)
                return overflow();
            value = value * 256 + field[i];
        }
        if (value < 0 && !negative)
            return invalid();
        *out = value;
        return 0;
    }
    size_t i = 0;
    while (i < size && (field[i] == ' ' || field[i] == 0))
        i++;
    uint64_t value = 0;
    for (; i < size && field[i] != ' ' && field[i] != 0; i++) {
        if (field[i] < '0' || field[i] > '7')
            return invalid();
        unsigned digit = field[i] - '0';
        if (value > ((uint64_t)INT64_MAX - digit) / 8)
            return overflow();
        value = value * 8 + digit;
    }
    for (; i < size; i++)
        if (field[i] != ' ' && field[i] != 0)
            return invalid();
    *out = (int64_t)value;
    return 0;
}

static int pax_time(const char *text, int64_t *out) {
    bool negative = *text == '-';
    if (negative)
        text++;
    if (*text < '0' || *text > '9')
        return invalid();
    uint64_t value = 0, maximum = (uint64_t)INT64_MAX + (negative ? 1U : 0U);
    while (*text >= '0' && *text <= '9') {
        unsigned digit = (unsigned)(*text++ - '0');
        if (value > (maximum - digit) / 10)
            return overflow();
        value = value * 10 + digit;
    }
    bool fraction = false;
    if (*text == '.') {
        text++;
        if (*text < '0' || *text > '9')
            return invalid();
        while (*text >= '0' && *text <= '9')
            fraction |= *text++ != '0';
    }
    if (*text)
        return invalid();
    if (negative) {
        if (value == (uint64_t)INT64_MAX + 1) {
            if (fraction)
                return overflow();
            *out = INT64_MIN;
        } else
            *out = -(int64_t)value - (fraction ? 1 : 0);
    } else
        *out = (int64_t)value;
    return 0;
}

static bool zero_block(const uint8_t *header) {
    for (size_t i = 0; i < BLOCK; i++)
        if (header[i])
            return false;
    return true;
}

static int check_header(const uint8_t *header) {
    int64_t stored;
    if (header_number(header + 148, 8, false, &stored))
        return -1;
    uint64_t sum = 0;
    int64_t signed_sum = 0;
    for (size_t i = 0; i < BLOCK; i++) {
        if (i >= 148 && i < 156) {
            sum += ' ';
            signed_sum += ' ';
        } else {
            sum += header[i];
            signed_sum += (int8_t)header[i];
        }
    }
    return stored == (int64_t)sum || stored == signed_sum ? 0 : invalid();
}

bool arc_tar_header_valid(const uint8_t *header) {
    return check_header(header) == 0;
}

static int metadata_add(TarReader *reader, uint64_t *used, uint64_t bytes) {
    if (*used > limits(reader)->max_extra || bytes > limits(reader)->max_extra - *used)
        return overflow();
    *used += bytes;
    return 0;
}

static void pax_clear(Pax *pax) {
    for (size_t i = 0; i < pax->count; i++)
        free(pax->items[i].value);
    free(pax->items);
    memset(pax, 0, sizeof(*pax));
}

static int pax_key(const char *key, size_t length) {
    for (unsigned i = 0; i < KEY_COUNT; i++)
        if (strlen(pax_names[i]) == length && !memcmp(key, pax_names[i], length))
            return (int)i;
    return -1;
}

static int pax_set(Pax *pax, unsigned key, const char *value, size_t length, bool global) {
    if (global && key >= SP_SIZE) {
        errno = ENOSYS;
        return -1;
    }
    bool repeated = key == SP_OFFSET || key == SP_LENGTH;
    size_t index = pax->count;
    if (!repeated)
        for (size_t i = 0; i < pax->count; i++)
            if (pax->items[i].key == key) {
                index = i;
                break;
            }
    if (global && !length) {
        if (index < pax->count) {
            free(pax->items[index].value);
            memmove(pax->items + index, pax->items + index + 1,
                    (pax->count - index - 1) * sizeof(*pax->items));
            pax->count--;
        }
        return 0;
    }
    char *copy = strndup(value, length);
    if (!copy)
        return -1;
    if (index == pax->count) {
        if (pax->count == pax->capacity) {
            size_t capacity = pax->capacity ? pax->capacity * 2 : 16;
            if (capacity < pax->capacity || capacity > SIZE_MAX / sizeof(*pax->items)) {
                free(copy);
                return overflow();
            }
            PaxItem *items = realloc(pax->items, capacity * sizeof(*items));
            if (!items) {
                free(copy);
                return -1;
            }
            pax->items = items;
            pax->capacity = capacity;
        }
        pax->count++;
    } else
        free(pax->items[index].value);
    pax->items[index] = (PaxItem){key, copy};
    return 0;
}

static const char *pax_get(const Pax *pax, unsigned key) {
    for (size_t i = pax->count; i; i--)
        if (pax->items[i - 1].key == key)
            return pax->items[i - 1].value;
    return NULL;
}

static const char *resolve(const Pax *local, const Pax *global, unsigned key) {
    const char *value = pax_get(local, key);
    return value ? value : pax_get(global, key);
}

static int pax_read(TarReader *reader, Pax *pax, uint64_t size, bool global) {
    if (size > limits(reader)->max_extra || size > SIZE_MAX - 1)
        return overflow();
    char *buffer = malloc((size_t)size + 1);
    if (!buffer)
        return -1;
    int result = -1;
    if (arc_stream_read_fully(reader->base.stream, buffer, (size_t)size) != (ssize_t)size)
        goto done;
    buffer[size] = 0;
    if (memchr(buffer, 0, (size_t)size)) {
        invalid();
        goto done;
    }
    size_t position = 0;
    while (position < size) {
        size_t digits = 0, length = 0;
        while (digits < size - position && buffer[position + digits] >= '0' &&
               buffer[position + digits] <= '9') {
            unsigned digit = (unsigned)(buffer[position + digits] - '0');
            if (length > (SIZE_MAX - digit) / 10) {
                overflow();
                goto done;
            }
            length = length * 10 + digit;
            digits++;
        }
        if (!digits || digits >= size - position || buffer[position + digits] != ' ' ||
            length > size - position || length <= digits + 3 ||
            buffer[position + length - 1] != '\n') {
            invalid();
            goto done;
        }
        const char *key = buffer + position + digits + 1;
        size_t payload = length - digits - 2;
        const char *equal = memchr(key, '=', payload);
        if (!equal || equal == key) {
            invalid();
            goto done;
        }
        int kind = pax_key(key, (size_t)(equal - key));
        if (kind >= 0 &&
            pax_set(pax, (unsigned)kind, equal + 1, payload - (size_t)(equal - key) - 1, global))
            goto done;
        position += length;
    }
    result = 0;
done:
    free(buffer);
    return result;
}

static int seek_position(ArcStream *stream, int64_t target) {
    int64_t current = arc_stream_tell(stream);
    if (current < 0 || target < 0)
        return invalid();
    if (!arc_stream_seek(stream, 0, SEEK_END)) {
        int64_t end = arc_stream_tell(stream);
        if (end < target) {
            arc_stream_seek(stream, current, SEEK_SET);
            errno = EIO;
            return -1;
        }
        return arc_stream_seek(stream, target, SEEK_SET);
    }
    if (current > target) {
        errno = ESPIPE;
        return -1;
    }
    uint8_t discard[8192];
    while (current < target) {
        size_t count = (uint64_t)(target - current) > sizeof(discard) ? sizeof(discard)
                                                                      : (size_t)(target - current);
        if (arc_stream_read_fully(stream, discard, count) != (ssize_t)count) {
            if (!errno)
                errno = EIO;
            return -1;
        }
        current += (int64_t)count;
    }
    return 0;
}

static int skip_padding(TarReader *reader, uint64_t size) {
    int64_t current = arc_stream_tell(reader->base.stream);
    uint64_t padding = (BLOCK - size % BLOCK) % BLOCK;
    if (current < 0 || padding > (uint64_t)(INT64_MAX - current))
        return overflow();
    return seek_position(reader->base.stream, current + (int64_t)padding);
}

static char *long_text(TarReader *reader, uint64_t size) {
    if ((size > limits(reader)->max_name && size - limits(reader)->max_name > 1) ||
        size > SIZE_MAX - 1) {
        overflow();
        return NULL;
    }
    char *text = malloc((size_t)size + 1);
    if (!text)
        return NULL;
    if (arc_stream_read_fully(reader->base.stream, text, (size_t)size) != (ssize_t)size) {
        free(text);
        return NULL;
    }
    text[size] = 0;
    size_t length = strnlen(text, (size_t)size);
    for (size_t i = length; i < size; i++)
        if (text[i]) {
            free(text);
            invalid();
            return NULL;
        }
    if (length > limits(reader)->max_name) {
        free(text);
        overflow();
        return NULL;
    }
    return text;
}

static int map_add(TarReader *reader, uint64_t offset, uint64_t length) {
    if (reader->map_count > limits(reader)->max_extra / 4 + 4)
        return overflow();
    if (reader->map_count == reader->map_capacity) {
        size_t capacity = reader->map_capacity ? reader->map_capacity * 2 : 16;
        if (capacity < reader->map_capacity || capacity > SIZE_MAX / sizeof(*reader->map))
            return overflow();
        Extent *map = realloc(reader->map, capacity * sizeof(*map));
        if (!map)
            return -1;
        reader->map = map;
        reader->map_capacity = capacity;
    }
    reader->map[reader->map_count++] = (Extent){offset, length, 0};
    return 0;
}

static int old_map_block(TarReader *reader, const uint8_t *map, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        int64_t offset, length;
        if (header_number(map + i * 24, 12, false, &offset) ||
            header_number(map + i * 24 + 12, 12, false, &length))
            return -1;
        if ((offset || length) && map_add(reader, (uint64_t)offset, (uint64_t)length))
            return -1;
    }
    return 0;
}

static int old_sparse(TarReader *reader, const uint8_t *header, uint64_t *metadata) {
    int64_t real;
    if (header_number(header + 483, 12, false, &real) || old_map_block(reader, header + 386, 4))
        return -1;
    reader->real_size = (uint64_t)real;
    int extended = header[482] == '0' ? 0 : header[482];
    while (extended) {
        if (extended != 1 && extended != '1')
            return invalid();
        if (metadata_add(reader, metadata, BLOCK))
            return -1;
        uint8_t block[BLOCK];
        if (arc_stream_read_fully(reader->base.stream, block, BLOCK) != BLOCK ||
            old_map_block(reader, block, 21))
            return -1;
        extended = block[504] == '0' ? 0 : block[504];
    }
    return 0;
}

static int map_number(const char **text, char delimiter, uint64_t *value) {
    const char *begin = *text;
    uint64_t result = 0;
    while (**text >= '0' && **text <= '9') {
        unsigned digit = (unsigned)(*(*text)++ - '0');
        if (result > (UINT64_MAX - digit) / 10)
            return overflow();
        result = result * 10 + digit;
    }
    if (*text == begin || **text != delimiter)
        return invalid();
    if (delimiter)
        (*text)++;
    *value = result;
    return 0;
}

static int line_number(TarReader *reader, uint64_t physical, uint64_t *used, uint64_t *value) {
    uint64_t result = 0;
    unsigned digits = 0;
    for (;;) {
        if (*used >= physical || *used >= limits(reader)->max_extra)
            return overflow();
        uint8_t ch;
        if (arc_stream_read_fully(reader->base.stream, &ch, 1) != 1)
            return -1;
        (*used)++;
        if (ch == '\n') {
            if (!digits)
                return invalid();
            *value = result;
            return 0;
        }
        if (ch < '0' || ch > '9')
            return invalid();
        unsigned digit = ch - '0';
        if (result > (UINT64_MAX - digit) / 10)
            return overflow();
        result = result * 10 + digit;
        digits++;
    }
}

static int pax_sparse(TarReader *reader, const Pax *local, uint64_t physical, uint64_t *map_bytes,
                      uint64_t *metadata) {
    const char *real = pax_get(local, SP_REAL);
    if (!real || !*real)
        real = pax_get(local, SP_SIZE);
    if (decimal(real, 10, &reader->real_size))
        return -1;
    const char *major = pax_get(local, SP_MAJOR), *minor = pax_get(local, SP_MINOR),
               *map = pax_get(local, SP_MAP);
    uint64_t version = 0, revision = 0;
    if (major && decimal(major, 10, &version))
        return -1;
    if (minor && decimal(minor, 10, &revision))
        return -1;
    if (version > 1 || (version == 1 && revision) || (!version && revision > 1)) {
        errno = ENOSYS;
        return -1;
    }
    if (version == 1) {
        if (map || pax_get(local, SP_OFFSET) || pax_get(local, SP_LENGTH))
            return invalid();
        uint64_t count, used = 0;
        if (line_number(reader, physical, &used, &count))
            return -1;
        if (count > limits(reader)->max_extra / 4 + 1)
            return overflow();
        for (uint64_t i = 0; i < count; i++) {
            uint64_t offset, length;
            if (line_number(reader, physical, &used, &offset) ||
                line_number(reader, physical, &used, &length) || map_add(reader, offset, length))
                return -1;
        }
        uint64_t padding = (BLOCK - used % BLOCK) % BLOCK;
        if (used > physical || padding > physical - used ||
            metadata_add(reader, metadata, used + padding))
            return overflow();
        for (uint64_t i = 0; i < padding; i++) {
            uint8_t ch;
            if (arc_stream_read_fully(reader->base.stream, &ch, 1) != 1 || ch)
                return invalid();
        }
        *map_bytes = used + padding;
    } else if (map) {
        if (pax_get(local, SP_OFFSET) || pax_get(local, SP_LENGTH))
            return invalid();
        const char *p = map;
        while (*p) {
            uint64_t offset, length;
            if (map_number(&p, ',', &offset))
                return -1;
            const char *end = strchr(p, ',');
            if (map_number(&p, end ? ',' : 0, &length) || (end && !*p) ||
                map_add(reader, offset, length))
                return invalid();
        }
    } else {
        bool pending = false;
        uint64_t offset = 0;
        for (size_t i = 0; i < local->count; i++) {
            PaxItem item = local->items[i];
            if (item.key == SP_OFFSET) {
                if (pending || decimal(item.value, 10, &offset))
                    return invalid();
                pending = true;
            } else if (item.key == SP_LENGTH) {
                uint64_t length;
                if (!pending || decimal(item.value, 10, &length) || map_add(reader, offset, length))
                    return invalid();
                pending = false;
            }
        }
        if (pending)
            return invalid();
    }
    const char *blocks = pax_get(local, SP_COUNT);
    uint64_t count;
    if (blocks && (decimal(blocks, 10, &count) || count != reader->map_count))
        return invalid();
    return 0;
}

static int validate_map(TarReader *reader, uint64_t packed) {
    uint64_t end = 0, total = 0;
    for (size_t i = 0; i < reader->map_count; i++) {
        Extent *extent = &reader->map[i];
        if (extent->offset < end || extent->offset > reader->real_size ||
            extent->length > reader->real_size - extent->offset || extent->length > packed - total)
            return invalid();
        extent->packed = total;
        total += extent->length;
        end = extent->offset + extent->length;
    }
    return total == packed ? 0 : invalid();
}

static char *header_path(const uint8_t *header) {
    size_t name = strnlen((const char *)header, 100);
    size_t prefix =
        !memcmp(header + 257, "ustar\0", 6) ? strnlen((const char *)header + 345, 155) : 0;
    char *path = malloc(name + prefix + 2);
    if (!path)
        return NULL;
    size_t offset = 0;
    if (prefix) {
        memcpy(path, header + 345, prefix);
        path[prefix] = '/';
        offset = prefix + 1;
    }
    memcpy(path + offset, header, name);
    path[offset + name] = 0;
    return path;
}

static int read_entry(TarReader *reader) {
    Pax local = {0};
    char *long_name = NULL, *long_link = NULL;
    uint64_t metadata = 0;
    bool pending_member = false;
    uint8_t header[BLOCK];
    int result = -1;
    for (;;) {
        ssize_t count = arc_stream_read_fully(reader->base.stream, header, BLOCK);
        if (!count || (count == BLOCK && zero_block(header))) {
            if (pending_member) {
                invalid();
                goto done;
            }
            reader->eof = true;
            result = 1;
            goto done;
        }
        if (count != BLOCK || check_header(header))
            goto done;
        unsigned type = header[156];
        if (type != 'g' && type != 'x' && type != 'L' && type != 'K')
            break;
        if (type != 'g')
            pending_member = true;
        int64_t length;
        if (header_number(header + 124, 12, false, &length) ||
            metadata_add(reader, &metadata, BLOCK + (uint64_t)length))
            goto done;
        if (type == 'g' || type == 'x') {
            if (pax_read(reader, type == 'g' ? &reader->global : &local, (uint64_t)length,
                         type == 'g'))
                goto done;
        } else {
            char **target = type == 'L' ? &long_name : &long_link;
            char *text = long_text(reader, (uint64_t)length);
            if (!text)
                goto done;
            free(*target);
            *target = text;
        }
        if (skip_padding(reader, (uint64_t)length))
            goto done;
    }
    int64_t size, mode, uid, gid, mtime;
    if (header_number(header + 124, 12, false, &size) ||
        header_number(header + 100, 8, false, &mode) ||
        header_number(header + 108, 8, false, &uid) ||
        header_number(header + 116, 8, false, &gid) ||
        header_number(header + 136, 12, true, &mtime))
        goto done;
    uint64_t physical = (uint64_t)size;
    const char *value = resolve(&local, &reader->global, SIZE);
    if (value && *value && decimal(value, 10, &physical))
        goto done;
    uint64_t u = (uint64_t)uid, g = (uint64_t)gid, m = (uint64_t)mode;
    value = resolve(&local, &reader->global, UID);
    if (value && *value && decimal(value, 10, &u))
        goto done;
    value = resolve(&local, &reader->global, GID);
    if (value && *value && decimal(value, 10, &g))
        goto done;
    value = resolve(&local, &reader->global, MODE);
    if (value && *value && decimal(value, 8, &m))
        goto done;
    value = resolve(&local, &reader->global, MTIME);
    if (value && *value && pax_time(value, &mtime))
        goto done;
    if (u > UINT32_MAX || g > UINT32_MAX || m > UINT32_MAX || physical > INT64_MAX) {
        overflow();
        goto done;
    }
    reader->real_size = physical;
    reader->sparse = header[156] == 'S';
    bool pax_sparse_fields = false;
    for (size_t i = 0; i < local.count; i++)
        if (local.items[i].key >= SP_SIZE)
            pax_sparse_fields = true;
    if (reader->sparse && pax_sparse_fields) {
        invalid();
        goto done;
    }
    if (reader->sparse && old_sparse(reader, header, &metadata))
        goto done;
    reader->data_start = arc_stream_tell(reader->base.stream);
    uint64_t padding = (BLOCK - physical % BLOCK) % BLOCK;
    if (reader->data_start < 0 || physical > (uint64_t)(INT64_MAX - reader->data_start) ||
        padding > (uint64_t)(INT64_MAX - reader->data_start) - physical) {
        overflow();
        goto done;
    }
    reader->entry_end = reader->data_start + (int64_t)(physical + padding);
    uint64_t map_bytes = 0;
    if (pax_sparse_fields) {
        reader->sparse = true;
        if (pax_sparse(reader, &local, physical, &map_bytes, &metadata))
            goto done;
    }
    if (reader->real_size > limits(reader)->max_uncompressed_bytes) {
        overflow();
        goto done;
    }
    if (reader->sparse && (map_bytes > physical || validate_map(reader, physical - map_bytes)))
        goto done;
    reader->data_start += (int64_t)map_bytes;
    const char *path = resolve(&local, &reader->global, PATH);
    if (!path || !*path)
        path = long_name;
    const char *sparse_name = pax_get(&local, SP_NAME);
    if (!path && sparse_name && *sparse_name)
        path = sparse_name;
    reader->current.path = path ? strdup(path) : header_path(header);
    if (!reader->current.path || !*reader->current.path ||
        strlen(reader->current.path) > limits(reader)->max_name) {
        invalid();
        goto done;
    }
    reader->current.size = reader->real_size;
    reader->current.mode = (uint32_t)m;
    reader->current.uid = (uint32_t)u;
    reader->current.gid = (uint32_t)g;
    reader->current.mtime = (uint64_t)mtime;
    switch (header[156]) {
        case 0:
        case '0':
        case '7':
        case 'S':
            reader->current.type = ARC_ENTRY_FILE;
            break;
        case '5':
            reader->current.type = ARC_ENTRY_DIR;
            break;
        case '1':
            reader->current.type = ARC_ENTRY_HARDLINK;
            break;
        case '2':
            reader->current.type = ARC_ENTRY_SYMLINK;
            break;
        default:
            reader->current.type = ARC_ENTRY_OTHER;
            break;
    }
    if (reader->current.type == ARC_ENTRY_HARDLINK || reader->current.type == ARC_ENTRY_SYMLINK) {
        const char *link = resolve(&local, &reader->global, LINK);
        if (!link || !*link)
            link = long_link;
        reader->current.link_target =
            link ? strdup(link) : strndup((const char *)header + 157, 100);
        if (!reader->current.link_target ||
            strlen(reader->current.link_target) > limits(reader)->max_name) {
            invalid();
            goto done;
        }
    }
    reader->entry_valid = true;
    result = 0;
done:
    pax_clear(&local);
    free(long_name);
    free(long_link);
    return result;
}

typedef struct SparseStream {
    ArcStream *parent;
    Extent *map;
    size_t count;
    uint64_t size, position;
    int64_t data_start;
} SparseStream;

static ssize_t sparse_read(ArcStream *stream, void *buffer, size_t size) {
    SparseStream *s = stream->user_data;
    if ((uint64_t)size > s->size - s->position)
        size = (size_t)(s->size - s->position);
    size_t total = 0;
    while (total < size) {
        size_t lo = 0, hi = s->count;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (s->map[mid].offset + s->map[mid].length <= s->position)
                lo = mid + 1;
            else
                hi = mid;
        }
        const Extent *extent = lo < s->count ? s->map + lo : NULL;
        uint64_t stop = extent ? (s->position < extent->offset ? extent->offset
                                                               : extent->offset + extent->length)
                               : s->size;
        size_t count =
            stop - s->position > size - total ? size - total : (size_t)(stop - s->position);
        if (extent && s->position >= extent->offset) {
            uint64_t packed = extent->packed + s->position - extent->offset;
            if (packed > (uint64_t)(INT64_MAX - s->data_start) ||
                seek_position(s->parent, s->data_start + (int64_t)packed) ||
                arc_stream_read_fully(s->parent, (uint8_t *)buffer + total, count) !=
                    (ssize_t)count)
                return -1;
        } else
            memset((uint8_t *)buffer + total, 0, count);
        total += count;
        s->position += count;
    }
    stream->bytes_read += (int64_t)total;
    return (ssize_t)total;
}

static int sparse_seek(ArcStream *stream, int64_t offset, int whence) {
    SparseStream *s = stream->user_data;
    uint64_t base;
    if (whence == SEEK_SET)
        base = 0;
    else if (whence == SEEK_CUR)
        base = s->position;
    else if (whence == SEEK_END)
        base = s->size;
    else
        return invalid();
    if ((offset < 0 && (uint64_t)(-(offset + 1)) + 1 > base) ||
        (offset >= 0 && (uint64_t)offset > s->size - base))
        return invalid();
    s->position = offset < 0 ? base - ((uint64_t)(-(offset + 1)) + 1) : base + (uint64_t)offset;
    stream->bytes_read = 0;
    return 0;
}

static int64_t sparse_tell(ArcStream *stream) {
    return (int64_t)((SparseStream *)stream->user_data)->position;
}

static void sparse_close(ArcStream *stream) {
    SparseStream *s = stream->user_data;
    free(s->map);
    free(s);
    free(stream);
}

int arc_tar_next(ArcReader *reader, ArcEntry *entry) {
    if (!reader || !entry)
        return invalid();
    TarReader *tar = (TarReader *)reader;
    if (tar->entry_valid && arc_tar_skip_data(reader))
        return -1;
    arc_entry_free(&tar->current);
    free(tar->map);
    tar->map = NULL;
    tar->map_count = tar->map_capacity = 0;
    if (tar->eof)
        return 1;
    int result = read_entry(tar);
    if (!result) {
        *entry = tar->current;
        memset(&tar->current, 0, sizeof(tar->current));
    }
    return result;
}

ArcStream *arc_tar_open_data(ArcReader *reader) {
    if (!reader)
        return NULL;
    TarReader *tar = (TarReader *)reader;
    if (!tar->entry_valid)
        return NULL;
    if (!tar->sparse)
        return arc_stream_substream(tar->base.stream, tar->data_start, (int64_t)tar->real_size);
    static const struct ArcStreamVtable table = {sparse_read, sparse_seek, sparse_tell,
                                                 sparse_close};
    SparseStream *s = calloc(1, sizeof(*s));
    ArcStream *stream = calloc(1, sizeof(*stream));
    if (!s || !stream) {
        free(s);
        free(stream);
        return NULL;
    }
    if (tar->map_count) {
        s->map = malloc(tar->map_count * sizeof(*s->map));
        if (!s->map) {
            free(s);
            free(stream);
            return NULL;
        }
        memcpy(s->map, tar->map, tar->map_count * sizeof(*s->map));
    }
    s->parent = tar->base.stream;
    s->count = tar->map_count;
    s->size = tar->real_size;
    s->data_start = tar->data_start;
    stream->vtable = &table;
    stream->user_data = s;
    return stream;
}

int arc_tar_skip_data(ArcReader *reader) {
    if (!reader)
        return invalid();
    TarReader *tar = (TarReader *)reader;
    if (!tar->entry_valid)
        return invalid();
    if (seek_position(tar->base.stream, tar->entry_end))
        return -1;
    tar->entry_valid = false;
    return 0;
}

void arc_tar_close(ArcReader *reader) {
    if (!reader)
        return;
    TarReader *tar = (TarReader *)reader;
    arc_entry_free(&tar->current);
    pax_clear(&tar->global);
    free(tar->map);
    arc_stream_close(tar->base.stream);
    if (tar->base.owned_stream && tar->base.owned_stream != tar->base.stream)
        arc_stream_close(tar->base.owned_stream);
    free(tar);
}

ArcReader *arc_tar_open(ArcStream *stream) {
    if (!stream)
        return NULL;
    TarReader *reader = calloc(1, sizeof(*reader));
    if (!reader)
        return NULL;
    reader->base.format = 0;
    reader->base.stream = stream;
    arc_base_set_limits(&reader->base, NULL);
    return (ArcReader *)reader;
}
