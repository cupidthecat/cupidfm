#define _GNU_SOURCE
#include "selection.h"

#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    char *path;
    bool directory;
    GList *position;
} SelectionEntry;

struct PathSelection {
    GHashTable *lookup;
    GQueue order;
};

static char *canonical(const char *path) {
    return path && path[0] == '/' ? g_canonicalize_filename(path, NULL) : NULL;
}

static void remove_entry(PathSelection *selection, SelectionEntry *entry) {
    g_queue_delete_link(&selection->order, entry->position);
    g_hash_table_remove(selection->lookup, entry->path);
    g_free(entry->path);
    g_free(entry);
}

bool selection_set(PathSelection **handle, const char *path, bool directory, bool selected) {
    char *key = handle ? canonical(path) : NULL;
    if (!key) {
        errno = EINVAL;
        return false;
    }
    PathSelection *selection = *handle;
    if (!selection && selected) {
        selection = g_new0(PathSelection, 1);
        selection->lookup = g_hash_table_new(g_str_hash, g_str_equal);
        *handle = selection;
    }
    SelectionEntry *existing = selection ? g_hash_table_lookup(selection->lookup, key) : NULL;
    if (existing || !selected) {
        if (existing && !selected)
            remove_entry(selection, existing);
        g_free(key);
        return true;
    }
    char *parent = g_path_get_dirname(key);
    for (;;) {
        SelectionEntry *ancestor = g_hash_table_lookup(selection->lookup, parent);
        if (ancestor && ancestor->directory) {
            g_free(parent);
            g_free(key);
            errno = EEXIST;
            return false;
        }
        if (!strcmp(parent, "/"))
            break;
        char *next = g_path_get_dirname(parent);
        g_free(parent);
        parent = next;
    }
    g_free(parent);
    if (directory) {
        size_t length = strlen(key);
        for (GList *item = selection->order.head; item; item = item->next) {
            SelectionEntry *entry = item->data;
            if (!strncmp(entry->path, key, length) &&
                (key[length - 1] == '/' || entry->path[length] == '/')) {
                g_free(key);
                errno = EEXIST;
                return false;
            }
        }
    }
    SelectionEntry *entry = g_new0(SelectionEntry, 1);
    entry->path = key;
    entry->directory = directory;
    g_queue_push_tail(&selection->order, entry);
    entry->position = selection->order.tail;
    g_hash_table_insert(selection->lookup, entry->path, entry);
    return true;
}

bool selection_has(const PathSelection *selection, const char *path) {
    if (!selection)
        return false;
    char *key = canonical(path);
    bool found = key && g_hash_table_contains(selection->lookup, key);
    g_free(key);
    return found;
}

size_t selection_count(const PathSelection *selection) {
    return selection ? selection->order.length : 0;
}

bool selection_export(const PathSelection *selection, Vector *paths) {
    *paths = Vector_new(selection_count(selection));
    if (!paths->el)
        return false;
    if (selection) {
        for (GList *item = selection->order.head; item; item = item->next) {
            const SelectionEntry *entry = item->data;
            char *path = strdup(entry->path);
            if (!path) {
                Vector_bye(paths);
                *paths = (Vector){0};
                return false;
            }
            paths->el[Vector_len(*paths)] = path;
            Vector_set_len_no_free(paths, Vector_len(*paths) + 1);
        }
    }
    return true;
}

void selection_clear(PathSelection *selection) {
    if (!selection)
        return;
    while (selection->order.head)
        remove_entry(selection, selection->order.head->data);
}

void selection_destroy(PathSelection *selection) {
    if (!selection)
        return;
    selection_clear(selection);
    g_hash_table_destroy(selection->lookup);
    g_free(selection);
}

void selection_prune(PathSelection *selection) {
    if (!selection)
        return;
    for (GList *item = selection->order.head, *next; item; item = next) {
        next = item->next;
        SelectionEntry *entry = item->data;
        struct stat info;
        if (lstat(entry->path, &info) && (errno == ENOENT || errno == ENOTDIR))
            remove_entry(selection, entry);
    }
}
