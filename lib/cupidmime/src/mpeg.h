#ifndef CM_MPEG_INTERNAL_H
#define CM_MPEG_INTERNAL_H
#include "cupidmime.h"
/* ID3 or an offset-zero Layer III sync candidate claims the input. */
int cm_mpeg_detect(const unsigned char *p, size_t n, cm_result *r);
#endif
