/* Optional libFuzzer entry point. The shipped library has no fuzzing dependency. */
#include "cupidmime.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    cm_result a, b;
    if (cm_detect(data, size, &a) != CM_OK || cm_detect_prefix(data, size, &b) != CM_OK)
        abort();
    if (!strcmp(b.mime, "application/epub+zip") || !strcmp(b.mime, "audio/ogg") ||
        !strcmp(b.mime, "audio/webm") || !strcmp(b.mime, "audio/matroska") ||
        (!strcmp(a.mime, "application/epub+zip") && (a.flags || a.evidence != CM_STRUCTURE)))
        abort();
    if (!a.mime || !a.reason || !b.mime || !b.reason || a.examined > CM_SCAN_LIMIT ||
        b.examined > CM_SCAN_LIMIT || !(b.flags & CM_PARTIAL))
        abort();
    return 0;
}
