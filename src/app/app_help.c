#include "app_help.h"

#include <ctype.h>
#include <limits.h>
#include <stddef.h>

const char *app_help_vim(void) {
    return "Vim browser keys\n"
           "\n"
           "Key sequences: press a prefix, then the next key. Esc cancels a prefix.\n"
           "  t: tabs   g: go to   ,: sort   m: details   c: copy text\n"
           "\n"
           "Navigation\n"
           "  j/k, Up/Down       Next/previous entry; wrap at the ends\n"
           "  h/l, Left/Right    Leave/enter a directory\n"
           "  gg/G, Home/End     First/last entry\n"
           "  Ctrl+U/D          Half page; clamp at the ends\n"
           "  Ctrl+B/F, PgUp/Dn  Full page\n"
           "  H/L               Back/forward in directory history\n"
           "  gh/gc/gd          Home/config/Downloads\n"
           "  g Space           Enter a directory path\n"
           "  gf                Follow the hovered symlink\n"
           "\n"
           "Selection and finding\n"
           "  Space             Toggle selection and move down\n"
           "  v/V               Select/unset a range\n"
           "  Ctrl+A/R          Select all visible/invert visible selection\n"
           "  f                 Filter names with a regex\n"
           "  /, ?              Find next/previous by regex\n"
           "  n/N               Repeat find in either direction\n"
           "  Esc               Clear find, visual mode, filter, then selection\n"
           "  .                 Toggle hidden files\n"
           "  Selections belong to the tab and survive directory changes.\n"
           "\n"
           "Files and tasks\n"
           "  o, Enter / O      Open / choose an opener command\n"
           "  y/x               Yank/cut selected paths, or the hovered path\n"
           "  p/P               Paste / allow overwrite\n"
           "  Y/X               Clear the yank\n"
           "  a                 Create a file; end the name with / for a folder\n"
           "  r                 Rename one selected or hovered entry\n"
           "  d/D               System trash/permanent delete; asks first\n"
           "  -/_/Ctrl+_        Absolute symlink/relative symlink/hardlink\n"
           "  :/;               Blocking/background shell; files are in \"$@\"\n"
           "  w                 Task list: progress, cancellation, errors, output\n"
           "  Tab               Information about the hovered entry\n"
           "  J/K               Seek down/up in the preview\n"
           "\n"
           "Sorting and details\n"
           "  ,a/n/e/s/m/b/r    Alphabetical/natural/extension/size/mtime/btime/random\n"
           "  Uppercase sort key reverses order.\n"
           "  ms/mp/mm/mb/mo/mn  Size/permissions/mtime/btime/owner/no details\n"
           "  cc/cC             Copy path/file URL to desktop clipboard\n"
           "  cd/cD             Copy directory path/URL\n"
           "  cf/cn             Copy filename/stem\n"
           "\n"
           "Mouse\n"
           "  Left click        Hover; side-pane entries reveal their location\n"
           "  Right/double click Open or enter\n"
           "  Wheel             Move in the list or seek in the preview\n"
           "  Drag              Select a range\n"
           "\n"
           "Session\n"
           "  q/Q               Quit / quit without writing --cwd-file\n"
           "  Ctrl+Z            Suspend\n"
           "  F1, ~             Help; / fuzzy-finds commands in the help screen\n";
}

const char *app_help_tabs(void) {
    return "Tabs\n"
           "  t then t (tt)     Create a new tab in the current directory\n"
           "  t then r (tr)     Rename the current tab\n"
           "  New tabs keep independent selections, filters, sort, cursor, and history.\n"
           "  Up to nine tabs; a new tab opens after the current tab.\n"
           "  1..9              Switch tabs\n"
           "  [/]               Previous/next tab\n"
           "  {/}               Move the current tab left/right\n"
           "  Ctrl+C            Close current tab; quit if it is the last tab\n"
           "\n";
}

const char *app_help_setup(void) {
    return "Configuration and syntax\n"
           "  ~/.cupidfmrc       Browser profile, keys, pane weights, scrolloff\n"
           "  ~/.cupidfm/plugins CupidScript plugins\n"
           "  ~/.cupidfm/syntax  User syntax overrides (restart to reload)\n"
           "  Bundled syntax loads beside the checkout binary or from share/cupidfm/syntax.\n"
           "  install.sh installs the shared definitions automatically.\n"
           "  CUPIDFM_SYNTAX_DIR adds a syntax directory after user overrides.\n"
           "  Syntax uses foreground colors and the terminal background.\n"
           "\n"
           "Documentation\n"
           "  docs/browser-guide.md  Browser shortcuts, behavior, and remaining work\n"
           "  docs/syntax-guide.md   Syntax format, colors, and installation\n"
           "  examples/syntax/      Highlighting samples and regression expectations\n";
}

const char *app_help_prefix(int key) {
    switch (key) {
        case 't':
            return "t: t new tab; r rename tab; Esc cancel";
        case 'g':
            return "g: g first entry; h home; c config; d Downloads; Space path; f follow link";
        case ',':
            return ",: a alphabetical; n natural; e extension; s size; m modified; b created; r "
                   "random";
        case 'm':
            return "m: s size; p permissions; m modified; b created; o owner; n no details";
        case 'c':
            return "c: c path; C file URL; d directory; D directory URL; f filename; n stem";
        default:
            return "";
    }
}

int app_help_fuzzy_score(const char *query, const char *text) {
    if (!query || !text)
        return -1;
    while (*query && isspace((unsigned char)*query))
        query++;
    if (!*query)
        return 0;
    while (*text && isspace((unsigned char)*text))
        text++;
    int best = INT_MAX;
    for (size_t start = 0; text[start]; start++) {
        if (tolower((unsigned char)text[start]) != tolower((unsigned char)*query))
            continue;
        size_t position = start;
        int score = (int)start;
        if (start && isalnum((unsigned char)text[start - 1]))
            score += 20;
        const char *pattern = query;
        for (; *pattern; pattern++) {
            if (isspace((unsigned char)*pattern))
                continue;
            size_t before = position;
            while (text[position] &&
                   tolower((unsigned char)text[position]) != tolower((unsigned char)*pattern))
                position++;
            if (!text[position])
                break;
            score += (int)(position - before) * 3;
            position++;
        }
        if (!*pattern && score < best)
            best = score;
    }
    return best == INT_MAX ? -1 : best;
}
