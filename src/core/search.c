#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "search.h"

#include <ctype.h>
#include <regex.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "browser_ui.h"
#include "files.h"
#include "globals.h"
#include "main.h" // banner_mutex, draw_scrolling_banner, bannerwin, BANNER_TEXT, BUILD_INFO
#include "utils.h"

typedef struct {
    int score;      // lower is better
    FileAttr entry; // pointer owned by AppState.files
} FuzzyHit;

SIZE find_loaded_index_by_name(Vector *files, const char *name) {
    if (!files || !name || !*name) return (SIZE)-1;
    SIZE count = (SIZE)Vector_len(*files);
    for (SIZE i = 0; i < count; i++) {
        FileAttr fa = (FileAttr)files->el[i];
        const char *nm = FileAttr_get_name(fa);
        if (nm && strcmp(nm, name) == 0) {
            return i;
        }
    }
    return (SIZE)-1;
}

SIZE find_index_by_name_lazy(Vector *files,
                            const char *dir,
                            CursorAndSlice *cas,
                            LazyLoadState *lazy_load,
                            const char *name)
{
    if (!files || !dir || !cas || !lazy_load || !name || !*name) return (SIZE)-1;

    for (int safety = 0; safety < 512; safety++) {
        SIZE idx = find_loaded_index_by_name(files, name);
        if (idx != (SIZE)-1) return idx;

        size_t before = Vector_len(*files);
        cas->num_files = before;
        if (before == 0) return (SIZE)-1;

        cas->cursor = (SIZE)(before - 1);
        load_more_files_if_needed(files, dir, cas, &lazy_load->files_loaded, lazy_load->total_files);

        size_t after = Vector_len(*files);
        cas->num_files = after;
        if (after == before) break;
    }

    return (SIZE)-1;
}

static int cupidfuzzy_score(const char *pattern, const char *text) {
    if (!pattern || !*pattern || !text) return -1;
    const char *scan = text;
    const char *segment_start = text;
    int score = 0;

    for (const char *p = pattern; *p; p++) {
        char target = tolower((unsigned char)*p);
        bool matched = false;
        while (*scan) {
            if (tolower((unsigned char)*scan) == target) {
                score += (int)(scan - segment_start);
                segment_start = scan + 1;
                scan = segment_start;
                matched = true;
                break;
            }
            scan++;
        }
        if (!matched) return -1;
    }

    return score;
}

static int fuzzyhit_cmp(const void *a, const void *b) {
    const FuzzyHit *aa = (const FuzzyHit *)a;
    const FuzzyHit *bb = (const FuzzyHit *)b;
    if (aa->score != bb->score) return (aa->score < bb->score) ? -1 : 1;

    const char *an = FileAttr_get_name(aa->entry);
    const char *bn = FileAttr_get_name(bb->entry);
    if (!an && !bn) return 0;
    if (!an) return 1;
    if (!bn) return -1;
    return strcasecmp(an, bn);
}

static bool ci_substr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    size_t nlen = strlen(needle);
    if (nlen == 0) return true;
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (needle[i] && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

Vector *active_files(AppState *state) {
    return (state && state->search_active) ? &state->search_files : &state->files;
}

void search_clear(AppState *state) {
    if (!state) return;
    state->select_all_active = false;
    g_select_all_highlight = false;
    state->search_active = false;
    state->search_query[0] = '\0';
    if (state->search_files.el) {
        Vector_set_len_no_free(&state->search_files, 0);
    }
}

size_t search_rebuild(AppState *state, const char *query) {
    if (!state || !state->search_files.el) return 0;
    Vector_set_len_no_free(&state->search_files, 0);

    if (!query || !*query) return 0;

    size_t total = Vector_len(state->files);
    if (total == 0) return 0;

    if (state->search_mode == SEARCH_MODE_EXACT) {
        size_t count = 0;
        for (size_t i = 0; i < total; i++) {
            FileAttr fa = (FileAttr)state->files.el[i];
            const char *name = FileAttr_get_name(fa);
            if (!name) continue;
            if (!ci_substr(name, query)) continue;
            Vector_add(&state->search_files, 1);
            state->search_files.el[count++] = fa;
        }
        Vector_set_len_no_free(&state->search_files, count);
        return count;
    }

    if (state->search_mode == SEARCH_MODE_REGEX) {
        regex_t re;
        if (regcomp(&re, query, REG_EXTENDED | REG_NOSUB) != 0) {
            return 0;
        }
        size_t count = 0;
        for (size_t i = 0; i < total; i++) {
            FileAttr fa = (FileAttr)state->files.el[i];
            const char *name = FileAttr_get_name(fa);
            if (!name) continue;
            if (regexec(&re, name, 0, NULL, 0) != 0) continue;
            Vector_add(&state->search_files, 1);
            state->search_files.el[count++] = fa;
        }
        Vector_set_len_no_free(&state->search_files, count);
        regfree(&re);
        return count;
    }

    FuzzyHit *hits = malloc(total * sizeof(*hits));
    if (!hits) return 0;

    size_t count = 0;
    for (size_t i = 0; i < total; i++) {
        FileAttr fa = (FileAttr)state->files.el[i];
        const char *name = FileAttr_get_name(fa);
        if (!name) continue;

        int score = cupidfuzzy_score(query, name);
        if (score < 0) continue;

        hits[count].score = score;
        hits[count].entry = fa;
        count++;
    }

    if (count == 0) {
        free(hits);
        return 0;
    }

    qsort(hits, count, sizeof(*hits), fuzzyhit_cmp);

    Vector_add(&state->search_files, count);
    for (size_t i = 0; i < count; i++) {
        state->search_files.el[i] = hits[i].entry;
    }
    Vector_set_len_no_free(&state->search_files, count);

    free(hits);
    return count;
}

void search_before_reload(AppState *state, char saved_query[MAX_PATH_LENGTH]) {
    if (!state) return;
    state->select_all_active = false;
    g_select_all_highlight = false;
    if (saved_query) {
        saved_query[0] = '\0';
        if (state->search_query[0]) {
            strncpy(saved_query, state->search_query, MAX_PATH_LENGTH - 1);
            saved_query[MAX_PATH_LENGTH - 1] = '\0';
        }
    }
    search_clear(state);
}

void search_after_reload(AppState *state, CursorAndSlice *cas, const char *saved_query) {
    if (!state || !cas) return;
    if (saved_query && *saved_query) {
        strncpy(state->search_query, saved_query, sizeof(state->search_query) - 1);
        state->search_query[sizeof(state->search_query) - 1] = '\0';
        state->search_active = true;
        search_rebuild(state, state->search_query);
        cas->cursor = 0;
        cas->start = 0;
    }
    sync_selection_from_active(state, cas);
}

void maybe_load_more_for_search(AppState *state, CursorAndSlice *cas) {
    if (!state || !cas) return;
    if (!state->search_active) return;
    if (state->lazy_load.total_files == 0) return;
    if (state->lazy_load.files_loaded >= state->lazy_load.total_files) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long dt_ns = (now.tv_sec - state->lazy_load.last_load_time.tv_sec) * 1000000000L +
                 (now.tv_nsec - state->lazy_load.last_load_time.tv_nsec);
    if (state->lazy_load.last_load_time.tv_sec != 0 && dt_ns < 150000000L) {
        return;
    }
    state->lazy_load.last_load_time = now;

    CursorAndSlice tmp = *cas;
    tmp.start = (SIZE)state->lazy_load.files_loaded;
    tmp.cursor = tmp.start;
    tmp.num_files = (SIZE)Vector_len(state->files);

    load_more_files_if_needed(&state->files,
                              state->current_directory,
                              &tmp,
                              &state->lazy_load.files_loaded,
                              state->lazy_load.total_files);
}

void sync_selection_from_active(AppState *state, CursorAndSlice *cas) {
    if (!state || !cas) return;
    char old_name[MAX_PATH_LENGTH] = {0};
    if (state->selected_entry && *state->selected_entry) {
        strncpy(old_name, state->selected_entry, sizeof(old_name) - 1);
        old_name[sizeof(old_name) - 1] = '\0';
    }
    Vector *files = active_files(state);
    cas->num_files = (SIZE)Vector_len(*files);
    if (cas->num_files <= 0) {
        cas->cursor = 0;
        cas->start = 0;
        state->selected_entry = "";
        return;
    }
    if (cas->cursor >= (SIZE)cas->num_files) cas->cursor = (SIZE)cas->num_files - 1;
    fix_cursor(cas);
    state->selected_entry = FileAttr_get_name((FileAttr)files->el[cas->cursor]);
    if (state->preview_override_active) {
        const char *new_name = state->selected_entry ? state->selected_entry : "";
        if (strcmp(old_name, new_name) != 0) {
            state->preview_override_active = false;
            state->preview_override_path[0] = '\0';
        }
    }
}

bool prompt_fuzzy_search(AppState *state,
                         CursorAndSlice *cas,
                         WINDOW *win,
                         WINDOW *dir_window,
                         WINDOW *preview_window)
{
    if (!state || !cas || !win) return false;

    char saved_selection[MAX_PATH_LENGTH] = {0};
    if (state->selected_entry && *state->selected_entry) {
        strncpy(saved_selection, state->selected_entry, sizeof(saved_selection) - 1);
        saved_selection[sizeof(saved_selection) - 1] = '\0';
    }

    char query[MAX_PATH_LENGTH] = {0};
    strncpy(query, state->search_query, sizeof(query) - 1);
    query[sizeof(query) - 1] = '\0';
    size_t index = strlen(query);

    bool aborted = false;

    keypad(win, TRUE);
    wtimeout(win, 10);
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = (COLS - 2) +
                              (BANNER_TEXT ? (int)strlen(BANNER_TEXT) : 0) +
                              (BUILD_INFO ? (int)strlen(BUILD_INFO) : 0) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    state->search_query[0] = '\0';
    if (query[0]) {
        strncpy(state->search_query, query, sizeof(state->search_query) - 1);
        state->search_query[sizeof(state->search_query) - 1] = '\0';
        state->search_active = true;
        search_rebuild(state, state->search_query);
    } else {
        search_clear(state);
    }
    cas->cursor = 0;
    cas->start = 0;
    sync_selection_from_active(state, cas);

    while (true) {
        size_t shown = Vector_len(*active_files(state));
        werase(win);
        mvwprintw(win, 0, 0, "Search (Esc to cancel): %s [%zu]", query, shown);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) {
            struct timespec current_time;
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            long banner_time_diff = (current_time.tv_sec - last_banner_update.tv_sec) * 1000000 +
                                    (current_time.tv_nsec - last_banner_update.tv_nsec) / 1000;
            if (banner_time_diff >= BANNER_SCROLL_INTERVAL && BANNER_TEXT && bannerwin) {
                pthread_mutex_lock(&banner_mutex);
                draw_scrolling_banner(bannerwin, BANNER_TEXT, BUILD_INFO, banner_offset);
                pthread_mutex_unlock(&banner_mutex);
                banner_offset = (banner_offset + 1) % total_scroll_length;
                last_banner_update = current_time;
            }

            maybe_load_more_for_search(state, cas);
            if (state->search_active && state->search_query[0]) {
                search_rebuild(state, state->search_query);
                sync_selection_from_active(state, cas);
                draw_directory_window(dir_window, state->current_directory, active_files(state), cas);
                if (state->preview_override_active) {
                    draw_preview_window_path(preview_window, state->preview_override_path, NULL, state->preview_start_line);
                } else {
                    draw_preview_window(preview_window, state->current_directory, state->selected_entry, state->preview_start_line);
                }
                wrefresh(dir_window);
                wrefresh(preview_window);
            }

            napms(10);
            continue;
        }

        if (ch == 27) { // Esc
            aborted = true;
            break;
        }
        if (ch == '\n') {
            break;
        }

        if (ch == KEY_UP || ch == KEY_PPAGE) {
            if (state->search_active && Vector_len(state->search_files) > 0) {
                int step = (ch == KEY_PPAGE) ? MAX(1, cas->num_lines - 2) : 1;
                SIZE len = (SIZE)Vector_len(state->search_files);
                if (len <= 0) break;

                SIZE next = cas->cursor - step;
                while (next < 0) next += len;
                cas->cursor = next;
                fix_cursor(cas);
                sync_selection_from_active(state, cas);
                state->preview_start_line = 0;
            }
        } else if (ch == KEY_DOWN || ch == KEY_NPAGE) {
            if (state->search_active && Vector_len(state->search_files) > 0) {
                int step = (ch == KEY_NPAGE) ? MAX(1, cas->num_lines - 2) : 1;
                SIZE len = (SIZE)Vector_len(state->search_files);
                if (len <= 0) break;

                cas->cursor = (cas->cursor + step) % len;
                fix_cursor(cas);
                sync_selection_from_active(state, cas);
                state->preview_start_line = 0;
            }
        } else {
            bool query_changed = false;
            if (ch == KEY_BACKSPACE || ch == 127) {
                if (index > 0) {
                    index--;
                    query[index] = '\0';
                    query_changed = true;
                }
            } else if (isprint(ch) && index < MAX_PATH_LENGTH - 1) {
                query[index++] = (char)ch;
                query[index] = '\0';
                query_changed = true;
            }

            if (query_changed) {
                state->select_all_active = false;
                g_select_all_highlight = false;

                strncpy(state->search_query, query, sizeof(state->search_query) - 1);
                state->search_query[sizeof(state->search_query) - 1] = '\0';
                if (query[0] == '\0') {
                    search_clear(state);
                } else {
                    state->search_active = true;
                    search_rebuild(state, state->search_query);
                }
                cas->cursor = 0;
                cas->start = 0;
                sync_selection_from_active(state, cas);
                state->preview_start_line = 0;
            }
        }

        draw_directory_window(dir_window, state->current_directory, active_files(state), cas);
        if (state->preview_override_active) {
            draw_preview_window_path(preview_window, state->preview_override_path, NULL, state->preview_start_line);
        } else {
            draw_preview_window(preview_window, state->current_directory, state->selected_entry, state->preview_start_line);
        }
        wrefresh(dir_window);
        wrefresh(preview_window);
    }

    wtimeout(win, -1);
    werase(win);
    wrefresh(win);

    if (aborted) {
        search_clear(state);
        cas->num_files = (SIZE)Vector_len(state->files);
        cas->cursor = 0;
        cas->start = 0;
        if (saved_selection[0]) {
            SIZE idx = find_index_by_name_lazy(&state->files,
                                               state->current_directory,
                                               cas,
                                               &state->lazy_load,
                                               saved_selection);
            if (idx != (SIZE)-1) {
                cas->cursor = idx;
            }
        }
        sync_selection_from_active(state, cas);
        draw_directory_window(dir_window, state->current_directory, &state->files, cas);
        if (state->preview_override_active) {
            draw_preview_window_path(preview_window, state->preview_override_path, NULL, state->preview_start_line);
        } else {
            draw_preview_window(preview_window, state->current_directory, state->selected_entry, state->preview_start_line);
        }
        wrefresh(dir_window);
        wrefresh(preview_window);

        show_notification(win, "Search canceled.");
        should_clear_notif = false;
        return false;
    }

    if (query[0] == '\0') {
        search_clear(state);
        sync_selection_from_active(state, cas);
        show_notification(win, "Search cleared.");
        should_clear_notif = false;
        return true;
    }

    if (!state->search_active || Vector_len(state->search_files) == 0) {
        show_notification(win, "No matches for \"%s\"", query);
        should_clear_notif = false;
        return false;
    }

    show_notification(win, "Search: %s", query);
    should_clear_notif = false;
    return true;
}
