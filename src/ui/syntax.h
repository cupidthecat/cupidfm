#ifndef SYNTAX_H
#define SYNTAX_H

#include <ncurses.h>
#include <stdbool.h>
#include <stddef.h>

// Maximum number of patterns per category
#define SYNTAX_MAX_KEYWORDS 200
#define SYNTAX_MAX_TYPES 100
#define SYNTAX_MAX_PATTERNS 50

// Syntax highlighting color pair IDs
#define COLOR_SYNTAX_KEYWORD 10
#define COLOR_SYNTAX_TYPE 11
#define COLOR_SYNTAX_STRING 12
#define COLOR_SYNTAX_COMMENT 13
#define COLOR_SYNTAX_NUMBER 14
#define COLOR_SYNTAX_OPERATOR 15
#define COLOR_SYNTAX_PREPROCESSOR 16
#define COLOR_SYNTAX_FUNCTION 17
#define COLOR_SYNTAX_LABEL 18
#define COLOR_SYNTAX_ESCAPE 19

// Custom color indices for Monokai theme (8-15 are typically available)
#define COLOR_MONOKAI_ORANGE 8
#define COLOR_MONOKAI_GREEN 9
#define COLOR_MONOKAI_YELLOW 10
#define COLOR_MONOKAI_PURPLE 11
#define COLOR_MONOKAI_PINK 12
#define COLOR_MONOKAI_BLUE 13
#define COLOR_MONOKAI_GRAY 14
#define COLOR_MONOKAI_WHITE 15

// Syntax highlighting rule set for a language
typedef struct {
    char *language;       // Language name (e.g., "c", "python")
    char **keywords;      // Array of keyword strings (control flow)
    size_t keyword_count;
    char **types;         // Array of type keywords
    size_t type_count;
    char **statements;    // Array of statement keywords (storage class, qualifiers)
    size_t statement_count;
    char **constants;     // Array of constant keywords (true, false, NULL, etc.)
    size_t constant_count;
    char **preprocessor;  // Array of preprocessor directive names
    size_t preprocessor_count;
    
    // Comment delimiters
    char *line_comment;   // e.g., "//" for C
    char *block_comment_start;  // e.g., "/*"
    char *block_comment_end;    // e.g., "*/"
    
    // String delimiters
    char string_delim;    // e.g., '"' for double-quoted strings
    char char_delim;      // e.g., '\'' for character literals
    
    // Preprocessor
    char preprocessor_char; // e.g., '#' for C preprocessor
    
    // File extensions
    char **extensions;    // Array of file extensions (e.g., ".c", ".h")
    size_t extension_count;
    
    // Custom colors (RGB 0-255, -1 if not set)
    int color_keyword[3];      // RGB for keywords
    int color_type[3];         // RGB for types
    int color_string[3];       // RGB for strings
    int color_comment[3];      // RGB for comments
    int color_number[3];       // RGB for numbers/constants
    int color_preprocessor[3]; // RGB for preprocessor
    int color_operator[3];     // RGB for operators
    int color_function[3];     // RGB for function calls
    
    bool loaded;          // Whether this syntax definition is loaded
} SyntaxDef;

// Initialize syntax highlighting system and load all syntax definitions
void syntax_init(void);

// Clean up syntax highlighting resources
void syntax_cleanup(void);

// Get syntax definition for a file based on extension
SyntaxDef *syntax_get_for_file(const char *filename);

// Apply syntax highlighting to a line of text and print it to window
// lines: array of all lines in the file (can be NULL for single-line mode)
// num_lines: total number of lines in the file (0 for single-line mode)
// line_index: index of current line being highlighted (0 for single-line mode)
void syntax_highlight_line(WINDOW *win, const char *line, SyntaxDef *syntax, 
                          int *in_block_comment, int y, int x, int max_width,
                          char **lines, int num_lines, int line_index);

// Get initial block comment state by scanning backwards from current_line
int get_initial_block_comment_state(char **lines, int num_lines, int current_line, SyntaxDef *syntax);

// Initialize ncurses color pairs for syntax highlighting
void syntax_init_colors(void);

#endif // SYNTAX_H
