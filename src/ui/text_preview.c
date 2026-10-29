#define _POSIX_C_SOURCE 200809L
#include "text_preview.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    size_t line;
    off_t offset;
    int state;
} LineCheckpoint;
static struct {
    bool valid, has_syntax;
    struct stat identity;
    uint64_t syntax;
    size_t count, interval;
    LineCheckpoint points[2048];
} line_index;

void text_preview_clear_index(void) { line_index.valid = false; }

static uint64_t syntax_identity(const SyntaxDef *syntax) {
    uint64_t hash = 1469598103934665603ULL;
    if (!syntax) return 0;
    const char *parts[] = {syntax->block_comment_start, syntax->block_comment_end, syntax->line_comment};
    for (size_t i = 0; i < 3; i++) {
        const unsigned char *p = (const unsigned char *)parts[i];
        if (p) while (*p) {hash ^= *p++; hash *= 1099511628211ULL;}
        hash ^= 255; hash *= 1099511628211ULL;
    }
    hash ^= (unsigned char)syntax->string_delim; hash *= 1099511628211ULL;
    return hash ^ (unsigned char)syntax->char_delim;
}
static void reset_index(void) {
    line_index.count = 1;
    line_index.interval = 128;
    line_index.points[0] = (LineCheckpoint){0, 0, 0};
}
static bool prepare_index(FILE *stream, const SyntaxDef *syntax, bool set_syntax) {
    int fd = fileno(stream);
    struct stat st;
    struct timespec now;
    if (fd < 0 || ftello(stream) != 0 || fstat(fd, &st) || !S_ISREG(st.st_mode)) return false;
    if (clock_gettime(CLOCK_REALTIME, &now) || st.st_mtim.tv_sec >= now.tv_sec - 1 ||
        st.st_ctim.tv_sec >= now.tv_sec - 1) {
        line_index.valid = false;
        return false;
    }
    const struct stat *old = &line_index.identity;
    if (!line_index.valid || old->st_dev != st.st_dev || old->st_ino != st.st_ino ||
        old->st_size != st.st_size || old->st_mode != st.st_mode ||
        old->st_mtim.tv_sec != st.st_mtim.tv_sec || old->st_mtim.tv_nsec != st.st_mtim.tv_nsec ||
        old->st_ctim.tv_sec != st.st_ctim.tv_sec || old->st_ctim.tv_nsec != st.st_ctim.tv_nsec) {
        line_index.valid = true;
        line_index.has_syntax = false;
        line_index.identity = st;
        reset_index();
    }
    if (set_syntax) {
        uint64_t identity = syntax_identity(syntax);
        if (!line_index.has_syntax || line_index.syntax != identity) {
            line_index.syntax = identity;
            line_index.has_syntax = true;
            reset_index();
        }
    }
    return true;
}
static bool seek_index(FILE *stream, size_t wanted, size_t *line, int *state) {
    size_t lo = 0, hi = line_index.count;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (line_index.points[mid].line <= wanted) lo = mid;
        else hi = mid;
    }
    LineCheckpoint point = line_index.points[lo];
    if (fseeko(stream, point.offset, SEEK_SET)) return false;
    *line = point.line;
    *state = point.state;
    return true;
}
static void record_index(FILE *stream, size_t line, int state) {
    if (line <= line_index.points[line_index.count - 1].line || line % line_index.interval) return;
    if (line_index.count == sizeof(line_index.points) / sizeof(*line_index.points)) {
        if (line_index.interval > SIZE_MAX / 2) return;
        for (size_t i = 0; i < line_index.count / 2; i++) line_index.points[i] = line_index.points[i * 2];
        line_index.count /= 2;
        line_index.interval *= 2;
        if (line % line_index.interval) return;
    }
    off_t offset = ftello(stream);
    if (offset >= 0) line_index.points[line_index.count++] = (LineCheckpoint){line, offset, state};
}

int text_preview_count_lines(FILE *stream, int limit) {
    if (!stream || limit <= 0) return 0;
    char *line = NULL;
    size_t capacity = 0;
    int count = 0;
    if (prepare_index(stream, NULL, false)) {
        size_t position = 0; int state = 0;
        if (seek_index(stream, (size_t)limit, &position, &state)) count = (int)position;
    }
    while (count < limit && getline(&line, &capacity, stream) >= 0) count++;
    free(line);
    return count;
}

void draw_text_preview(WINDOW *window, FILE *stream, SyntaxDef *syntax,
                       int start_line, int max_y, int max_x) {
    if (!window || !stream || max_y <= 8 || max_x <= 4) return;
    size_t first = start_line > 0 ? (size_t)start_line : 0;
    size_t index = 0;
    int row = 7, state = 0;
    bool indexed = prepare_index(stream, syntax, true);
    if (indexed && !seek_index(stream, first, &index, &state)) indexed = false;
    char *line = NULL;
    size_t capacity = 0;
    while (row < max_y - 1) {
        ssize_t length = getline(&line, &capacity, stream);
        if (length < 0) {
            if (!feof(stream)) {
                mvwprintw(window, row, 2, "Unable to read file for preview");
            } else {
                mvwprintw(window, row++, 2, "--------------------------------");
                if (row < max_y - 1) mvwprintw(window, row, 2, "[End of file]");
            }
            break;
        }
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = 0;
        /* Retain physical lines, replacing terminal control bytes before display. */
        for (ssize_t i = 0; i < length; i++) {
            unsigned char c = (unsigned char)line[i];
            if (!isprint(c)) line[i] = ' ';
        }
        int next_state = syntax_scan_line_state(line, syntax, state);
        if (index >= first) {
            syntax_highlight_line(window, line, syntax, &state, row++, 2,
                                  max_x - 4, NULL, 0, 0);
        }
        /* Delimiters beyond the visible columns still affect the next line. */
        state = next_state;
        index++;
        if (indexed) record_index(stream, index, state);
    }
    free(line);
}
