#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <stdbool.h>
#include <stddef.h>

bool clipboard_set_from_file(const char *path);
bool ensure_cut_storage_dir(char *out_dir, size_t out_len);

#endif // CLIPBOARD_H
