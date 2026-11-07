#define _GNU_SOURCE
#include "openers.h"

#include <errno.h>
#include <fnmatch.h>
#include <string.h>

#include "cupidconf.h"

typedef struct {
    char *name, *path, *mime;
    GPtrArray *use;
} Rule;

struct Openers {
    GPtrArray *openers;
    GPtrArray *rules;
};

static void opener_free(gpointer data) {
    Opener *opener = data;
    g_free(opener->name);
    g_free(opener->description);
    g_free(opener->command);
    g_free(opener->platform);
    g_free(opener);
}

static void rule_free(gpointer data) {
    Rule *rule = data;
    g_free(rule->name);
    g_free(rule->path);
    g_free(rule->mime);
    g_ptr_array_free(rule->use, TRUE);
    g_free(rule);
}

void openers_free(Openers *set) {
    if (!set)
        return;
    g_ptr_array_free(set->openers, TRUE);
    g_ptr_array_free(set->rules, TRUE);
    g_free(set);
}

static Opener *lookup(Openers *set, const char *name, bool create) {
    for (guint i = 0; i < set->openers->len; i++) {
        Opener *opener = g_ptr_array_index(set->openers, i);
        if (!strcmp(opener->name, name))
            return opener;
    }
    if (!create)
        return NULL;
    Opener *opener = g_new0(Opener, 1);
    opener->name = g_strdup(name);
    g_ptr_array_add(set->openers, opener);
    return opener;
}

static Rule *rule_lookup(Openers *set, const char *name) {
    for (guint i = 0; i < set->rules->len; i++) {
        Rule *rule = g_ptr_array_index(set->rules, i);
        if (!strcmp(rule->name, name))
            return rule;
    }
    Rule *rule = g_new0(Rule, 1);
    rule->name = g_strdup(name);
    rule->use = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(set->rules, rule);
    return rule;
}

static void assign(char **target, const char *value) {
    g_free(*target);
    *target = g_strdup(value);
}

static bool boolean(const char *text, bool *value) {
    if (!g_ascii_strcasecmp(text, "true") || !strcmp(text, "1")) {
        *value = true;
        return true;
    }
    if (!g_ascii_strcasecmp(text, "false") || !strcmp(text, "0")) {
        *value = false;
        return true;
    }
    return false;
}

/* Match alternations such as application/{json,javascript} as ordinary globs.
 * Brace expansion is bounded by the configuration text, not the filesystem. */
static const char *glob_next(const char *at) {
    if (*at == '\\' && at[1])
        return at + 2;
    if (*at == '[') {
        const char *end = at + 1;
        if (*end == '!' || *end == '^')
            end++;
        if (*end == ']')
            end++;
        while (*end && *end != ']')
            end += *end == '\\' && end[1] ? 2 : 1;
        if (*end)
            return end + 1;
    }
    return at + 1;
}

static bool expand_match(const char *pattern, const char *text, unsigned depth, unsigned *budget) {
    if (!pattern || !text || depth > 16 || !*budget)
        return false;
    (*budget)--;
    const char *open = pattern;
    while (*open && *open != '{')
        open = glob_next(open);
    if (!*open)
        return fnmatch(pattern, text, 0) == 0;
    const char *close = open + 1;
    unsigned nesting = 1;
    while (*close) {
        if (*close == '{')
            nesting++;
        else if (*close == '}' && !--nesting)
            break;
        close = glob_next(close);
    }
    if (!*close)
        return false;
    const char *first = open + 1, *at = first;
    nesting = 0;
    for (;;) {
        if (at == close || (*at == ',' && !nesting)) {
            char *prefix = g_strndup(pattern, (gsize)(open - pattern));
            char *part = g_strndup(first, (gsize)(at - first));
            char *expanded = g_strconcat(prefix, part, close + 1, NULL);
            bool matched = expand_match(expanded, text, depth + 1, budget);
            g_free(prefix);
            g_free(part);
            g_free(expanded);
            if (matched)
                return true;
            if (!*budget || at == close)
                return false;
            first = at + 1;
        } else if (*at == '{')
            nesting++;
        else if (*at == '}')
            nesting--;
        at = glob_next(at);
    }
}

static bool pattern_match(const char *pattern, const char *text) {
    unsigned budget = 4096;
    return expand_match(pattern, text, 0, &budget);
}

static bool available(const Opener *opener) {
    return opener && opener->command && *opener->command &&
           (!opener->platform || !strcmp(opener->platform, "any") ||
            !strcmp(opener->platform, "unix") || !strcmp(opener->platform, "linux"));
}

static void append(GPtrArray *result, Openers *set, const char *name) {
    Opener *opener = lookup(set, name, false);
    if (available(opener) && !g_ptr_array_find(result, opener, NULL))
        g_ptr_array_add(result, opener);
}

GPtrArray *openers_match(Openers *set, const char *path, const char *mime) {
    GPtrArray *result = g_ptr_array_new();
    if (!set || !path)
        return result;
    char *directory_path = mime && !strncmp(mime, "folder/", 7) && !g_str_has_suffix(path, "/")
                               ? g_strconcat(path, "/", NULL)
                               : NULL;
    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    for (guint i = 0; i < set->rules->len; i++) {
        Rule *rule = g_ptr_array_index(set->rules, i);
        bool path_match =
            !rule->path || pattern_match(rule->path, strchr(rule->path, '/')
                                                         ? (directory_path ? directory_path : path)
                                                         : basename);
        bool mime_match = !rule->mime || pattern_match(rule->mime, mime ? mime : "");
        if (!path_match || !mime_match)
            continue;
        for (guint j = 0; j < rule->use->len; j++)
            append(result, set, g_ptr_array_index(rule->use, j));
        g_free(directory_path);
        return result;
    }
    g_free(directory_path);
    bool text =
        mime &&
        (!strncmp(mime, "text/", 5) || !strcmp(mime, "inode/empty") ||
         !strcmp(mime, "application/json") || !strcmp(mime, "application/javascript") ||
         !strcmp(mime, "application/xml") ||
         pattern_match(
             "application/{ndjson,x-shellscript,x-sh,x-perl,x-php,x-yaml,toml,wine-extension-ini}",
             mime) ||
         g_str_has_suffix(mime, "+xml") || g_str_has_suffix(mime, "+json"));
    /* Image XML subtypes retain their image opener. */
    if (mime && !strncmp(mime, "folder/", 7)) {
        append(result, set, "edit");
        append(result, set, "open");
    } else if (text && strncmp(mime, "image/", 6))
        append(result, set, "edit");
    else if (mime && (!strncmp(mime, "audio/", 6) || !strncmp(mime, "video/", 6)))
        append(result, set, "play");
    else
        append(result, set, "open");
    append(result, set, "reveal");
    return result;
}

Openers *openers_load(const char *filename, GError **error) {
    Openers *set = g_new0(Openers, 1);
    set->openers = g_ptr_array_new_with_free_func(opener_free);
    set->rules = g_ptr_array_new_with_free_func(rule_free);

    const struct {
        const char *name, *description, *command;
        bool block, orphan;
    } defaults[] = {
        {"edit", "Edit ($EDITOR)",
         "$"
         "{EDITOR:-vi} \"$@\"",
         true, false},
        {"open", "Open", "for path do xdg-open \"$path\"; done", false, false},
        {"play", "Play", "for path do xdg-open \"$path\"; done", false, true},
        {"reveal", "Reveal parent directory",
         "for path do parent=$"
         "{path%/*}; [ -n \"$parent\" ] || parent=/; xdg-open \"$parent\"; done",
         false, false},
    };

    for (size_t i = 0; i < G_N_ELEMENTS(defaults); i++) {
        Opener *opener = lookup(set, defaults[i].name, true);
        opener->description = g_strdup(defaults[i].description);
        opener->command = g_strdup(defaults[i].command);
        opener->block = defaults[i].block;
        opener->orphan = defaults[i].orphan;
    }
    if (!filename || !g_file_test(filename, G_FILE_TEST_EXISTS))
        return set;
    cupidconf_t *config = cupidconf_load(filename);
    if (!config) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "%s: %s", filename,
                    g_strerror(errno));
        openers_free(set);
        return NULL;
    }
    const char *bad = NULL;
    for (cupidconf_entry *entry = config->entries; entry; entry = entry->next) {
        const char *key = entry->key;
        const char *field = strrchr(key, '.');
        bool is_opener = g_str_has_prefix(key, "opener.");
        bool is_rule = g_str_has_prefix(key, "rule.");
        size_t prefix = is_opener ? 7 : 5;
        if ((!is_opener && !is_rule) || !field || field <= key + prefix || !field[1]) {
            bad = key;
            break;
        }
        char *name = g_strndup(key + prefix, (gsize)(field - key - prefix));
        field++;
        bool valid = true;
        if (is_opener) {
            Opener *opener = lookup(set, name, true);
            if (!strcmp(field, "command"))
                assign(&opener->command, entry->value);
            else if (!strcmp(field, "description"))
                assign(&opener->description, entry->value);
            else if (!strcmp(field, "for")) {
                valid = !strcmp(entry->value, "any") || !strcmp(entry->value, "unix") ||
                        !strcmp(entry->value, "linux") || !strcmp(entry->value, "macos") ||
                        !strcmp(entry->value, "windows") || !strcmp(entry->value, "android");
                if (valid)
                    assign(&opener->platform, entry->value);
            } else if (!strcmp(field, "block"))
                valid = boolean(entry->value, &opener->block);
            else if (!strcmp(field, "orphan"))
                valid = boolean(entry->value, &opener->orphan);
            else
                valid = false;
        } else {
            Rule *rule = rule_lookup(set, name);
            if (!strcmp(field, "path"))
                assign(&rule->path, entry->value);
            else if (!strcmp(field, "mime"))
                assign(&rule->mime, entry->value);
            else if (!strcmp(field, "use")) {
                char **names = g_strsplit_set(entry->value, ", \t", -1);
                for (size_t i = 0; names[i]; i++)
                    if (*names[i])
                        g_ptr_array_add(rule->use, g_strdup(names[i]));
                g_strfreev(names);
            } else
                valid = false;
        }
        g_free(name);
        if (!valid) {
            bad = key;
            break;
        }
    }
    if (bad)
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid opener setting: %s", bad);
    for (guint i = 0; !bad && i < set->openers->len; i++) {
        Opener *opener = g_ptr_array_index(set->openers, i);
        if (!opener->command || !*opener->command) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Opener has no command: %s",
                        opener->name);
            bad = opener->name;
        }
    }
    for (guint i = 0; !bad && i < set->rules->len; i++) {
        Rule *rule = g_ptr_array_index(set->rules, i);
        if ((!rule->mime && !rule->path) || !rule->use->len) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Incomplete opener rule: %s",
                        rule->name);
            bad = rule->name;
        }
        for (guint j = 0; !bad && j < rule->use->len; j++)
            if (!lookup(set, g_ptr_array_index(rule->use, j), false)) {
                g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Unknown opener in rule: %s",
                            rule->name);
                bad = rule->name;
            }
    }
    cupidconf_free(config);
    if (bad) {
        openers_free(set);
        return NULL;
    }
    return set;
}
