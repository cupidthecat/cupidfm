#ifndef CM_ZIP_INTERNAL_H
#define CM_ZIP_INTERNAL_H
#include "cupidmime.h"
/* Called only for an offset-zero local-header or end-record signature. */
void cm_zip_detect(const unsigned char *p, size_t n, cm_result *r);
#endif
