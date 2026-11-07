#ifndef CM_MARKUP_INTERNAL_H
#define CM_MARKUP_INTERNAL_H

#include "cupidmime.h"

/* Input has already passed the UTF-8 text policy and had its BOM removed. */
void cm_markup_detect(const unsigned char *data, size_t size, cm_result *result);

#endif
