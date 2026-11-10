// syntax.c - Syntax highlighting system for CupidFM text editor
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "syntax.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cupidconf.h"
#include "globals.h"

#define MAX_SYNTAX_DEFS 512
#define MAX_LINE_LENGTH 1024

static SyntaxDef g_syntax_defs[MAX_SYNTAX_DEFS];
static size_t g_syntax_count = 0;
static bool g_syntax_initialized = false;

// Forward declarations
static bool parse_color_rgb(const char *value, int rgb[3]);

// Helper: get home directory
static const char *get_home_dir(void) {
    const char *home = getenv("HOME");
    return home ? home : "/tmp";
}

// Helper: trim whitespace from both ends
static char *trim_whitespace(char *str) {
    if (!str)
        return NULL;
    while (isspace((unsigned char)*str))
        str++;
    if (*str == 0)
        return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    *(end + 1) = '\0';
    return str;
}

// Helper: check if string ends with suffix
static bool ends_with(const char *str, const char *suffix) {
    if (!str || !suffix)
        return false;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len)
        return false;
    return strcasecmp(str + str_len - suffix_len, suffix) == 0;
}

// Helper: split comma-separated values into array
static char **split_csv(const char *value, size_t *count) {
    if (!value || !count)
        return NULL;

    *count = 0;
    char *copy = strdup(value);
    if (!copy)
        return NULL;

    // Count items
    char *tmp = strdup(copy);
    char *token = strtok(tmp, ",");
    while (token) {
        (*count)++;
        token = strtok(NULL, ",");
    }
    free(tmp);

    if (*count == 0) {
        free(copy);
        return NULL;
    }

    char **result = calloc(*count, sizeof(char *));
    if (!result) {
        free(copy);
        return NULL;
    }

    size_t i = 0;
    token = strtok(copy, ",");
    while (token && i < *count) {
        result[i] = strdup(trim_whitespace(token));
        i++;
        token = strtok(NULL, ",");
    }

    free(copy);
    return result;
}

// Free a syntax definition
static void syntax_def_free(SyntaxDef *def) {
    if (!def)
        return;

    syntax_rules_free(def->rules);
    free(def->language);

    for (size_t i = 0; i < def->keyword_count; i++) {
        free(def->keywords[i]);
    }
    free(def->keywords);

    for (size_t i = 0; i < def->type_count; i++) {
        free(def->types[i]);
    }
    free(def->types);

    for (size_t i = 0; i < def->statement_count; i++) {
        free(def->statements[i]);
    }
    free(def->statements);

    for (size_t i = 0; i < def->constant_count; i++) {
        free(def->constants[i]);
    }
    free(def->constants);

    for (size_t i = 0; i < def->preprocessor_count; i++) {
        free(def->preprocessor[i]);
    }
    free(def->preprocessor);

    for (size_t i = 0; i < def->extension_count; i++) {
        free(def->extensions[i]);
    }
    free(def->extensions);

    free(def->line_comment);
    free(def->block_comment_start);
    free(def->block_comment_end);

    // Reset color arrays
    for (int i = 0; i < 3; i++) {
        def->color_keyword[i] = -1;
        def->color_type[i] = -1;
        def->color_string[i] = -1;
        def->color_comment[i] = -1;
        def->color_number[i] = -1;
        def->color_preprocessor[i] = -1;
        def->color_operator[i] = -1;
        def->color_function[i] = -1;
    }

    memset(def, 0, sizeof(SyntaxDef));
}

// Load a single syntax definition from a cupidconf file
static bool load_syntax_file(const char *filepath) {
    if (g_syntax_count >= MAX_SYNTAX_DEFS)
        return false;

    cupidconf_t *config = cupidconf_load(filepath);
    const char *format = config ? cupidconf_get(config, "format") : NULL;
    bool modern = format && !strcmp(format, "2");
    if (modern) {
        const char *configured_name = cupidconf_get(config, "name");
        if (configured_name) {
            for (size_t i = 0; i < g_syntax_count; i++)
                if (!strcmp(g_syntax_defs[i].language, configured_name)) {
                    cupidconf_free(config);
                    return true;
                }
        }
        SyntaxRules *rules = syntax_rules_from_config(config);
        cupidconf_free(config);
        if (!rules)
            return false;
        const char *name = syntax_rules_name(rules);
        for (size_t i = 0; i < g_syntax_count; i++)
            if (!strcmp(g_syntax_defs[i].language, name)) {
                syntax_rules_free(rules);
                return true;
            }
        SyntaxDef *def = &g_syntax_defs[g_syntax_count++];
        *def = (SyntaxDef){.rules = rules, .language = g_strdup(name), .loaded = true};
        return true;
    }

    cupidconf_free(config);
    FILE *fp = fopen(filepath, "r");
    if (!fp)
        return false;

    SyntaxDef *def = &g_syntax_defs[g_syntax_count];
    memset(def, 0, sizeof(SyntaxDef));

    // Initialize color arrays to -1 (not set)
    for (int i = 0; i < 3; i++) {
        def->color_keyword[i] = -1;
        def->color_type[i] = -1;
        def->color_string[i] = -1;
        def->color_comment[i] = -1;
        def->color_number[i] = -1;
        def->color_preprocessor[i] = -1;
        def->color_operator[i] = -1;
        def->color_function[i] = -1;
    }

    // Extract language name from filename (e.g., "c.cupidconf" -> "c")
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    char lang_name[64];
    strncpy(lang_name, filename, sizeof(lang_name) - 1);
    lang_name[sizeof(lang_name) - 1] = '\0';
    char *dot = strrchr(lang_name, '.');
    if (dot)
        *dot = '\0';
    def->language = strdup(lang_name);

    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim_whitespace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#')
            continue;

        char *eq = strchr(trimmed, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key = trim_whitespace(trimmed);
        char *value = trim_whitespace(eq + 1);

        if (strcasecmp(key, "keywords") == 0) {
            def->keywords = split_csv(value, &def->keyword_count);
        } else if (strcasecmp(key, "types") == 0) {
            def->types = split_csv(value, &def->type_count);
        } else if (strcasecmp(key, "statements") == 0) {
            def->statements = split_csv(value, &def->statement_count);
        } else if (strcasecmp(key, "constants") == 0) {
            def->constants = split_csv(value, &def->constant_count);
        } else if (strcasecmp(key, "preprocessor") == 0) {
            def->preprocessor = split_csv(value, &def->preprocessor_count);
        } else if (strcasecmp(key, "extensions") == 0) {
            def->extensions = split_csv(value, &def->extension_count);
        } else if (strcasecmp(key, "line_comment") == 0) {
            def->line_comment = strdup(value);
        } else if (strcasecmp(key, "block_comment_start") == 0) {
            def->block_comment_start = strdup(value);
        } else if (strcasecmp(key, "block_comment_end") == 0) {
            def->block_comment_end = strdup(value);
        } else if (strcasecmp(key, "string_delim") == 0 && value[0]) {
            def->string_delim = value[0];
        } else if (strcasecmp(key, "char_delim") == 0 && value[0]) {
            def->char_delim = value[0];
        } else if (strcasecmp(key, "preprocessor_char") == 0 && value[0]) {
            def->preprocessor_char = value[0];
        } else if (strcasecmp(key, "color_keyword") == 0) {
            parse_color_rgb(value, def->color_keyword);
        } else if (strcasecmp(key, "color_type") == 0) {
            parse_color_rgb(value, def->color_type);
        } else if (strcasecmp(key, "color_string") == 0) {
            parse_color_rgb(value, def->color_string);
        } else if (strcasecmp(key, "color_comment") == 0) {
            parse_color_rgb(value, def->color_comment);
        } else if (strcasecmp(key, "color_number") == 0) {
            parse_color_rgb(value, def->color_number);
        } else if (strcasecmp(key, "color_preprocessor") == 0) {
            parse_color_rgb(value, def->color_preprocessor);
        } else if (strcasecmp(key, "color_operator") == 0) {
            parse_color_rgb(value, def->color_operator);
        } else if (strcasecmp(key, "color_function") == 0) {
            parse_color_rgb(value, def->color_function);
        }
    }

    fclose(fp);
    def->loaded = true;
    g_syntax_count++;
    return true;
}

static void load_directory(const char *path) {
    struct dirent **entries;
    int count = scandir(path, &entries, NULL, alphasort);
    if (count < 0)
        return;
    for (int i = 0; i < count; i++) {
        if (ends_with(entries[i]->d_name, ".cupidconf")) {
            char file[MAX_PATH_LENGTH + 256];
            snprintf(file, sizeof(file), "%s/%s", path, entries[i]->d_name);
            load_syntax_file(file);
        }
        free(entries[i]);
    }
    free(entries);
}

void syntax_init(void) {
    if (g_syntax_initialized)
        return;
    char path[MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/.cupidfm/syntax", get_home_dir());
    load_directory(path);
    const char *extra = getenv("CUPIDFM_SYNTAX_DIR");
    if (extra && *extra)
        load_directory(extra);
#ifdef __linux__
    char executable[MAX_PATH_LENGTH];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length > 0) {
        executable[length] = '\0';
        char *slash = strrchr(executable, '/');
        if (slash) {
            *slash = '\0';
            snprintf(path, sizeof(path), "%s/syntax", executable);
            load_directory(path);
        }
    }
#endif
    load_directory("/usr/local/share/cupidfm/syntax");
    load_directory("/usr/share/cupidfm/syntax");
    g_syntax_initialized = true;
}

// Clean up syntax highlighting resources
void syntax_cleanup(void) {
    for (size_t i = 0; i < g_syntax_count; i++) {
        syntax_def_free(&g_syntax_defs[i]);
    }
    g_syntax_count = 0;
    g_syntax_initialized = false;
}

// Get syntax definition for a file based on extension
SyntaxDef *syntax_get_for_file(const char *filename) {
    if (!filename || !g_syntax_initialized)
        return NULL;

    for (size_t i = 0; i < g_syntax_count; i++) {
        SyntaxDef *def = &g_syntax_defs[i];
        if (def->rules && strcmp(def->language, "unknown") &&
            syntax_rules_detect(def->rules, "filename", filename))
            return def;
        for (size_t j = 0; j < def->extension_count; j++) {
            if (ends_with(filename, def->extensions[j])) {
                return def;
            }
        }
    }

    int fd = open(filename, O_RDONLY | O_NONBLOCK);
    struct stat info;
    char content[65537];
    ssize_t length = fd >= 0 && !fstat(fd, &info) && S_ISREG(info.st_mode)
                         ? read(fd, content, sizeof(content) - 1)
                         : -1;
    if (fd >= 0)
        close(fd);
    if (length > 0) {
        content[length] = '\0';
        char *newline = strchr(content, '\n');
        char first[65537];
        size_t size = newline ? (size_t)(newline - content) : (size_t)length;
        memcpy(first, content, size);
        first[size] = '\0';
        for (size_t i = 0; i < g_syntax_count; i++)
            if (g_syntax_defs[i].rules &&
                syntax_rules_detect(g_syntax_defs[i].rules, "header", first))
                return &g_syntax_defs[i];
        for (size_t i = 0; i < g_syntax_count; i++)
            if (g_syntax_defs[i].rules &&
                syntax_rules_detect(g_syntax_defs[i].rules, "signature", content))
                return &g_syntax_defs[i];
    }
    return NULL;
}

// Helper: parse RGB color from string "R,G,B" or "#RRGGBB"
static bool parse_color_rgb(const char *value, int rgb[3]) {
    if (!value || !*value)
        return false;

    if (value[0] == '#') {
        // Hex format: #RRGGBB
        unsigned int r, g, b;
        if (sscanf(value + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            rgb[0] = r;
            rgb[1] = g;
            rgb[2] = b;
            return true;
        }
    } else {
        // Comma-separated: R,G,B
        if (sscanf(value, "%d,%d,%d", &rgb[0], &rgb[1], &rgb[2]) == 3) {
            return true;
        }
    }
    return false;
}

static void rule_palette(void);

void syntax_init_colors(void) {
    if (has_colors()) {
        use_default_colors();
        rule_palette();
    }
}

static int nearest_color(int r, int g, int b) {
#ifdef NCURSES_EXT_COLORS
    if (COLORS >= 16777216)
        return (r << 16) | (g << 8) | b;
#endif
    if (COLORS < 256) {
        const int ansi[][3] = {{0, 0, 0},       {128, 0, 0},   {0, 128, 0},   {128, 128, 0},
                               {0, 0, 128},     {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
                               {128, 128, 128}, {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                               {0, 0, 255},     {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
        int best = 0, distance = 1000000;
        for (int i = 0; i < COLORS && i < 16; i++) {
            int d = (ansi[i][0] - r) * (ansi[i][0] - r) + (ansi[i][1] - g) * (ansi[i][1] - g) +
                    (ansi[i][2] - b) * (ansi[i][2] - b);
            if (d < distance) {
                best = i;
                distance = d;
            }
        }
        return best;
    }
    int best = 16, distance = 1000000;
    for (int i = 16; i < 256; i++) {
        int n = i - 16, rr, gg, bb;
        if (i >= 232)
            rr = gg = bb = 8 + (i - 232) * 10;
        else {
            const int cube[] = {0, 95, 135, 175, 215, 255};
            rr = cube[n / 36];
            gg = cube[n / 6 % 6];
            bb = cube[n % 6];
        }
        int d = (rr - r) * (rr - r) + (gg - g) * (gg - g) + (bb - b) * (bb - b);
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return best;
}

static void rule_palette(void) {
    const struct {
        int pair, r, g, b;
    } palette[] = {
        {10, 249, 38, 114},  {11, 102, 217, 239}, {12, 230, 219, 116}, {13, 117, 113, 94},
        {14, 174, 129, 255}, {15, 249, 38, 114},  {16, 203, 75, 22},   {17, 102, 217, 239},
        {18, 211, 54, 130},  {19, 174, 129, 255}, {20, 189, 230, 173}, {21, 166, 226, 46},
        {22, 203, 75, 22},   {23, 211, 54, 130},  {24, 174, 129, 255}, {25, 211, 54, 130},
        {26, 80, 80, 80},    {43, 248, 248, 242}, {44, 248, 248, 242}, {45, 255, 175, 0}};

    short background = -1;
    for (size_t i = 0; i < sizeof(palette) / sizeof(*palette); i++) {
        if (palette[i].pair >= COLOR_PAIRS)
            continue;
        int foreground = nearest_color(palette[i].r, palette[i].g, palette[i].b);
#ifdef NCURSES_EXT_COLORS
        init_extended_pair(palette[i].pair, foreground, background);
#else
        init_pair(palette[i].pair, (short)foreground, background);
#endif
    }
    const int named[][3] = {{0, 0, 0},       {128, 0, 0},   {0, 128, 0},   {128, 128, 0},
                            {0, 0, 128},     {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
                            {128, 128, 128}, {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                            {0, 0, 255},     {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
    for (int i = 0; i < 16; i++) {
        if (27 + i >= COLOR_PAIRS)
            continue;
        int foreground = nearest_color(named[i][0], named[i][1], named[i][2]);
#ifdef NCURSES_EXT_COLORS
        init_extended_pair(27 + i, foreground, background);
#else
        init_pair(27 + i, (short)foreground, background);
#endif
    }
}

// Helper: check if a character is part of an identifier
static bool is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

// Helper: check if word matches keyword list
static bool is_keyword(const char *word, size_t len, char **keywords, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (strlen(keywords[i]) == len && strncmp(word, keywords[i], len) == 0) {
            return true;
        }
    }
    return false;
}

// Helper: check if identifier is all uppercase (constant style)
static bool is_uppercase_ident(const char *word, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = word[i];
        if (c == '_' || isdigit((unsigned char)c))
            continue;
        if (!isupper((unsigned char)c))
            return false;
    }
    return true;
}

// Helper: check if identifier looks like a type (ends with _t or _T)
static bool is_type_suffix(const char *word, size_t len) {
    if (len < 3)
        return false;
    return (word[len - 2] == '_' && (word[len - 1] == 't' || word[len - 1] == 'T'));
}

// Helper: check if character is an operator
static bool is_operator_char(char c) {
    return (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '=' || c == '!' ||
            c == '<' || c == '>' || c == '&' || c == '|' || c == '^' || c == '~' || c == '?' ||
            c == ':');
}

// Helper: check if next non-whitespace character is '('
static bool is_followed_by_paren(const char *line, int pos, int len) {
    while (pos < len && isspace((unsigned char)line[pos])) {
        pos++;
    }
    return (pos < len && line[pos] == '(');
}

/* Track comments through an entire physical line, including clipped columns. */
int syntax_scan_line_state(const char *line, const SyntaxDef *syntax, int state) {
    if (!line || !syntax)
        return 0;
    if (syntax->rules)
        return syntax_rules_line(syntax->rules, line, state, NULL);
    size_t opening = syntax->block_comment_start ? strlen(syntax->block_comment_start) : 0;
    size_t closing = syntax->block_comment_end ? strlen(syntax->block_comment_end) : 0;
    size_t comment = syntax->line_comment ? strlen(syntax->line_comment) : 0;
    char quote = 0;
    for (size_t i = 0; line[i];) {
        if (state) {
            if (closing && !strncmp(line + i, syntax->block_comment_end, closing)) {
                state = 0;
                i += closing;
            } else
                i++;
        } else if (quote) {
            if (line[i] == '\\' && line[i + 1])
                i += 2;
            else {
                if (line[i] == quote)
                    quote = 0;
                i++;
            }
        } else if (comment && !strncmp(line + i, syntax->line_comment, comment)) {
            break;
        } else if (opening && closing && !strncmp(line + i, syntax->block_comment_start, opening)) {
            state = 1;
            i += opening;
        } else {
            if ((syntax->string_delim && line[i] == syntax->string_delim) ||
                (syntax->char_delim && line[i] == syntax->char_delim))
                quote = line[i];
            i++;
        }
    }
    return state;
}

int get_initial_block_comment_state(char **lines, int num_lines, int current_line,
                                    SyntaxDef *syntax) {
    int state = 0;
    if (!lines)
        return state;
    for (int i = 0; i < current_line && i < num_lines; i++)
        state = syntax_scan_line_state(lines[i], syntax, state);
    return state;
}

// Helper: parse and highlight a number (supports various formats)
static int parse_number(WINDOW *win, const char *line, int pos, int len, int y, int *col,
                        int max_x) {
    int start = pos;

    // Check for 0x (hex), 0b (binary), or 0 (octal)
    if (line[pos] == '0' && pos + 1 < len) {
        if (line[pos + 1] == 'x' || line[pos + 1] == 'X') {
            // Hexadecimal
            pos += 2;
            while (pos < len &&
                   (isxdigit((unsigned char)line[pos]) || line[pos] == '.' || line[pos] == 'p' ||
                    line[pos] == 'P' || line[pos] == '+' || line[pos] == '-')) {
                pos++;
            }
        } else if (line[pos + 1] == 'b' || line[pos + 1] == 'B') {
            // Binary
            pos += 2;
            while (pos < len && (line[pos] == '0' || line[pos] == '1')) {
                pos++;
            }
        } else if (isdigit((unsigned char)line[pos + 1])) {
            // Octal
            pos++;
            while (pos < len && (line[pos] >= '0' && line[pos] <= '7')) {
                pos++;
            }
        }
    }

    // Decimal or floating point
    if (pos == start) {
        while (pos < len && isdigit((unsigned char)line[pos])) {
            pos++;
        }

        // Check for decimal point
        if (pos < len && line[pos] == '.') {
            pos++;
            while (pos < len && isdigit((unsigned char)line[pos])) {
                pos++;
            }
        }

        // Check for exponent (e or E)
        if (pos < len && (line[pos] == 'e' || line[pos] == 'E')) {
            pos++;
            if (pos < len && (line[pos] == '+' || line[pos] == '-')) {
                pos++;
            }
            while (pos < len && isdigit((unsigned char)line[pos])) {
                pos++;
            }
        }
    }

    // Parse suffixes (U, L, LL, F, etc.)
    while (pos < len) {
        char c = line[pos];
        if (c == 'u' || c == 'U' || c == 'l' || c == 'L' || c == 'f' || c == 'F') {
            pos++;
        } else {
            break;
        }
    }

    // Print the number
    wattron(win, COLOR_PAIR(COLOR_SYNTAX_NUMBER));
    for (int i = start; i < pos && *col < max_x; i++) {
        mvwaddch(win, y, (*col)++, line[i]);
    }
    wattroff(win, COLOR_PAIR(COLOR_SYNTAX_NUMBER));

    return pos;
}

// Apply syntax highlighting to a line of text
void syntax_highlight_line(WINDOW *win, const char *line, SyntaxDef *syntax, int *in_block_comment,
                           int y, int x, int max_width, char **lines, int num_lines,
                           int line_index) {
    if (!win || !line)
        return;
    (void)lines;
    (void)num_lines;
    (void)line_index;

    // No syntax highlighting available - just print the line normally
    if (!syntax) {
        mvwprintw(win, y, x, "%.*s", max_width, line);
        return;
    }

    if (syntax->rules) {
        size_t length = strlen(line);
        unsigned char *colors = g_malloc(length + 1);
        int next = syntax_rules_line(syntax->rules, line, in_block_comment ? *in_block_comment : 0,
                                     colors);
        int column = 0;
        for (size_t i = 0; i < length && column < max_width;) {
            gunichar ch = g_utf8_get_char_validated(line + i, length - i);
            size_t bytes = ch == (gunichar)-1 || ch == (gunichar)-2
                               ? 1
                               : (size_t)(g_utf8_next_char(line + i) - (line + i));
            int width = ch == '\t'                  ? 8 - column % 8
                        : g_unichar_iszerowidth(ch) ? 0
                        : g_unichar_iswide(ch)      ? 2
                                                    : 1;
            if (column + width > max_width || ch == '\n' || ch == '\r')
                break;
            wattrset(win,
                     COLOR_PAIR(colors[i]) |
                         ((colors[i] == 22 || colors[i] == 23 || colors[i] == 43) ? A_BOLD : 0));
            if (ch == '\t')
                for (int j = 0; j < width; j++)
                    mvwaddch(win, y, x + column + j, ' ');
            else if (ch == (gunichar)-1 || ch == (gunichar)-2 || g_unichar_iscntrl(ch))
                mvwaddch(win, y, x + column, '?');
            else
                mvwaddnstr(win, y, x + column, line + i, (int)bytes);
            column += width;
            i += bytes;
        }
        wattrset(win, A_NORMAL);
        g_free(colors);
        if (in_block_comment)
            *in_block_comment = next;
        return;
    }

    int pos = 0;
    int col = x;
    size_t len = strlen(line);
    bool in_string = false;
    bool in_char = false;
    bool in_line_comment = false;
    int max_x = x + max_width;

    while (pos < (int)len && col < max_x) {
        // Check for block comment continuation
        if (in_block_comment && *in_block_comment) {
            if (syntax->block_comment_end && strncmp(&line[pos], syntax->block_comment_end,
                                                     strlen(syntax->block_comment_end)) == 0) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_COMMENT));
                for (size_t i = 0; i < strlen(syntax->block_comment_end) && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[pos++]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_COMMENT));
                *in_block_comment = 0;
                continue;
            }
            wattron(win, COLOR_PAIR(COLOR_SYNTAX_COMMENT));
            mvwaddch(win, y, col++, line[pos++]);
            wattroff(win, COLOR_PAIR(COLOR_SYNTAX_COMMENT));
            continue;
        }

        // Check for line comment start
        if (!in_string && !in_char && syntax->line_comment &&
            strncmp(&line[pos], syntax->line_comment, strlen(syntax->line_comment)) == 0) {
            in_line_comment = true;
        }

        // Check for block comment start
        if (!in_string && !in_char && !in_line_comment && syntax->block_comment_start &&
            strncmp(&line[pos], syntax->block_comment_start, strlen(syntax->block_comment_start)) ==
                0) {
            *in_block_comment = 1;
            wattron(win, COLOR_PAIR(COLOR_SYNTAX_COMMENT));
            for (size_t i = 0; i < strlen(syntax->block_comment_start) && col < max_x; i++) {
                mvwaddch(win, y, col++, line[pos++]);
            }
            wattroff(win, COLOR_PAIR(COLOR_SYNTAX_COMMENT));
            continue;
        }

        // Line comment - color rest of line
        if (in_line_comment) {
            wattron(win, COLOR_PAIR(COLOR_SYNTAX_COMMENT));
            mvwaddch(win, y, col++, line[pos++]);
            wattroff(win, COLOR_PAIR(COLOR_SYNTAX_COMMENT));
            continue;
        }

        // Check for preprocessor (e.g., #include, #define)
        if (!in_string && !in_char && syntax->preprocessor_char) {
            // Skip leading whitespace to check for preprocessor
            int check_pos = 0;
            while (check_pos < pos && isspace((unsigned char)line[check_pos])) {
                check_pos++;
            }
            if (check_pos == pos && line[pos] == syntax->preprocessor_char) {
                // Highlight the # symbol
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_PREPROCESSOR));
                mvwaddch(win, y, col++, line[pos++]);

                // Skip whitespace after #
                while (pos < (int)len && isspace((unsigned char)line[pos]) && col < max_x) {
                    mvwaddch(win, y, col++, line[pos++]);
                }

                // Highlight the directive keyword (define, include, ifndef, etc.)
                int directive_start = pos;
                while (pos < (int)len && is_ident_char(line[pos])) {
                    pos++;
                }
                for (int i = directive_start; i < pos && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_PREPROCESSOR));

                // For simple directives like #define MACRO, #ifndef MACRO, highlight the macro name
                // Skip whitespace
                while (pos < (int)len && isspace((unsigned char)line[pos]) && col < max_x) {
                    mvwaddch(win, y, col++, line[pos++]);
                }

                // Check if next token is an identifier (macro name)
                if (pos < (int)len && is_ident_char(line[pos])) {
                    int macro_start = pos;
                    while (pos < (int)len && is_ident_char(line[pos])) {
                        pos++;
                    }
                    int macro_len = pos - macro_start;

                    // Highlight macro name as constant if uppercase
                    if (is_uppercase_ident(&line[macro_start], macro_len)) {
                        wattron(win, COLOR_PAIR(COLOR_SYNTAX_NUMBER) | A_BOLD);
                        for (int i = macro_start; i < pos && col < max_x; i++) {
                            mvwaddch(win, y, col++, line[i]);
                        }
                        wattroff(win, COLOR_PAIR(COLOR_SYNTAX_NUMBER) | A_BOLD);
                    } else {
                        // Regular macro name
                        for (int i = macro_start; i < pos && col < max_x; i++) {
                            mvwaddch(win, y, col++, line[i]);
                        }
                    }
                }

                // Rest of the line (macro definition, include path, etc.) - normal color
                while (pos < (int)len && col < max_x) {
                    mvwaddch(win, y, col++, line[pos++]);
                }
                break;
            }
        }

        // String handling
        if (syntax->string_delim && line[pos] == syntax->string_delim && !in_char) {
            if (!in_string) {
                in_string = true;
            } else if (pos > 0 && line[pos - 1] != '\\') {
                in_string = false;
            }
            wattron(win, COLOR_PAIR(COLOR_SYNTAX_STRING));
            mvwaddch(win, y, col++, line[pos++]);
            wattroff(win, COLOR_PAIR(COLOR_SYNTAX_STRING));
            continue;
        }

        // Character literal handling
        if (syntax->char_delim && line[pos] == syntax->char_delim && !in_string) {
            if (!in_char) {
                in_char = true;
            } else if (pos > 0 && line[pos - 1] != '\\') {
                in_char = false;
            }
            wattron(win, COLOR_PAIR(COLOR_SYNTAX_STRING));
            mvwaddch(win, y, col++, line[pos++]);
            wattroff(win, COLOR_PAIR(COLOR_SYNTAX_STRING));
            continue;
        }

        // Inside string or char - handle escape sequences
        if (in_string || in_char) {
            if (line[pos] == '\\' && pos + 1 < (int)len) {
                // Highlight escape sequence
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_ESCAPE) | A_BOLD);
                mvwaddch(win, y, col++, line[pos++]);
                // Handle different escape types
                if (line[pos] == 'x' || line[pos] == 'u' || line[pos] == 'U') {
                    // Hex escape \xHH, unicode \uHHHH, \UHHHHHHHH
                    char esc_type = line[pos];
                    mvwaddch(win, y, col++, line[pos++]);
                    int max_digits = (esc_type == 'x') ? 2 : (esc_type == 'u') ? 4 : 8;
                    for (int i = 0; i < max_digits && pos < (int)len &&
                                    isxdigit((unsigned char)line[pos]) && col < max_x;
                         i++) {
                        mvwaddch(win, y, col++, line[pos++]);
                    }
                } else if (line[pos] >= '0' && line[pos] <= '7') {
                    // Octal escape \ooo
                    for (int i = 0; i < 3 && pos < (int)len && line[pos] >= '0' &&
                                    line[pos] <= '7' && col < max_x;
                         i++) {
                        mvwaddch(win, y, col++, line[pos++]);
                    }
                } else {
                    // Single char escape (\n, \t, etc.)
                    mvwaddch(win, y, col++, line[pos++]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_ESCAPE) | A_BOLD);
            } else {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_STRING));
                mvwaddch(win, y, col++, line[pos++]);
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_STRING));
            }
            continue;
        }

        // Check for numbers (improved parsing)
        if (isdigit((unsigned char)line[pos]) && (pos == 0 || !is_ident_char(line[pos - 1]))) {
            pos = parse_number(win, line, pos, len, y, &col, max_x);
            continue;
        }

        // Check for identifiers (keywords, types, constants, etc.)
        if (is_ident_char(line[pos])) {
            int word_start = pos;
            while (pos < (int)len && is_ident_char(line[pos])) {
                pos++;
            }
            int word_len = pos - word_start;

            // Check for labels (identifier followed by colon at line start)
            bool is_label = false;
            if (pos < (int)len && line[pos] == ':') {
                // Check if this is at the start of the line (after whitespace)
                int check = 0;
                while (check < word_start && isspace((unsigned char)line[check])) {
                    check++;
                }
                if (check == word_start) {
                    is_label = true;
                }
            }

            // Check for typedef type name (after closing brace and before semicolon)
            bool is_typedef_name = false;
            if (word_start > 0) {
                int check = word_start - 1;
                while (check >= 0 && isspace((unsigned char)line[check])) {
                    check--;
                }
                if (check >= 0 && line[check] == '}') {
                    // Check if semicolon follows the identifier
                    int after = pos;
                    while (after < (int)len && isspace((unsigned char)line[after])) {
                        after++;
                    }
                    if (after < (int)len && line[after] == ';') {
                        is_typedef_name = true;
                    }
                }
            }

            // Check if it's a constant (true, false, NULL, etc.)
            if (syntax->constants && is_keyword(&line[word_start], word_len, syntax->constants,
                                                syntax->constant_count)) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_NUMBER) | A_BOLD);
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_NUMBER) | A_BOLD);
            }
            // Check if it's a control flow keyword
            else if (syntax->keywords && is_keyword(&line[word_start], word_len, syntax->keywords,
                                                    syntax->keyword_count)) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_KEYWORD) | A_BOLD);
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_KEYWORD) | A_BOLD);
            }
            // Check if it's a statement keyword (storage class, qualifiers)
            else if (syntax->statements &&
                     is_keyword(&line[word_start], word_len, syntax->statements,
                                syntax->statement_count)) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_KEYWORD));
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_KEYWORD));
            }
            // Check if it's a type
            else if (syntax->types &&
                     is_keyword(&line[word_start], word_len, syntax->types, syntax->type_count)) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_TYPE));
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_TYPE));
            }
            // Check if it's a label
            else if (is_label) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_LABEL) | A_BOLD);
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_LABEL) | A_BOLD);
            }
            // Check if it's a typedef type name (after } before ;)
            else if (is_typedef_name) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_TYPE) | A_BOLD);
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_TYPE) | A_BOLD);
            }
            // Check if it's a function call (followed by '(')
            else if (is_followed_by_paren(&line[0], pos, len)) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_FUNCTION));
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_FUNCTION));
            }
            // Check if it's an uppercase identifier (constant style)
            else if (is_uppercase_ident(&line[word_start], word_len) && word_len >= 2) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_NUMBER));
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_NUMBER));
            }
            // Check if it has type suffix (_t or _T)
            else if (is_type_suffix(&line[word_start], word_len)) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_TYPE));
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_TYPE));
            }
            // Regular identifier
            else {
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
            }
            continue;
        }

        // Check for operators
        if (is_operator_char(line[pos])) {
            wattron(win, COLOR_PAIR(COLOR_SYNTAX_OPERATOR));
            // Handle multi-character operators
            while (pos < (int)len && is_operator_char(line[pos]) && col < max_x) {
                mvwaddch(win, y, col++, line[pos++]);
            }
            wattroff(win, COLOR_PAIR(COLOR_SYNTAX_OPERATOR));
            continue;
        }

        // Regular character (brackets, semicolons, etc.)
        mvwaddch(win, y, col++, line[pos++]);
    }
}
