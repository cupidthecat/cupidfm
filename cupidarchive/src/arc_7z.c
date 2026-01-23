#define _POSIX_C_SOURCE 200809L
#include "arc_7z.h"
#include "arc_base.h"
#include "arc_stream.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <lzma.h>
#include <stdio.h>

// 7z signature bytes: 37 7A BC AF 27 1C
#define SEVENZ_SIG_SIZE 6
static const uint8_t SEVENZ_SIG[SEVENZ_SIG_SIZE] = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C};

// 7z header IDs
enum {
    kEnd = 0x00,
    kHeader = 0x01,
    kArchiveProperties = 0x02,
    kAdditionalStreamsInfo = 0x03,
    kMainStreamsInfo = 0x04,
    kFilesInfo = 0x05,
    kPackInfo = 0x06,
    kUnpackInfo = 0x07,
    kSubStreamsInfo = 0x08,
    kSize = 0x09,
    kCRC = 0x0A,
    kFolder = 0x0B,
    kCodersUnpackSize = 0x0C,
    kNumUnpackStream = 0x0D,
    kEmptyStream = 0x0E,
    kEmptyFile = 0x0F,
    kAnti = 0x10,
    kName = 0x11,
    kEncodedHeader = 0x17
};

// Coder method IDs
#define SEVENZ_METHOD_COPY 0x00
#define SEVENZ_METHOD_LZMA 0x030101
#define SEVENZ_METHOD_LZMA2 0x21

typedef struct SevenZFolderInfo {
    uint64_t pack_pos;
    uint64_t pack_size;
    uint64_t unpack_size;
    uint64_t coder_id;
    uint8_t *coder_props;
    size_t coder_props_size;
} SevenZFolderInfo;

typedef struct SevenZReader {
    ArcReaderBase base;
    ArcEntry current_entry;
    bool entry_valid;
    bool entry_returned;
    int64_t data_offset;
    uint64_t pack_size;
    uint64_t unpack_size;
    uint64_t coder_id;
    uint8_t *coder_props;
    size_t coder_props_size;
} SevenZReader;

typedef struct LzmaFilterData {
    ArcStream *underlying;
    lzma_stream strm;
    uint8_t *in_buf;
    size_t in_buf_size;
    bool eof;
    bool initialized;
    lzma_filter filters[2];
    lzma_options_lzma lzma_opts;
    void *opts_alloc;
} LzmaFilterData;

static uint64_t read_le64_buf(const uint8_t *data) {
    return ((uint64_t)data[0]) |
           ((uint64_t)data[1] << 8) |
           ((uint64_t)data[2] << 16) |
           ((uint64_t)data[3] << 24) |
           ((uint64_t)data[4] << 32) |
           ((uint64_t)data[5] << 40) |
           ((uint64_t)data[6] << 48) |
           ((uint64_t)data[7] << 56);
}

static ssize_t lzma_filter_read(ArcStream *stream, void *buf, size_t n) {
    LzmaFilterData *data = (LzmaFilterData *)stream->user_data;
    if (!data->initialized) {
        data->strm = (lzma_stream)LZMA_STREAM_INIT;
        if (lzma_raw_decoder(&data->strm, data->filters) != LZMA_OK) {
            return -1;
        }
        data->initialized = true;
    }
    if (data->eof) {
        return 0;
    }

    if (stream->byte_limit > 0) {
        int64_t remaining = stream->byte_limit - stream->bytes_read;
        if (remaining <= 0) {
            return 0;
        }
        if ((int64_t)n > remaining) {
            n = (size_t)remaining;
        }
    }

    data->strm.next_out = (uint8_t *)buf;
    data->strm.avail_out = n;

    while (data->strm.avail_out > 0 && !data->eof) {
        if (data->strm.avail_in == 0) {
            ssize_t in_read = arc_stream_read(data->underlying, data->in_buf, data->in_buf_size);
            if (in_read < 0) {
                return -1;
            }
            if (in_read == 0) {
                lzma_ret ret = lzma_code(&data->strm, LZMA_FINISH);
                if (ret == LZMA_STREAM_END || ret == LZMA_BUF_ERROR) {
                    data->eof = true;
                    break;
                }
                return -1;
            }
            data->strm.next_in = data->in_buf;
            data->strm.avail_in = (size_t)in_read;
        }

        lzma_ret ret = lzma_code(&data->strm, LZMA_RUN);
        if (ret == LZMA_STREAM_END) {
            data->eof = true;
            break;
        }
        if (ret != LZMA_OK && ret != LZMA_BUF_ERROR) {
            return -1;
        }
    }

    size_t out = n - data->strm.avail_out;
    stream->bytes_read += out;
    return (ssize_t)out;
}

static int lzma_filter_seek(ArcStream *stream, int64_t off, int whence) {
    (void)stream;
    (void)off;
    (void)whence;
    errno = ESPIPE;
    return -1;
}

static int64_t lzma_filter_tell(ArcStream *stream) {
    return stream->bytes_read;
}

static void lzma_filter_close(ArcStream *stream) {
    LzmaFilterData *data = (LzmaFilterData *)stream->user_data;
    if (data) {
        if (data->initialized) {
            lzma_end(&data->strm);
        }
        free(data->opts_alloc);
        free(data->in_buf);
        free(data);
    }
    free(stream);
}

static const struct ArcStreamVtable lzma_filter_vtable = {
    .read = lzma_filter_read,
    .seek = lzma_filter_seek,
    .tell = lzma_filter_tell,
    .close = lzma_filter_close
};

static int read_byte(const uint8_t *buf, size_t size, size_t *pos, uint8_t *out) {
    if (*pos >= size) {
        return -1;
    }
    *out = buf[*pos];
    (*pos)++;
    return 0;
}

static int read_bytes(const uint8_t *buf, size_t size, size_t *pos, void *out, size_t n) {
    if (*pos + n > size) {
        return -1;
    }
    memcpy(out, buf + *pos, n);
    *pos += n;
    return 0;
}

// 7z variable-length integer
static int read_7z_uint64(const uint8_t *buf, size_t size, size_t *pos, uint64_t *out) {
    uint8_t first;
    if (read_byte(buf, size, pos, &first) < 0) {
        return -1;
    }

    uint8_t mask = 0x80;
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        if ((first & mask) == 0) {
            value |= (uint64_t)(first & (mask - 1)) << (8 * i);
            *out = value;
            return 0;
        }
        uint8_t b;
        if (read_byte(buf, size, pos, &b) < 0) {
            return -1;
        }
        value |= (uint64_t)b << (8 * i);
        mask >>= 1;
    }

    *out = value;
    return 0;
}

static int skip_7z_data(const uint8_t *buf, size_t size, size_t *pos, uint64_t len) {
    (void)buf;
    if (*pos + len > size) {
        return -1;
    }
    *pos += (size_t)len;
    return 0;
}

static int read_7z_crc_list(const uint8_t *buf, size_t size, size_t *pos, uint64_t num_items) {
    uint8_t all_defined = 0;
    if (read_byte(buf, size, pos, &all_defined) < 0) {
        return -1;
    }

    uint64_t num_defined = num_items;
    if (all_defined == 0) {
        size_t bitset_bytes = (size_t)((num_items + 7) / 8);
        if (*pos + bitset_bytes > size) {
            return -1;
        }
        num_defined = 0;
        for (size_t i = 0; i < bitset_bytes; i++) {
            uint8_t bits = buf[*pos + i];
            for (int b = 0; b < 8; b++) {
                if ((i * 8 + b) >= num_items) {
                    break;
                }
                if (bits & (1u << b)) {
                    num_defined++;
                }
            }
        }
        *pos += bitset_bytes;
    }

    if (skip_7z_data(buf, size, pos, num_defined * 4) < 0) {
        return -1;
    }
    return 0;
}

static int parse_coder(const uint8_t *buf, size_t size, size_t *pos,
                       uint64_t *coder_id_out, uint8_t **props_out, size_t *props_size_out) {
    uint8_t flags;
    if (read_byte(buf, size, pos, &flags) < 0) {
        return -1;
    }

    uint8_t id_size = flags & 0x0F;
    bool is_complex = (flags & 0x10) != 0;
    bool has_props = (flags & 0x20) != 0;
    bool has_more_sizes = (flags & 0x40) != 0;

    if (is_complex || has_more_sizes || id_size == 0 || id_size > 8) {
        return -1;
    }

    uint8_t id_bytes[8] = {0};
    if (read_bytes(buf, size, pos, id_bytes, id_size) < 0) {
        return -1;
    }

    uint64_t coder_id = 0;
    for (uint8_t i = 0; i < id_size; i++) {
        coder_id |= ((uint64_t)id_bytes[i]) << (8 * i);
    }

    uint8_t *props = NULL;
    size_t props_size = 0;
    if (has_props) {
        uint64_t props_len = 0;
        if (read_7z_uint64(buf, size, pos, &props_len) < 0) {
            return -1;
        }
        if (props_len > 0) {
            props = malloc((size_t)props_len);
            if (!props) {
                return -1;
            }
            if (read_bytes(buf, size, pos, props, (size_t)props_len) < 0) {
                free(props);
                return -1;
            }
            props_size = (size_t)props_len;
        }
    }

    *coder_id_out = coder_id;
    *props_out = props;
    *props_size_out = props_size;
    return 0;
}

static int parse_streams_info(const uint8_t *buf, size_t size, size_t *pos, SevenZFolderInfo *info) {
    uint8_t id;

    // PackInfo
    if (read_byte(buf, size, pos, &id) < 0 || id != kPackInfo) {
        return -1;
    }

    if (read_7z_uint64(buf, size, pos, &info->pack_pos) < 0) {
        return -1;
    }

    uint64_t num_pack_streams = 0;
    if (read_7z_uint64(buf, size, pos, &num_pack_streams) < 0) {
        return -1;
    }
    if (num_pack_streams != 1) {
        return -1;
    }

    if (read_byte(buf, size, pos, &id) < 0 || id != kSize) {
        return -1;
    }

    if (read_7z_uint64(buf, size, pos, &info->pack_size) < 0) {
        return -1;
    }

    // Optional CRC
    if (read_byte(buf, size, pos, &id) < 0) {
        return -1;
    }
    if (id == kCRC) {
        if (read_7z_crc_list(buf, size, pos, num_pack_streams) < 0) {
            return -1;
        }
        if (read_byte(buf, size, pos, &id) < 0) {
            return -1;
        }
    }
    if (id != kEnd) {
        return -1;
    }

    // UnpackInfo
    if (read_byte(buf, size, pos, &id) < 0 || id != kUnpackInfo) {
        return -1;
    }
    if (read_byte(buf, size, pos, &id) < 0 || id != kFolder) {
        return -1;
    }

    uint64_t num_folders = 0;
    if (read_7z_uint64(buf, size, pos, &num_folders) < 0 || num_folders != 1) {
        return -1;
    }

    uint8_t external;
    if (read_byte(buf, size, pos, &external) < 0 || external != 0) {
        return -1;
    }

    uint64_t num_coders = 0;
    if (read_7z_uint64(buf, size, pos, &num_coders) < 0 || num_coders != 1) {
        return -1;
    }

    if (parse_coder(buf, size, pos, &info->coder_id, &info->coder_props, &info->coder_props_size) < 0) {
        return -1;
    }

    // No bind pairs for single coder
    if (read_byte(buf, size, pos, &id) < 0 || id != kCodersUnpackSize) {
        return -1;
    }

    if (read_7z_uint64(buf, size, pos, &info->unpack_size) < 0) {
        return -1;
    }

    // Optional CRC
    if (read_byte(buf, size, pos, &id) < 0) {
        return -1;
    }
    if (id == kCRC) {
        if (read_7z_crc_list(buf, size, pos, num_folders) < 0) {
            return -1;
        }
        if (read_byte(buf, size, pos, &id) < 0) {
            return -1;
        }
    }
    if (id != kEnd) {
        return -1;
    }

    // Optional SubStreamsInfo (skip)
    if (read_byte(buf, size, pos, &id) < 0) {
        return -1;
    }
    if (id != kSubStreamsInfo) {
        // Not present, rewind one byte
        (*pos)--;
        // Expect end of StreamsInfo
        if (read_byte(buf, size, pos, &id) < 0) {
            return -1;
        }
        if (id != kEnd) {
            return -1;
        }
    } else {
        // Skip until End
        for (;;) {
            if (read_byte(buf, size, pos, &id) < 0) {
                return -1;
            }
            if (id == kEnd) {
                break;
            }
            if (id == kNumUnpackStream || id == kSize || id == kCRC) {
                // Skip using size fields
                uint64_t n = 0;
                if (read_7z_uint64(buf, size, pos, &n) < 0) {
                    return -1;
                }
                if (id == kCRC) {
                    if (skip_7z_data(buf, size, pos, 4 * n) < 0) {
                        return -1;
                    }
                } else {
                    if (skip_7z_data(buf, size, pos, n) < 0) {
                        return -1;
                    }
                }
            } else {
                return -1;
            }
        }
    }

    return 0;
}

static char *decode_7z_name(const uint8_t *data, size_t size) {
    if (size < 2) {
        return NULL;
    }
    // UTF-16LE, null-terminated string
    size_t max_chars = size / 2;
    char *out = malloc(max_chars + 1);
    if (!out) {
        return NULL;
    }
    size_t out_len = 0;
    for (size_t i = 0; i + 1 < size; i += 2) {
        uint16_t ch = (uint16_t)data[i] | ((uint16_t)data[i + 1] << 8);
        if (ch == 0) {
            break;
        }
        out[out_len++] = (ch < 0x80) ? (char)ch : '?';
    }
    out[out_len] = '\0';
    return out;
}

static int parse_files_info(const uint8_t *buf, size_t size, size_t *pos, char **name_out, uint64_t *num_files_out) {
    uint8_t id;
    if (read_byte(buf, size, pos, &id) < 0 || id != kFilesInfo) {
        return -1;
    }

    uint64_t num_files = 0;
    if (read_7z_uint64(buf, size, pos, &num_files) < 0) {
        return -1;
    }
    *num_files_out = num_files;

    while (1) {
        if (read_byte(buf, size, pos, &id) < 0) {
            return -1;
        }
        if (id == kEnd) {
            break;
        }

        uint64_t size_prop = 0;
        if (read_7z_uint64(buf, size, pos, &size_prop) < 0) {
            return -1;
        }

        if (id == kName) {
            uint8_t external;
            if (read_byte(buf, size, pos, &external) < 0 || external != 0) {
                return -1;
            }
            size_t name_bytes = (size_t)size_prop - 1;
            if (name_bytes > 0) {
                char *name = decode_7z_name(buf + *pos, name_bytes);
                if (name) {
                    *name_out = name;
                }
            }
            if (skip_7z_data(buf, size, pos, size_prop - 1) < 0) {
                return -1;
            }
        } else {
            if (skip_7z_data(buf, size, pos, size_prop) < 0) {
                return -1;
            }
        }
    }

    return 0;
}

static int decode_header_if_needed(const uint8_t *buf, size_t size, const ArcLimits *limits,
                                   uint8_t **decoded_out, size_t *decoded_size_out,
                                   SevenZFolderInfo *folder_out) {
    size_t pos = 0;
    uint8_t id;
    if (read_byte(buf, size, &pos, &id) < 0) {
        return -1;
    }

    if (id == kHeader) {
        *decoded_out = (uint8_t *)buf;
        *decoded_size_out = size;
        return 0;
    }

    if (id != kEncodedHeader) {
        return -1;
    }

    // Parse streams info for encoded header
    if (parse_streams_info(buf, size, &pos, folder_out) < 0) {
        return -1;
    }

    uint64_t unpack_limit = limits && limits->max_uncompressed_bytes ? limits->max_uncompressed_bytes : (1024ULL * 1024ULL * 1024ULL);
    if (folder_out->unpack_size > unpack_limit) {
        return -1;
    }

    if (folder_out->pack_pos + folder_out->pack_size > size) {
        return -1;
    }

    const uint8_t *packed = buf + folder_out->pack_pos;
    size_t packed_size = (size_t)folder_out->pack_size;
    uint8_t *decoded = malloc((size_t)folder_out->unpack_size);
    if (!decoded) {
        return -1;
    }

    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_filter filters[2];
    memset(filters, 0, sizeof(filters));

    lzma_options_lzma lzma_opts;
    lzma_filter filter = {0};

    if (folder_out->coder_id == SEVENZ_METHOD_LZMA2 && folder_out->coder_props_size == 1) {
        uint8_t prop = folder_out->coder_props[0];
        if (prop > 40) {
            free(decoded);
            return -1;
        }
        uint32_t dict = 1u << (prop / 2 + 11);
        if (prop % 2) {
            dict += dict / 2;
        }
        memset(&lzma_opts, 0, sizeof(lzma_opts));
        lzma_opts.dict_size = dict;
        filter.id = LZMA_FILTER_LZMA2;
        filter.options = &lzma_opts;
        filters[0] = filter;
        filters[1].id = LZMA_VLI_UNKNOWN;

        if (lzma_raw_decoder(&strm, filters) != LZMA_OK) {
            free(decoded);
            return -1;
        }
    } else if (folder_out->coder_id == SEVENZ_METHOD_LZMA && folder_out->coder_props_size == 5) {
        filter.id = LZMA_FILTER_LZMA1;
        filter.options = &lzma_opts;
        if (lzma_properties_decode(&filter, NULL, folder_out->coder_props, folder_out->coder_props_size) != LZMA_OK) {
            free(decoded);
            return -1;
        }
        filters[0] = filter;
        filters[1].id = LZMA_VLI_UNKNOWN;

        if (lzma_raw_decoder(&strm, filters) != LZMA_OK) {
            free(decoded);
            return -1;
        }
    } else if (folder_out->coder_id == SEVENZ_METHOD_COPY) {
        if (packed_size != folder_out->unpack_size) {
            free(decoded);
            return -1;
        }
        memcpy(decoded, packed, packed_size);
        *decoded_out = decoded;
        *decoded_size_out = packed_size;
        return 0;
    } else {
        free(decoded);
        return -1;
    }

    strm.next_in = packed;
    strm.avail_in = packed_size;
    strm.next_out = decoded;
    strm.avail_out = (size_t)folder_out->unpack_size;

    lzma_ret ret = lzma_code(&strm, LZMA_FINISH);
    lzma_end(&strm);
    if (ret != LZMA_STREAM_END) {
        free(decoded);
        return -1;
    }

    *decoded_out = decoded;
    *decoded_size_out = (size_t)folder_out->unpack_size;
    return 0;
}

static int parse_7z_header(const uint8_t *header, size_t header_size, const ArcLimits *limits,
                           SevenZFolderInfo *folder, char **name_out, uint64_t *num_files_out) {
    (void)limits;
    size_t pos = 0;
    uint8_t id;
    if (read_byte(header, header_size, &pos, &id) < 0 || id != kHeader) {
        return -1;
    }

    bool have_streams = false;
    while (1) {
        if (read_byte(header, header_size, &pos, &id) < 0) {
            return -1;
        }
        if (id == kEnd) {
            break;
        }

        if (id == kMainStreamsInfo) {
            if (parse_streams_info(header, header_size, &pos, folder) < 0) {
                return -1;
            }
            have_streams = true;
        } else if (id == kFilesInfo) {
            if (parse_files_info(header, header_size, &pos, name_out, num_files_out) < 0) {
                return -1;
            }
        } else if (id == kArchiveProperties || id == kAdditionalStreamsInfo) {
            // Skip these sections
            uint8_t tmp_id;
            do {
                if (read_byte(header, header_size, &pos, &tmp_id) < 0) {
                    return -1;
                }
            } while (tmp_id != kEnd);
        } else {
            return -1;
        }
    }

    if (!have_streams) {
        return -1;
    }

    // Some minimal 7z archives omit FilesInfo; assume a single file with a default name.
    if (*num_files_out == 0) {
        *num_files_out = 1;
        if (!*name_out) {
            *name_out = strdup("file");
            if (!*name_out) {
                return -1;
            }
        }
    }

    return 0;
}

static ArcStream *create_lzma_stream(ArcStream *packed, uint64_t coder_id,
                                     const uint8_t *props, size_t props_size,
                                     int64_t out_limit) {
    if (!packed) {
        return NULL;
    }

    // Simple LZMA/LZMA2 decoder stream using liblzma

    ArcStream *stream = calloc(1, sizeof(*stream));
    if (!stream) {
        return NULL;
    }

    LzmaFilterData *data = calloc(1, sizeof(*data));
    if (!data) {
        free(stream);
        return NULL;
    }

    data->underlying = packed;
    data->in_buf_size = 64 * 1024;
    data->in_buf = malloc(data->in_buf_size);
    if (!data->in_buf) {
        free(data);
        free(stream);
        return NULL;
    }

    data->filters[0].options = NULL;
    data->filters[1].id = LZMA_VLI_UNKNOWN;

    if (coder_id == SEVENZ_METHOD_LZMA2 && props_size == 1) {
        uint8_t prop = props[0];
        if (prop > 40) {
            free(data->in_buf);
            free(data);
            free(stream);
            return NULL;
        }
        uint32_t dict = 1u << (prop / 2 + 11);
        if (prop % 2) {
            dict += dict / 2;
        }
        lzma_options_lzma *lzma2_opts = malloc(sizeof(*lzma2_opts));
        if (!lzma2_opts) {
            free(data->in_buf);
            free(data);
            free(stream);
            return NULL;
        }
        memset(lzma2_opts, 0, sizeof(*lzma2_opts));
        lzma2_opts->dict_size = dict;
        data->filters[0].id = LZMA_FILTER_LZMA2;
        data->filters[0].options = lzma2_opts;
        data->opts_alloc = lzma2_opts;
    } else if (coder_id == SEVENZ_METHOD_LZMA && props_size == 5) {
        lzma_filter filter = {0};
        filter.id = LZMA_FILTER_LZMA1;
        filter.options = &data->lzma_opts;
        if (lzma_properties_decode(&filter, NULL, props, props_size) != LZMA_OK) {
            free(data->in_buf);
            free(data);
            free(stream);
            return NULL;
        }
        data->filters[0].id = LZMA_FILTER_LZMA1;
        data->filters[0].options = &data->lzma_opts;
    } else {
        free(data->in_buf);
        free(data);
        free(stream);
        return NULL;
    }

    stream->vtable = &lzma_filter_vtable;
    stream->byte_limit = out_limit;
    stream->bytes_read = 0;
    stream->user_data = data;
    return stream;
}

static void free_folder_info(SevenZFolderInfo *info) {
    if (info && info->coder_props) {
        free(info->coder_props);
        info->coder_props = NULL;
        info->coder_props_size = 0;
    }
}

ArcReader *arc_7z_open(ArcStream *stream) {
    return arc_7z_open_ex(stream, arc_default_limits());
}

ArcReader *arc_7z_open_ex(ArcStream *stream, const ArcLimits *limits) {
    if (!stream) {
        return NULL;
    }

    // Read signature header
    uint8_t sig[SEVENZ_SIG_SIZE];
    if (arc_stream_read(stream, sig, sizeof(sig)) != (ssize_t)sizeof(sig)) {
        return NULL;
    }
    if (memcmp(sig, SEVENZ_SIG, SEVENZ_SIG_SIZE) != 0) {
        return NULL;
    }

    // Version (2), start header crc (4), next header offset (8), size (8), crc (4)
    uint8_t header_bytes[2 + 4 + 8 + 8 + 4];
    if (arc_stream_read(stream, header_bytes, sizeof(header_bytes)) != (ssize_t)sizeof(header_bytes)) {
        return NULL;
    }

    uint64_t next_header_offset = read_le64_buf(header_bytes + 6);
    uint64_t next_header_size = read_le64_buf(header_bytes + 14);

    int64_t base_offset = 32; // Signature header size
    int64_t header_pos = base_offset + (int64_t)next_header_offset;
    if (arc_stream_seek(stream, header_pos, SEEK_SET) < 0) {
        return NULL;
    }

    if (next_header_size == 0 || next_header_size > 64 * 1024 * 1024) {
        return NULL;
    }

    uint8_t *header_buf = malloc((size_t)next_header_size);
    if (!header_buf) {
        return NULL;
    }
    if (arc_stream_read(stream, header_buf, (size_t)next_header_size) != (ssize_t)next_header_size) {
        free(header_buf);
        return NULL;
    }

    SevenZFolderInfo folder = {0};
    uint8_t *decoded = NULL;
    size_t decoded_size = 0;
    if (decode_header_if_needed(header_buf, (size_t)next_header_size, limits, &decoded, &decoded_size, &folder) < 0) {
        free_folder_info(&folder);
        free(header_buf);
        return NULL;
    }

    SevenZFolderInfo main_folder = {0};
    char *name = NULL;
    uint64_t num_files = 0;
    if (parse_7z_header(decoded, decoded_size, limits, &main_folder, &name, &num_files) < 0) {
        free_folder_info(&main_folder);
        free_folder_info(&folder);
        if (decoded != header_buf) {
            free(decoded);
        }
        free(header_buf);
        return NULL;
    }
    free_folder_info(&folder);

    if (num_files != 1) {
        free_folder_info(&main_folder);
        if (decoded != header_buf) {
            free(decoded);
        }
        free(header_buf);
        errno = ENOSYS;
        return NULL;
    }

    SevenZReader *reader = calloc(1, sizeof(*reader));
    if (!reader) {
        free_folder_info(&main_folder);
        if (decoded != header_buf) {
            free(decoded);
        }
        free(header_buf);
        return NULL;
    }

    reader->base.format = 3; // ARC_FORMAT_7Z
    reader->base.stream = stream;
    reader->base.limits = limits;
    reader->entry_valid = true;
    reader->entry_returned = false;
    reader->data_offset = base_offset + (int64_t)main_folder.pack_pos;
    reader->pack_size = main_folder.pack_size;
    reader->unpack_size = main_folder.unpack_size;
    reader->coder_id = main_folder.coder_id;
    reader->coder_props = main_folder.coder_props;
    reader->coder_props_size = main_folder.coder_props_size;

    reader->current_entry.path = name ? name : strdup("file");
    reader->current_entry.size = main_folder.unpack_size;
    reader->current_entry.mode = 0644;
    reader->current_entry.mtime = 0;
    reader->current_entry.type = ARC_ENTRY_FILE;
    reader->current_entry.link_target = NULL;
    reader->current_entry.uid = 0;
    reader->current_entry.gid = 0;

    if (decoded != header_buf) {
        free(decoded);
    }
    free(header_buf);

    return (ArcReader *)reader;
}

int arc_7z_next(ArcReader *reader, ArcEntry *entry) {
    if (!reader || !entry) {
        return -1;
    }
    SevenZReader *seven = (SevenZReader *)reader;
    if (!seven->entry_valid || seven->entry_returned) {
        return 1;
    }
    memset(entry, 0, sizeof(*entry));
    entry->path = strdup(seven->current_entry.path ? seven->current_entry.path : "file");
    if (!entry->path) {
        return -1;
    }
    entry->size = seven->current_entry.size;
    entry->mode = seven->current_entry.mode;
    entry->mtime = seven->current_entry.mtime;
    entry->type = seven->current_entry.type;
    entry->link_target = NULL;
    entry->uid = seven->current_entry.uid;
    entry->gid = seven->current_entry.gid;
    seven->entry_returned = true;
    return 0;
}

ArcStream *arc_7z_open_data(ArcReader *reader) {
    if (!reader) {
        return NULL;
    }
    SevenZReader *seven = (SevenZReader *)reader;
    if (!seven->entry_valid) {
        return NULL;
    }

    if (arc_stream_seek(seven->base.stream, seven->data_offset, SEEK_SET) < 0) {
        return NULL;
    }

    ArcStream *packed = arc_stream_substream(seven->base.stream, seven->data_offset, (int64_t)seven->pack_size);
    if (!packed) {
        return NULL;
    }

    if (seven->coder_id == SEVENZ_METHOD_COPY) {
        return packed;
    }

    int64_t out_limit = (int64_t)seven->unpack_size;
    if (seven->base.limits && seven->base.limits->max_uncompressed_bytes > 0) {
        if (out_limit <= 0 || (uint64_t)out_limit > seven->base.limits->max_uncompressed_bytes) {
            out_limit = (int64_t)seven->base.limits->max_uncompressed_bytes;
        }
    }

    ArcStream *decompressed = create_lzma_stream(packed, seven->coder_id, seven->coder_props, seven->coder_props_size, out_limit);
    if (!decompressed) {
        arc_stream_close(packed);
        return NULL;
    }
    return decompressed;
}

int arc_7z_skip_data(ArcReader *reader) {
    if (!reader) {
        return -1;
    }
    SevenZReader *seven = (SevenZReader *)reader;
    seven->entry_valid = false;
    return 0;
}

void arc_7z_close(ArcReader *reader) {
    if (!reader) {
        return;
    }
    SevenZReader *seven = (SevenZReader *)reader;
    arc_entry_free(&seven->current_entry);
    if (seven->base.stream) {
        arc_stream_close(seven->base.stream);
        seven->base.stream = NULL;
    }
    if (seven->base.owned_stream) {
        arc_stream_close(seven->base.owned_stream);
        seven->base.owned_stream = NULL;
    }
    if (seven->coder_props) {
        free(seven->coder_props);
    }
    free(seven);
}

