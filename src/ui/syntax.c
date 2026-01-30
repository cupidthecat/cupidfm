// syntax.c - Syntax highlighting system for CupidFM text editor
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "syntax.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "../lib/cupidconf.h"
#include "globals.h"

#define MAX_SYNTAX_DEFS 50
#define MAX_LINE_LENGTH 1024

static SyntaxDef g_syntax_defs[MAX_SYNTAX_DEFS];
static size_t g_syntax_count = 0;
static bool g_syntax_initialized = false;

// Store original color values to restore on exit
static bool g_colors_changed = false;
static short g_original_colors[8][3]; // Store RGB for colors 8-15

// Forward declarations
static bool parse_color_rgb(const char *value, int rgb[3]);

// Helper: get home directory
static const char *get_home_dir(void) {
    const char *home = getenv("HOME");
    return home ? home : "/tmp";
}

// Helper: trim whitespace from both ends
static char *trim_whitespace(char *str) {
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return str;
}

// Helper: check if string ends with suffix
static bool ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return false;
    return strcasecmp(str + str_len - suffix_len, suffix) == 0;
}

// Helper: split comma-separated values into array
static char **split_csv(const char *value, size_t *count) {
    if (!value || !count) return NULL;
    
    *count = 0;
    char *copy = strdup(value);
    if (!copy) return NULL;
    
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
    if (!def) return;
    
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
    if (g_syntax_count >= MAX_SYNTAX_DEFS) return false;
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return false;
    
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
    if (dot) *dot = '\0';
    def->language = strdup(lang_name);
    
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim_whitespace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;
        
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;
        
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

// Initialize syntax highlighting system
void syntax_init(void) {
    if (g_syntax_initialized) return;
    
    char syntax_dir[MAX_PATH_LENGTH];
    snprintf(syntax_dir, sizeof(syntax_dir), "%s/.cupidfm/syntax", get_home_dir());
    
    DIR *dir = opendir(syntax_dir);
    if (!dir) {
        g_syntax_initialized = true;
        return; // No syntax directory, that's okay
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!ends_with(entry->d_name, ".cupidconf")) continue;
        
        char filepath[MAX_PATH_LENGTH];
        snprintf(filepath, sizeof(filepath), "%s/%s", syntax_dir, entry->d_name);
        load_syntax_file(filepath);
    }
    
    closedir(dir);
    g_syntax_initialized = true;
}

// Clean up syntax highlighting resources
void syntax_cleanup(void) {
    // Restore original terminal colors if we changed them
    if (g_colors_changed && can_change_color()) {
        for (int i = 0; i < 8; i++) {
            init_color(COLOR_MONOKAI_ORANGE + i,
                      g_original_colors[i][0],
                      g_original_colors[i][1],
                      g_original_colors[i][2]);
        }
        g_colors_changed = false;
    }
    
    for (size_t i = 0; i < g_syntax_count; i++) {
        syntax_def_free(&g_syntax_defs[i]);
    }
    g_syntax_count = 0;
    g_syntax_initialized = false;
}

// Get syntax definition for a file based on extension
SyntaxDef *syntax_get_for_file(const char *filename) {
    if (!filename || !g_syntax_initialized) return NULL;
    
    for (size_t i = 0; i < g_syntax_count; i++) {
        SyntaxDef *def = &g_syntax_defs[i];
        for (size_t j = 0; j < def->extension_count; j++) {
            if (ends_with(filename, def->extensions[j])) {
                return def;
            }
        }
    }
    
    return NULL;
}

// Helper: parse RGB color from string "R,G,B" or "#RRGGBB"
static bool parse_color_rgb(const char *value, int rgb[3]) {
    if (!value || !*value) return false;
    
    if (value[0] == '#') {
        // Hex format: #RRGGBB
        unsigned int r, g, b;
        if (sscanf(value + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            rgb[0] = r; rgb[1] = g; rgb[2] = b;
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

// Helper: convert RGB (0-255) to ncurses color value (0-1000)
static short rgb_to_ncurses(int rgb_value) {
    return (short)((rgb_value * 1000) / 255);
}

// Initialize ncurses color pairs for syntax highlighting
void syntax_init_colors(void) {
    if (!has_colors()) return;
    
    // Check if we can define custom colors
    if (can_change_color()) {
        // Save original color values so we can restore them on exit
        for (int i = 0; i < 8; i++) {
            color_content(COLOR_MONOKAI_ORANGE + i, 
                         &g_original_colors[i][0],
                         &g_original_colors[i][1],
                         &g_original_colors[i][2]);
        }
        g_colors_changed = true;
        
        // Define Monokai colors
        // Orange: (232, 125, 62) for keywords
        init_color(COLOR_MONOKAI_ORANGE, rgb_to_ncurses(232), rgb_to_ncurses(125), rgb_to_ncurses(62));
        // Green: (180, 210, 115) for types
        init_color(COLOR_MONOKAI_GREEN, rgb_to_ncurses(180), rgb_to_ncurses(210), rgb_to_ncurses(115));
        // Yellow: (229, 181, 103) for strings
        init_color(COLOR_MONOKAI_YELLOW, rgb_to_ncurses(229), rgb_to_ncurses(181), rgb_to_ncurses(103));
        // Purple: (158, 134, 200) for numbers/constants
        init_color(COLOR_MONOKAI_PURPLE, rgb_to_ncurses(158), rgb_to_ncurses(134), rgb_to_ncurses(200));
        // Pink: (176, 82, 121) for preprocessor
        init_color(COLOR_MONOKAI_PINK, rgb_to_ncurses(176), rgb_to_ncurses(82), rgb_to_ncurses(121));
        // Blue: (108, 153, 187) for functions
        init_color(COLOR_MONOKAI_BLUE, rgb_to_ncurses(108), rgb_to_ncurses(153), rgb_to_ncurses(187));
        // Gray: (121, 121, 121) for comments
        init_color(COLOR_MONOKAI_GRAY, rgb_to_ncurses(121), rgb_to_ncurses(121), rgb_to_ncurses(121));
        
        // Initialize color pairs with Monokai theme
        init_pair(COLOR_SYNTAX_KEYWORD, COLOR_MONOKAI_ORANGE, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_TYPE, COLOR_MONOKAI_GREEN, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_STRING, COLOR_MONOKAI_YELLOW, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_COMMENT, COLOR_MONOKAI_GRAY, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_NUMBER, COLOR_MONOKAI_PURPLE, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_OPERATOR, COLOR_MONOKAI_ORANGE, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_PREPROCESSOR, COLOR_MONOKAI_PINK, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_FUNCTION, COLOR_MONOKAI_BLUE, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_LABEL, COLOR_MONOKAI_PINK, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_ESCAPE, COLOR_MONOKAI_PURPLE, COLOR_BLACK);
    } else {
        // Fallback to basic 8 colors if custom colors not supported
        init_pair(COLOR_SYNTAX_KEYWORD, COLOR_YELLOW, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_TYPE, COLOR_GREEN, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_STRING, COLOR_YELLOW, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_COMMENT, COLOR_BLUE, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_NUMBER, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_OPERATOR, COLOR_RED, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_PREPROCESSOR, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_FUNCTION, COLOR_CYAN, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_LABEL, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(COLOR_SYNTAX_ESCAPE, COLOR_MAGENTA, COLOR_BLACK);
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
        if (c == '_' || isdigit((unsigned char)c)) continue;
        if (!isupper((unsigned char)c)) return false;
    }
    return true;
}

// Helper: check if identifier looks like a type (ends with _t or _T)
static bool is_type_suffix(const char *word, size_t len) {
    if (len < 3) return false;
    return (word[len-2] == '_' && (word[len-1] == 't' || word[len-1] == 'T'));
}

// Helper: check if character is an operator
static bool is_operator_char(char c) {
    return (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
            c == '=' || c == '!' || c == '<' || c == '>' || c == '&' ||
            c == '|' || c == '^' || c == '~' || c == '?' || c == ':');
}

// Helper: check if next non-whitespace character is '('
static bool is_followed_by_paren(const char *line, int pos, int len) {
    while (pos < len && isspace((unsigned char)line[pos])) {
        pos++;
    }
    return (pos < len && line[pos] == '(');
}

// Helper: scan backwards through lines to determine initial block comment state
// Returns 1 if we're inside a block comment at the start of the given line, 0 otherwise
int get_initial_block_comment_state(char **lines, int num_lines, int current_line, SyntaxDef *syntax) {
    if (!syntax || !syntax->block_comment_start || !syntax->block_comment_end) return 0;

    const char *start_delim = syntax->block_comment_start;
    const char *end_delim   = syntax->block_comment_end;
    const int start_len = (int)strlen(start_delim);
    const int end_len   = (int)strlen(end_delim);

    int depth = 0;

    // Walk upward through earlier lines
    for (int li = current_line - 1; li >= 0; --li) {
        const char *s = lines[li];
        if (!s) continue;
        int L = (int)strlen(s);

        // Scan this line right-to-left
        for (int pos = L - 1; pos >= 0; --pos) {
            // Check for end delimiter "*/" ending at pos
            if (pos - end_len + 1 >= 0 &&
                strncmp(&s[pos - end_len + 1], end_delim, (size_t)end_len) == 0) {
                depth++;                 // treat end as an "open" when scanning backwards
                pos -= (end_len - 1);
                continue;
            }

            // Check for start delimiter "/*" ending at pos
            if (pos - start_len + 1 >= 0 &&
                strncmp(&s[pos - start_len + 1], start_delim, (size_t)start_len) == 0) {

                if (depth == 0) {
                    // Found an unmatched start => we are inside a block comment at current_line
                    return 1;
                }
                depth--;                 // match it against an end we saw later (above current_line)
                pos -= (start_len - 1);
                continue;
            }
        }
    }

    return 0;
}

// Helper: parse and highlight a number (supports various formats)
static int parse_number(WINDOW *win, const char *line, int pos, int len, int y, int *col, int max_x) {
    int start = pos;
    
    // Check for 0x (hex), 0b (binary), or 0 (octal)
    if (line[pos] == '0' && pos + 1 < len) {
        if (line[pos + 1] == 'x' || line[pos + 1] == 'X') {
            // Hexadecimal
            pos += 2;
            while (pos < len && (isxdigit((unsigned char)line[pos]) || line[pos] == '.' || 
                   line[pos] == 'p' || line[pos] == 'P' || line[pos] == '+' || line[pos] == '-')) {
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
void syntax_highlight_line(WINDOW *win, const char *line, SyntaxDef *syntax,
                          int *in_block_comment, int y, int x, int max_width,
                          char **lines, int num_lines, int line_index) {
    if (!win || !line) return;
    
    // No syntax highlighting available - just print the line normally
    if (!syntax) {
        mvwprintw(win, y, x, "%.*s", max_width, line);
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
            if (syntax->block_comment_end &&
                strncmp(&line[pos], syntax->block_comment_end, strlen(syntax->block_comment_end)) == 0) {
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
            strncmp(&line[pos], syntax->block_comment_start, strlen(syntax->block_comment_start)) == 0) {
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
                         isxdigit((unsigned char)line[pos]) && col < max_x; i++) {
                        mvwaddch(win, y, col++, line[pos++]);
                    }
                } else if (line[pos] >= '0' && line[pos] <= '7') {
                    // Octal escape \ooo
                    for (int i = 0; i < 3 && pos < (int)len && 
                         line[pos] >= '0' && line[pos] <= '7' && col < max_x; i++) {
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
        if (isdigit((unsigned char)line[pos]) && 
            (pos == 0 || !is_ident_char(line[pos - 1]))) {
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
            if (syntax->constants && is_keyword(&line[word_start], word_len, 
                                               syntax->constants, syntax->constant_count)) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_NUMBER) | A_BOLD);
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_NUMBER) | A_BOLD);
            }
            // Check if it's a control flow keyword
            else if (syntax->keywords && is_keyword(&line[word_start], word_len, 
                                               syntax->keywords, syntax->keyword_count)) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_KEYWORD) | A_BOLD);
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_KEYWORD) | A_BOLD);
            }
            // Check if it's a statement keyword (storage class, qualifiers)
            else if (syntax->statements && is_keyword(&line[word_start], word_len,
                                                     syntax->statements, syntax->statement_count)) {
                wattron(win, COLOR_PAIR(COLOR_SYNTAX_KEYWORD));
                for (int i = 0; i < word_len && col < max_x; i++) {
                    mvwaddch(win, y, col++, line[word_start + i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_SYNTAX_KEYWORD));
            }
            // Check if it's a type
            else if (syntax->types && is_keyword(&line[word_start], word_len,
                                                 syntax->types, syntax->type_count)) {
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
            int op_start = pos;
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
