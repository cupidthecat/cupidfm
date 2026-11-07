/* Region-engine adaptation.
 * Copyright (c) 2016-2020: Zachary Yedidia, et al.
 * SPDX-License-Identifier: MIT
 * See syntax/LICENSE for the permission and warranty terms.
 */
#define _GNU_SOURCE
#include "syntax_rules.h"
#include "cupidconf.h"
#include "syntax.h"
#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int parent;
    char *group, *limit_group;
    unsigned char color, limit;
    GRegex *match, *start, *end, *skip;
} Rule;

struct SyntaxRules {
    char *name;
    GRegex *filename, *header, *signature;
    Rule *rules;
    size_t count;
};

static unsigned char color(const char *s) {
    if (!s)
        return 0;
    if (!strcmp(s, "todo"))
        return 23;
    if (!strcmp(s, "error") || !strcmp(s, "gutter-error"))
        return 22;
    if (!strcmp(s, "special"))
        return 21;
    if (!strcmp(s, "underlined"))
        return 25;
    if (!strcmp(s, "symbol.tag") || !strcmp(s, "symbol.tag.extended"))
        return 24;
    if (!strcmp(s, "constant.string.char"))
        return 20;
    if (!strncmp(s, "indent-char", 11))
        return 26;
    const char *named[] = {"black",       "red",           "green",       "yellow",
                           "blue",        "magenta",       "cyan",        "white",
                           "brightblack", "brightred",     "brightgreen", "brightyellow",
                           "brightblue",  "brightmagenta", "brightcyan",  "brightwhite"};
    for (int i = 0; i < 16; i++)
        if (!strcmp(s, named[i]))
            return 27 + i;
    if (!strcmp(s, "diff-modified"))
        return 45;
    if (!strcmp(s, "constant-string"))
        return 44;
    if (!strcmp(s, "bold default"))
        return 43;
    if (strstr(s, "comment") || !strcmp(s, "brightblack"))
        return COLOR_SYNTAX_COMMENT;
    if (strstr(s, "string"))
        return COLOR_SYNTAX_STRING;
    if (strstr(s, "constant") || strstr(s, "number"))
        return COLOR_SYNTAX_NUMBER;
    if (!strncmp(s, "type.extended", 13))
        return 44;
    if (strstr(s, "type"))
        return COLOR_SYNTAX_TYPE;
    if (strstr(s, "preproc"))
        return COLOR_SYNTAX_PREPROCESSOR;
    if (strstr(s, "statement"))
        return COLOR_SYNTAX_KEYWORD;
    if (!strncmp(s, "symbol.operator", 15))
        return COLOR_SYNTAX_OPERATOR;
    if (strstr(s, "identifier"))
        return COLOR_SYNTAX_FUNCTION;
    return 44;
}

static GRegex *compile(const char *pattern, bool *ok) {
    if (!pattern)
        return NULL;
    GError *error = NULL;
    GRegex *r = g_regex_new(pattern, G_REGEX_OPTIMIZE, 0, &error);
    if (!r) {
        fprintf(stderr, "Syntax regex: %s\n", error->message);
        g_clear_error(&error);
        *ok = false;
    }
    return r;
}

static const char *field(cupidconf_t *conf, size_t i, const char *name) {
    char key[80];
    snprintf(key, sizeof(key), "rule.%zu.%s", i, name);
    return cupidconf_get(conf, key);
}

SyntaxRules *syntax_rules_load(const char *path) {
    cupidconf_t *conf = cupidconf_load(path);
    if (!conf)
        return NULL;
    const char *version = cupidconf_get(conf, "format"), *name = cupidconf_get(conf, "name");
    const char *count = cupidconf_get(conf, "rules");
    if (!version || strcmp(version, "2") || !name || !count) {
        cupidconf_free(conf);
        return NULL;
    }
    char *end;
    errno = 0;
    long length = strtol(count, &end, 10);
    if (errno || end == count || *end || length < 0 || length > 100000) {
        cupidconf_free(conf);
        return NULL;
    }
    SyntaxRules *r = g_new0(SyntaxRules, 1);
    r->name = g_strdup(name);
    r->count = length;
    r->rules = g_new0(Rule, length);
    bool ok = true;
    r->filename = compile(cupidconf_get(conf, "detect.filename"), &ok);
    r->header = compile(cupidconf_get(conf, "detect.header"), &ok);
    r->signature = compile(cupidconf_get(conf, "detect.signature"), &ok);
    for (size_t i = 0; i < r->count; i++) {
        Rule *rule = &r->rules[i];
        const char *parent = field(conf, i, "parent");
        errno = 0;
        long parsed = parent ? strtol(parent, &end, 10) : -2;
        if (!parent || errno || end == parent || *end || parsed < -1 || parsed >= (long)i)
            ok = false;
        rule->parent = parsed >= -1 && parsed < (long)i ? (int)parsed : -1;
        rule->group = g_strdup(field(conf, i, "group"));
        if (!rule->group)
            ok = false;
        rule->color = color(rule->group);
        const char *limit = field(conf, i, "limit-group");
        rule->limit_group = g_strdup(limit ? limit : rule->group);
        rule->limit = limit ? color(limit) : rule->color;
        rule->match = compile(field(conf, i, "match"), &ok);
        rule->start = compile(field(conf, i, "start"), &ok);
        rule->end = compile(field(conf, i, "end"), &ok);
        rule->skip = compile(field(conf, i, "skip"), &ok);
        if (!rule->match && (!rule->start || !rule->end))
            ok = false;
    }
    cupidconf_free(conf);
    if (!ok) {
        syntax_rules_free(r);
        return NULL;
    }
    return r;
}

void syntax_rules_free(SyntaxRules *r) {
    if (!r)
        return;
    GRegex *detect[] = {r->filename, r->header, r->signature};
    for (size_t j = 0; j < 3; j++)
        if (detect[j])
            g_regex_unref(detect[j]);
    for (size_t i = 0; i < r->count; i++) {
        g_free(r->rules[i].group);
        g_free(r->rules[i].limit_group);
        GRegex *items[] = {r->rules[i].match, r->rules[i].start, r->rules[i].end, r->rules[i].skip};
        for (size_t j = 0; j < 4; j++)
            if (items[j])
                g_regex_unref(items[j]);
    }
    g_free(r->rules);
    g_free(r->name);
    g_free(r);
}

const char *syntax_rules_name(const SyntaxRules *r) {
    return r->name;
}

bool syntax_rules_detect(const SyntaxRules *r, const char *kind, const char *text) {
    GRegex *regex = !strcmp(kind, "filename") ? r->filename
                    : !strcmp(kind, "header") ? r->header
                                              : r->signature;
    return regex && text && g_regex_match(regex, text, 0, NULL);
}

static bool find(GRegex *regex, const char *text, int length, int offset, int *start, int *end) {
    if (!regex || offset > length)
        return false;
    GMatchInfo *info = NULL;
    bool found = g_regex_match_full(regex, text, length, offset, 0, &info, NULL);
    if (found)
        g_match_info_fetch_pos(info, 0, start, end);
    if (info)
        g_match_info_free(info);
    return found;
}

/* Region traversal follows the ordered rules licensed in syntax/LICENSE.
 * Matches and offsets stay in bytes until the renderer decodes characters. */
static bool boundary(GRegex *regex, GRegex *skip, const char *line, int length, int *a, int *b) {
    if (!regex)
        return false;
    if (!skip)
        return find(regex, line, length, 0, a, b);
    char *masked = g_strndup(line, length);
    int start, end, offset = 0;
    while (find(skip, line, length, offset, &start, &end)) {
        if (end > start)
            memset(masked + start, 0, end - start);
        offset = end > start ? end : start + 1;
    }
    bool matched = find(regex, masked, length, 0, a, b);
    g_free(masked);
    return matched;
}

typedef struct {
    const SyntaxRules *rules;
    const char *line;
    int *changes;
    int last;
    size_t budget;
    unsigned depth;
} Highlight;

static void set(Highlight *h, int position, int color) {
    if (h->changes)
        h->changes[position] = color;
}

static void traverse(Highlight *h, int offset, int length, int parent, bool final) {
    if (h->depth >= 128) {
        if (final)
            h->last = parent;
        return;
    }
    h->depth++;
again:
    if (!h->budget) {
        if (final)
            h->last = parent;
        h->depth--;
        return;
    }
    h->budget--;
    const char *line = h->line + offset;
    const SyntaxRules *r = h->rules;
    if (parent < 0 && !length) {
        if (final)
            h->last = -1;
        h->depth--;
        return;
    }
    const Rule *active = parent < 0 ? NULL : &r->rules[parent];
    if (active && offset == 0 && h->changes && h->changes[0] < 0)
        set(h, 0, active->color);
    int ea = 0, eb = 0;
    bool ends = active && boundary(active->end, active->skip, line, length, &ea, &eb);
    bool nesting = !ends || offset != ea;
    int first = ends && nesting ? ea : length, last = 0, child = -1;
    for (size_t i = 0; nesting && i < r->count; i++) {
        const Rule *rule = &r->rules[i];
        int a, b;
        if (rule->parent == parent && boundary(rule->start, rule->skip, line, length, &a, &b) &&
            a < first) {
            first = a;
            last = b;
            child = (int)i;
        }
    }
    if (child >= 0 && first != length) {
        set(h, offset + first, r->rules[child].limit);
        if (parent < 0)
            traverse(h, offset, first, -1, false);
        else
            traverse(h, offset + last, length - last, -1, false);
        offset += last;
        length -= last;
        parent = child;
        goto again;
    }
    if (h->changes) {
        unsigned char *colors = g_malloc(MAX(1, length));
        memset(colors, active ? active->color : 44, length);
        for (size_t i = 0; i < r->count; i++) {
            const Rule *rule = &r->rules[i];
            if (rule->parent != parent || !rule->match ||
                (active && (!nesting || (strcmp(active->group, active->limit_group) &&
                                         strcmp(rule->group, active->limit_group)))))
                continue;
            int a, b, scan = 0;
            while (find(rule->match, line, length, scan, &a, &b)) {
                if ((!ends || a < ea) && b > a)
                    memset(colors + a, rule->color, b - a);
                scan = b > a ? b : a + 1;
            }
        }
        for (int i = 0; i < length; i++)
            if (!i || colors[i] != colors[i - 1])
                set(h, offset + i, colors[i]);
        g_free(colors);
    }
    if (ends) {
        set(h, offset + ea, active->limit);
        int next = active->parent;
        set(h, offset + eb, next < 0 ? 44 : r->rules[next].color);
        offset += eb;
        length -= eb;
        parent = next;
        goto again;
    } else if (final)
        h->last = parent;
    h->depth--;
}

int syntax_rules_line(const SyntaxRules *r, const char *line, int state, unsigned char *colors) {
    size_t full_length = strlen(line), length = full_length;
    if (length && line[length - 1] == '\n')
        length--;
    if (length && line[length - 1] == '\r')
        length--;
    if (colors)
        memset(colors, 44, full_length);
    if (length > G_MAXINT)
        return 0;
    Highlight h = {.rules = r, .line = line, .last = -1, .budget = (length + 1) * 32};
    if (colors) {
        h.changes = g_new(int, length + 1);
        for (size_t i = 0; i <= length; i++)
            h.changes[i] = -1;
    }
    int parent = state > 0 && state <= (int)r->count && r->rules[state - 1].start ? state - 1 : -1;
    traverse(&h, 0, (int)length, parent, true);
    if (colors) {
        int current = 44;
        for (size_t i = 0; i < length; i++) {
            if (h.changes[i] >= 0)
                current = h.changes[i];
            colors[i] = current;
        }
        g_free(h.changes);
    }
    return h.last + 1;
}
