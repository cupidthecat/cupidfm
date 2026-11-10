#define _GNU_SOURCE
#include "bulk_rename.h"

#include <errno.h>
#include <glib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "globals.h"
#include "process.h"

static bool fail(char **error, const char *message) {
    if (error && !*error)
        *error = g_strdup(message);
    return false;
}

static bool below(const char *parent, const char *path) {
    size_t length = strlen(parent);
    return !strncmp(parent, path, length) && (length == 1 || !path[length] || path[length] == '/');
}

void bulk_rename_free(BulkRenamePlan *plan) {
    if (!plan)
        return;
    for (size_t i = 0; i < plan->count; i++) {
        g_free(plan->entries[i].old_path);
        g_free(plan->entries[i].new_path);
        g_free(plan->entries[i].error);
    }
    g_free(plan->root);
    g_free(plan->entries);
    g_free(plan->order);
    *plan = (BulkRenamePlan){0};
}

bool bulk_rename_prepare(BulkRenamePlan *plan, const char *const *paths, size_t count,
                         char **error) {
    *plan = (BulkRenamePlan){0};
    if (!count || !paths)
        return fail(error, "No paths selected");
    plan->entries = g_new0(BulkRenameEntry, count);
    plan->order = g_new(size_t, count);
    plan->count = count;
    GHashTable *old = g_hash_table_new(g_str_hash, g_str_equal);
    bool ok = true;
    for (size_t i = 0; i < count; i++) {
        struct stat info;
        if (!paths[i] || paths[i][0] != '/' || !paths[i][1] || strchr(paths[i], '\n') ||
            strchr(paths[i], '\r') || strlen(paths[i]) >= MAX_PATH_LENGTH ||
            lstat(paths[i], &info)) {
            ok = fail(error, "Selected paths must exist, be absolute, and contain no line breaks");
            break;
        }
        BulkRenameEntry *entry = &plan->entries[i];
        entry->old_path = g_canonicalize_filename(paths[i], NULL);
        entry->device = info.st_dev;
        entry->inode = info.st_ino;
        entry->directory = S_ISDIR(info.st_mode);
        if (g_hash_table_contains(old, entry->old_path)) {
            ok = fail(error, "A source appears more than once");
            break;
        }
        g_hash_table_add(old, entry->old_path);
        if (!plan->root)
            plan->root = g_path_get_dirname(entry->old_path);
        else {
            while (!below(plan->root, entry->old_path)) {
                char *parent = g_path_get_dirname(plan->root);
                g_free(plan->root);
                plan->root = parent;
            }
        }
    }
    for (size_t i = 0; ok && i < count; i++) {
        char *parent = g_path_get_dirname(plan->entries[i].old_path);
        while (strcmp(parent, "/")) {
            if (g_hash_table_contains(old, parent)) {
                ok = fail(error, "A source and one of its descendants cannot be renamed together");
                break;
            }
            char *next = g_path_get_dirname(parent);
            g_free(parent);
            parent = next;
        }
        g_free(parent);
    }
    g_hash_table_unref(old);
    if (!ok)
        bulk_rename_free(plan);
    return ok;
}

bool bulk_rename_write(const BulkRenamePlan *plan, FILE *file, char **error) {
    size_t root = strlen(plan->root);
    for (size_t i = 0; i < plan->count; i++) {
        const char *path = plan->entries[i].old_path;
        path += root;
        if (*path == '/')
            path++;
        if (fprintf(file, "%s\n", path) < 0)
            return fail(error, "Cannot write rename list");
    }
    return fflush(file) == 0 || fail(error, "Cannot flush rename list");
}

static bool line(FILE *file, char buffer[MAX_PATH_LENGTH], char **error) {
    size_t used = 0;
    int c;
    while ((c = fgetc(file)) != EOF && c != '\n') {
        if (c == 0 || used + 1 >= MAX_PATH_LENGTH)
            return fail(error, "A new name is too long or contains a NUL byte");
        buffer[used++] = (char)c;
    }
    if (used && buffer[used - 1] == '\r')
        used--;
    buffer[used] = 0;
    if (!used)
        return fail(error, "Rename list has an empty name or too few lines");
    if (ferror(file))
        return fail(error, "Cannot read rename list");
    return true;
}

bool bulk_rename_read(BulkRenamePlan *plan, FILE *file, char **error) {
    GHashTable *sources = g_hash_table_new(g_str_hash, g_str_equal);
    GHashTable *targets = g_hash_table_new(g_str_hash, g_str_equal);
    size_t *dependent = g_new(size_t, plan->count);
    bool *queued = g_new0(bool, plan->count);
    size_t order_count = 0;
    bool ok = true;
    plan->changed = 0;
    for (size_t i = 0; i < plan->count; i++) {
        dependent[i] = SIZE_MAX;
        g_hash_table_insert(sources, plan->entries[i].old_path, &plan->entries[i]);
    }
    for (size_t i = 0; i < plan->count; i++) {
        char name[MAX_PATH_LENGTH];
        if (!line(file, name, error)) {
            ok = false;
            break;
        }
        BulkRenameEntry *entry = &plan->entries[i];
        g_free(entry->new_path);
        entry->new_path = g_canonicalize_filename(name, plan->root);
        if (strlen(entry->new_path) >= MAX_PATH_LENGTH || !strcmp(entry->new_path, "/") ||
            (entry->directory && strcmp(entry->old_path, entry->new_path) &&
             below(entry->old_path, entry->new_path))) {
            ok = fail(error,
                      "A destination is too long, is the filesystem root, or is inside itself");
            break;
        }
        if (g_hash_table_contains(targets, entry->new_path)) {
            ok = fail(error, "Two entries have the same destination");
            break;
        }
        g_hash_table_add(targets, entry->new_path);
        plan->changed += strcmp(entry->old_path, entry->new_path) != 0;
    }
    if (ok && (fgetc(file) != EOF || ferror(file)))
        ok = fail(error, "Rename list has too many lines or could not be read");
    for (size_t i = 0; ok && i < plan->count; i++) {
        BulkRenameEntry *entry = &plan->entries[i];
        if (!strcmp(entry->old_path, entry->new_path))
            continue;
        BulkRenameEntry *occupied = g_hash_table_lookup(sources, entry->new_path);
        struct stat info;
        if (!occupied) {
            if (!lstat(entry->new_path, &info)) {
                ok = fail(error, "A destination already exists outside the rename batch");
                break;
            }
            if (errno != ENOENT) {
                ok = fail(error, "Cannot inspect a destination");
                break;
            }
            queued[i] = true;
            plan->order[order_count++] = i;
        } else if (!strcmp(occupied->old_path, occupied->new_path)) {
            ok = fail(error, "A destination belongs to an unchanged entry");
            break;
        } else {
            dependent[(size_t)(occupied - plan->entries)] = i;
        }
    }
    /* Each unique destination has at most one dependency; process free ends
     * before entries targeting their old names. Cycles are attempted last and
     * fail without replacing either source. */
    for (size_t at = 0; ok && at < order_count; at++) {
        size_t next = dependent[plan->order[at]];
        if (next != SIZE_MAX && !queued[next]) {
            queued[next] = true;
            plan->order[order_count++] = next;
        }
    }
    for (size_t i = 0; ok && i < plan->count; i++)
        if (!queued[i] && strcmp(plan->entries[i].old_path, plan->entries[i].new_path))
            plan->order[order_count++] = i;
    g_free(dependent);
    g_free(queued);
    g_hash_table_unref(targets);
    g_hash_table_unref(sources);
    return ok;
}

void bulk_rename_apply(BulkRenamePlan *plan) {
    plan->succeeded = plan->failed = 0;
    for (size_t i = 0; i < plan->changed; i++) {
        BulkRenameEntry *entry = &plan->entries[plan->order[i]];
        struct stat info;
        g_clear_pointer(&entry->error, g_free);
        entry->renamed = false;
        int error = 0;
        if (lstat(entry->old_path, &info))
            error = errno;
        else if (info.st_dev != entry->device || info.st_ino != entry->inode)
            error = ESTALE;
        else if (!file_command_rename(entry->old_path, entry->new_path))
            error = errno;
        if (error) {
            entry->error = g_strdup(g_strerror(error));
            plan->failed++;
        } else {
            entry->renamed = true;
            plan->succeeded++;
        }
    }
}
