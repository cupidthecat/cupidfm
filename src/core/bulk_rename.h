#ifndef BULK_RENAME_H
#define BULK_RENAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

typedef struct {
    char *old_path, *new_path;
    char *error;
    dev_t device;
    ino_t inode;
    bool directory;
    bool renamed;
} BulkRenameEntry;

typedef struct {
    char *root;
    BulkRenameEntry *entries;
    size_t *order;
    size_t count, changed, succeeded, failed;
} BulkRenamePlan;

/* A line is a path relative to the common parent, or an absolute path.
 * Sources with line breaks cannot be represented and are rejected. */
bool bulk_rename_prepare(BulkRenamePlan *plan, const char *const *paths, size_t count,
                         char **error);
bool bulk_rename_write(const BulkRenamePlan *plan, FILE *file, char **error);
bool bulk_rename_read(BulkRenamePlan *plan, FILE *file, char **error);
/* Never replaces an existing destination. Independent successes are retained. */
void bulk_rename_apply(BulkRenamePlan *plan);
void bulk_rename_free(BulkRenamePlan *plan);

#endif
