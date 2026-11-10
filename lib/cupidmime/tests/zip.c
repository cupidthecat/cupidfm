#include "cupidmime.h"
#include "zip_fixtures.h"
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

static void le16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void le32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static cm_result detect(const void *p, size_t n) {
    cm_result r;
    CHECK(cm_detect(p, n, &r) == CM_OK);
    return r;
}

static void generic(const void *p, size_t n, unsigned flags) {
    cm_result r = detect(p, n);
    CHECK(!strcmp(r.mime, "application/zip"));
    CHECK((r.flags & flags) == flags);
    CHECK(r.flags & CM_AMBIGUOUS);
    if (!flags)
        CHECK(r.flags == CM_AMBIGUOUS);
}

static void epub(const void *p, size_t n) {
    cm_result r = detect(p, n);
    CHECK(!strcmp(r.mime, "application/epub+zip"));
    CHECK(r.flags == 0);
    CHECK(r.evidence == CM_STRUCTURE);
}

static void prefixes(const unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char *b = malloc(i ? i : 1);
        cm_result r;
        CHECK(b != NULL);
        memcpy(b, p, i);
        r = detect(b, i);
        CHECK(strcmp(r.mime, "application/epub+zip"));
        CHECK(cm_detect_prefix(b, i, &r) == CM_OK);
        CHECK(r.flags & CM_PARTIAL);
        CHECK(strcmp(r.mime, "application/epub+zip"));
        free(b);
    }
}

static void mutations(void) {
    unsigned char b[sizeof(epub_zip) + 22];
    size_t i;
    cm_result r;
#define RESET() memcpy(b, epub_zip, sizeof(epub_zip))
#define GENERIC(f) generic(b, sizeof(epub_zip), (f))
    RESET();
    b[38] ^= 1;
    GENERIC(0); /* marker payload differs */
    RESET();
    b[EPUB_CD + 16] ^= 1;
    GENERIC(CM_MALFORMED); /* CRC disagrees */
    RESET();
    b[14] ^= 1;
    b[EPUB_CD + 16] ^= 1;
    GENERIC(0); /* both CRCs wrong for marker */
    RESET();
    b[EPUB_CD + 46] = 'M';
    GENERIC(CM_MALFORMED); /* names conflict */
    RESET();
    memcpy(b + 30, "word.xml", 8);
    memcpy(b + EPUB_CD + 46, "word.xml", 8);
    GENERIC(0); /* marker bytes elsewhere are insufficient */
    RESET();
    b[EPUB_CD + 8] = 1;
    GENERIC(0); /* encryption: unsupported */
    RESET();
    b[6] = 2;
    b[EPUB_CD + 8] = 2;
    GENERIC(0); /* reserved stored-method flag */
    RESET();
    le16(b + EPUB_CD + 10, 99);
    GENERIC(0); /* unsupported method */
    RESET();
    le32(b + EPUB_CD + 20, UINT32_MAX);
    GENERIC(0); /* ZIP64 sentinel */
    RESET();
    le32(b + 18, UINT32_MAX);
    GENERIC(0);
    RESET();
    le32(b + EPUB_END + 16, UINT32_MAX);
    GENERIC(0);
    RESET();
    le16(b + EPUB_END + 4, 1);
    GENERIC(0); /* split archive */
    RESET();
    le16(b + EPUB_END + 8, 1);
    GENERIC(CM_MALFORMED);
    RESET();
    le32(b + EPUB_END + 12, 0xfffffff0u);
    GENERIC(CM_MALFORMED);
    RESET();
    le16(b + EPUB_END + 8, 3);
    le16(b + EPUB_END + 10, 3);
    GENERIC(CM_MALFORMED);
    RESET();
    le16(b + EPUB_CD + 28, 0xffffu);
    GENERIC(CM_MALFORMED);
    RESET();
    le16(b + EPUB_CD + 30, 2);
    GENERIC(CM_MALFORMED); /* malformed extra extent */
    RESET();
    le32(b + EPUB_SECOND_CD + 42, 1);
    GENERIC(0); /* unsupported overlap/layout */
    RESET();
    le32(b + EPUB_SECOND_CD + 20, 0xfffffff0u);
    GENERIC(CM_MALFORMED);
    RESET();
    le16(b + EPUB_END + 20, 1);
    GENERIC(CM_TRUNCATED); /* absent comment */
    b[sizeof(epub_zip)] = 'x';
    epub(b, sizeof(epub_zip) + 1); /* complete comment */
    RESET();
    memset(b + sizeof(epub_zip), 0, 22);
    le16(b + EPUB_END + 20, 22);
    memcpy(b + sizeof(epub_zip), "PK\005\006", 4);
    generic(b, sizeof(b), 0); /* a second end candidate inside the comment */
    RESET();
    CHECK(cm_detect_prefix(b, sizeof(epub_zip), &r) == CM_OK);
    CHECK(!strcmp(r.mime, "application/zip"));
    CHECK(r.flags & CM_PARTIAL);
    RESET();
    memcpy(b + EPUB_CD, epub_zip + EPUB_SECOND_CD, EPUB_END - EPUB_SECOND_CD);
    memcpy(b + EPUB_CD + EPUB_END - EPUB_SECOND_CD, epub_zip + EPUB_CD, EPUB_SECOND_CD - EPUB_CD);
    GENERIC(0); /* a legal reordered directory is unsupported, not malformed */
    /* Exercise arbitrary bit damage without assuming all changed bytes matter. */
    for (i = 0; i < sizeof(epub_zip); ++i) {
        RESET();
        b[i] ^= 0x80;
        r = detect(b, sizeof(epub_zip));
        CHECK(r.mime && r.reason);
    }
#undef RESET
#undef GENERIC
}

static void descriptors(void) {
    unsigned char b[sizeof(streaming_zip)];
    size_t n = sizeof(b) - 4;
    cm_result r = detect(streaming_zip, sizeof(streaming_zip));
    CHECK(!strcmp(r.mime, "application/zip"));
    CHECK(r.flags == CM_AMBIGUOUS);
    memcpy(b, streaming_zip, sizeof(b));
    b[62] ^= 1;
    generic(b, sizeof(b), CM_MALFORMED); /* signed descriptor CRC */
    memcpy(b, streaming_zip, 58);
    memcpy(b + 58, streaming_zip + 62, sizeof(b) - 62);
    le32(b + STREAMING_END - 4 + 16, STREAMING_CD - 4);
    le32(b + STREAMING_SECOND_CD - 4 + 42, STREAMING_SECOND_LOCAL - 4);
    generic(b, n, 0); /* unsigned descriptor */
    le32(b + 58, 0x08074b50u);
    le32(b + STREAMING_CD - 4 + 16, 0x08074b50u);
    generic(b, n, 0); /* an unsigned CRC equal to the optional signature */
}

static void limits(void) {
    unsigned char *b = malloc(CM_SCAN_LIMIT + 1);
    cm_result r;
    CHECK(b != NULL);
    memcpy(b, epub_zip, sizeof(epub_zip));
    memset(b + sizeof(epub_zip), 0, CM_SCAN_LIMIT + 1 - sizeof(epub_zip));
    r = detect(b, CM_SCAN_LIMIT + 1);
    CHECK(r.flags & CM_PARTIAL);
    CHECK(!strcmp(r.mime, "application/zip"));
    free(b);
    /* The maximum ZIP comment still permits bounded end-record discovery. */
    b = malloc(sizeof(epub_zip) + 65535u);
    CHECK(b != NULL);
    memcpy(b, epub_zip, sizeof(epub_zip));
    memset(b + sizeof(epub_zip), 'x', 65535);
    le16(b + EPUB_END + 20, 65535);
    epub(b, sizeof(epub_zip) + 65535u);
    free(b);
}

int main(int argc, char **argv) {
    if (argc == 2) { /* Reuse the independent fixture as a binary fuzz seed. */
        FILE *f = fopen(argv[1], "wb");
        CHECK(f != NULL);
        CHECK(fwrite(epub_zip, 1, sizeof(epub_zip), f) == sizeof(epub_zip));
        CHECK(fclose(f) == 0);
        return 0;
    }
    epub(epub_zip, sizeof(epub_zip));
    generic(compressed_zip, sizeof(compressed_zip), 0);
    generic(duplicate_zip, sizeof(duplicate_zip), 0);
    prefixes(epub_zip, sizeof(epub_zip));
    prefixes(compressed_zip, sizeof(compressed_zip));
    prefixes(streaming_zip, sizeof(streaming_zip));
    mutations();
    descriptors();
    limits();
    printf("%u ZIP checks passed\n", checks);
    return 0;
}
