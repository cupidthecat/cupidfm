#ifndef CUPIDMIME_H
#define CUPIDMIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CM_VERSION "0.1.0"
#define CM_SCAN_LIMIT ((size_t)1048576)

typedef enum { CM_OK = 0, CM_INVALID_ARGUMENT = 1 } cm_status;

typedef enum { CM_FALLBACK = 0, CM_TEXT = 1, CM_SIGNATURE = 2, CM_STRUCTURE = 3 } cm_evidence;

enum {
    CM_TRUNCATED = 1u, /* A recognized header or declared extent is missing bytes. */
    CM_MALFORMED = 2u, /* A checked format constraint failed. */
    CM_PARTIAL = 4u,   /* Only a prefix was examined. */
    CM_AMBIGUOUS = 8u  /* A container subtype or format candidate remains unresolved. */
};

typedef struct {
    const char *mime;   /* Static storage, no parameters. */
    const char *reason; /* Static diagnostic, not a stable machine identifier. */
    cm_evidence evidence;
    unsigned flags;
    size_t examined; /* Size of the bounded inspection window. */
} cm_result;

/* NULL data is permitted only with size == 0. No output is changed on error.
 * At most CM_SCAN_LIMIT bytes are inspected. This is identification, not validation.
 */
cm_status cm_detect(const void *data, size_t size, cm_result *out);
/* Use when the supplied bytes are a prefix of a longer resource. */
cm_status cm_detect_prefix(const void *data, size_t size, cm_result *out);
const char *cm_version(void);

#ifdef __cplusplus
}
#endif
#endif
