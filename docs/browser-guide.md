# Browser guide

## Choose a keymap

New configurations use Vim browser keys. Existing configurations retain their bindings.
To select the browser keymap explicitly, add this setting to ~/.cupidfmrc:

    keymap=vim

You can also run ./cupidfm --keymap=vim. The Vim profile starts with alphabetical sorting,
directories first, and hidden files disabled. Tabs keep their directory, cursor, filter,
selection, sorting preferences, and navigation history. Switching tabs refreshes their contents.

The original profile retains F1 to exit and Tab to switch panes. Its Ctrl+D delete and the plugin
fm.delete() API still use the existing session trash and undo mechanism. In the Vim profile,
d uses recoverable system trash through GIO, and D requests permanent deletion. Both ask for
confirmation. System trash survives shutdown; session trash does not.

## Panes and selection

The Vim profile shows the parent directory, current directory, and preview. Directory previews
use the tab's sorting and hidden-file settings. Click a side-pane entry to reveal it in the
current pane; right-click opens it. The header clips the current path before the tab labels. On narrow screens, it keeps the active
tab visible and hides labels that do not fit. The status
line shows the selection mode, selected count, position, and queued or running task count.

Set pane weights and the cursor's scroll margin in ~/.cupidfmrc:

    pane_parent=1
    pane_current=4
    pane_preview=3
    scrolloff=5

Pane weights range from 0 to 100; the current pane requires at least 1. A zero weight hides a
side pane. The parent pane collapses below 80 columns and the preview below 48 columns.
The current pane keeps at least 20 columns when space permits. Very small terminals display
a resize message. Scrolloff ranges from 0 to 100 and is limited to half the visible list.

Selections belong to the tab and keep their full paths when you navigate, filter, hide files,
or reload. Yank, file tasks, openers, shell commands, and chooser output use every selected path,
including paths outside the current directory. With no selection, they use the hovered entry.
Rename accepts one selected path, preserves existing destinations, and keeps the item selected under
its new name. File information and
permissions still use the hovered entry. Esc clears the tab's selection after leaving
find, visual, and filter modes. A directory and its descendants cannot be selected together.
Reloading prunes missing paths and finishes any visual range; hidden selections remain active.

## Browser keys

| Keys | Action |
| --- | --- |
| j, k, up/down | Next or previous file, wrapping at the ends |
| h, l, left/right | Leave or enter a directory |
| gg, G, Home, End | First or last file |
| Ctrl+U/D, Ctrl+B/F, PageUp/Down | Half-page or full-page movement, clamped to the list |
| H, L | Back or forward in directory history |
| Space | Toggle the hovered entry and move down one position |
| v, V | Select or unset a range, including across the list boundary |
| Ctrl+A, Ctrl+R | Select all visible entries or invert selection |
| Esc | Clear find, finish visual mode, clear the filter, then clear selection |
| J, K | Seek five lines down or up in the preview |
| f | Filter names with a regular expression |
| /, ?, n, N | Find next/previous, then repeat in either direction |
| . | Toggle hidden entries |
| o, Enter, O | Open, or enter an opener command |
| y, x, p, P | Yank, cut, paste, or paste with overwrite enabled |
| Y, X | Clear the yank |
| -, _, Ctrl+_ | Create absolute symlinks, relative symlinks, or hardlinks from the yank |
| a | Create a file; end the name with / for a directory |
| r | Use CupidFM's existing single-entry rename prompt |
| :, ; | Run a blocking shell command or queue a background shell task |
| Tab | Show information about the hovered file |
| Ctrl+E | Open CupidFM's editor |
| w | Open the task manager |
| q, Q, Ctrl+C | Quit, quit without a cwd file, or close the current tab |
| Ctrl+Z | Suspend |
| F1, ~ | Show browser help |

Lowercase find/filter patterns ignore case. Uppercase letters make the pattern case sensitive.
Patterns use POSIX extended regular expressions rather than Rust regex syntax.

Shell commands receive selected paths as positional arguments. Use "$@" for all selected paths
and "$1" for the first. Filenames are not interpolated into command text. Blocking openers use
$EDITOR for recognized text and xdg-open for other files. O lets you edit the opener command.

In the task manager, arrows or j/k select a task, Enter shows its error or captured shell output,
x cancels it, and w or Esc closes the window. Quit asks before cancelling unfinished tasks.
Cancellation can leave completed items and partially copied destinations. Check the task details
before retrying a batch.

File yanks are held in the process, so copying between tabs does not require a display server.
CupidFM also publishes its file clipboard payload through X11 or Wayland when available.
Copying text fields through c chords requires xclip or wl-copy.

## Help

Run ./cupidfm --help for command-line options, browser keys, syntax setup, and launch examples.
In the Vim profile, F1 or ~ opens help; the original profile uses its configured help key.
The help screen shows the active browser profile and configured editor keys.

Use j/k or the arrow keys to scroll, PageUp/PageDown for a page, and Home/End for either end.
Press / to fuzzy-find help commands. Results update as you type and rank closer matches first.
Queries ignore case and spaces: ntab finds the new-tab command. Enter keeps the results;
j/k, arrows, or n/N scroll through them. Backspace edits the query and Ctrl+U clears it.
Esc cancels an edit or clears the results; q closes help. Text wraps when the terminal is resized.

Some commands use two keys. Press t, then t to create a tab, or t, then r to rename it.
The g, comma, m, and c prefixes cover navigation, sorting, details, and copying text.
Pressing a prefix shows its available next keys in the notification line; Esc cancels it.

## Mouse navigation

Click an entry to move the cursor. Right-click to enter a directory or open a file.
Drag with the left button to select a range. Scrolling over the directory pane moves
three entries; scrolling over the preview pane seeks up to three lines. Borders and
empty rows do not open files. Preview seeking stops at the available content.

## Chords and tabs

| Prefix | Second key | Action |
| --- | --- | --- |
| , | a, n, e, s, m, b, r | Alphabetical, natural, extension, size, mtime, birth time, random order |
| , | A, N, E, S, M, B | Reverse the corresponding sort; directories stay first |
| m | s, p, m, b, o, n | Show size, permissions, mtime, birth time, owner, or no details |
| g | h, c, d | Home, ~/.config, or ~/Downloads |
| g | Space, f | Enter a directory path, or follow the hovered link |
| t | t, r | Create a tab in the current directory, or name the current tab |
| c | c, C, d, D, f, n | Copy path, file URL, directory path, directory URL, filename, or stem |

Use 1 through 9 to switch tabs, brackets to cycle, and braces to move a tab left or right.
New tabs appear immediately after the current tab. There are at most nine tabs.
Birth time is shown as "-" when unavailable. Directory sizes use the existing size cache.

Sorting reads the whole directory before displaying it, including entries beyond the legacy
200-entry batch. The scan currently runs on the UI thread. Linux directory watches refresh the
active Vim tab after external changes, with a 100 ms debounce.

## Shell integration

    cupidfm --keymap=vim --cwd-file=/tmp/cupidfm-cwd
    cupidfm --keymap=vim --chooser-file=/tmp/cupidfm-selection

The cwd file contains the final directory without a trailing newline. Q leaves it untouched.
Opening with the chooser option writes the selected paths, one per line, and exits. Relative
output paths are resolved against the directory from which CupidFM was launched.

## Libraries

CupidMIME is pinned in lib/cupidmime to upstream commit
9db5a59401c077640463b838d5141ba497695ff2. Recognized binary formats use its bounded byte detector.
libmagic supplies source-language types, descriptions, encodings, and format fallbacks.
Native detection does not open directories, FIFOs, or device nodes. Preview dispatch also skips
special files, including FIFOs with image, archive, or source-file extensions. Cache entries distinguish
the detection backends and recheck file identity before retaining a result.

GIO supplies copying, moving, recoverable trash, and cancellation. GLib/GIO development headers
and pkg-config are required at build time. CupidFM adds directory traversal without following
symlinks, rejects copying into the source, and keeps ncurses on the UI thread. File tasks run
through one worker. The task list retains up to 128 entries and reclaims a finished entry when full.
The byte counter describes the current file, not an aggregate estimate for a directory tree.

Cupid library sources are pinned without local modifications. The library suites run with Clang. Library fixes must be made and tested upstream before updating
their snapshots and source pins here.

## Coverage and remaining work

"Covered" identifies behavior exercised by the implementation and its regressions.
"Partial" identifies a working subset with remaining work.

| Browser system | Status | Coverage or remaining work |
| --- | --- | --- |
| Step, top/bottom, and page movement | Covered | Wrapping steps and clamped page movement |
| Parent/child navigation | Covered | Leaving restores the child entry |
| Back/forward history | Covered | Per-tab history, including hovered names |
| Hidden toggle | Covered | Active listing and per-tab preference |
| Alphabetical, natural, extension, size, mtime, btime, random sorting | Partial | No transliteration, custom ranks, configurable fallback, or async scan |
| Directory grouping and reverse sorting | Covered | Directories remain first |
| Scrolloff and pane ratios | Covered | Configurable margin and weights, parent/current/preview panes, narrow-terminal fallback |
| Individual, all, inverse, and visual selection | Covered | Per-tab paths survive navigation, filtering, hidden toggles, and reload; ancestor conflicts are rejected |
| Tabs, naming, cycling, movement, closing | Covered | Independent state, insertion after current tab, nine-tab limit |
| Find and filter | Partial | Submitted POSIX regex; no live filter or Rust regex compatibility |
| Recursive filename/content search | Missing | fd and rg search views are not wired |
| fzf/zoxide navigation | Missing | No built-in integration |
| Openers and open-with | Partial | Editor/xdg-open and editable command; no MIME rule tables or opener picker |
| File creation | Partial | One file or nested directory path; no bulk create |
| Rename | Partial | Single-entry prompt; no bulk workflow or cursor presets |
| Copy/move/paste and overwrite | Partial | Background recursion; no conflict policy editor or aggregate progress estimate |
| Absolute/relative/hard links | Covered | Yanked local paths, preserving existing destinations |
| Trash and permanent deletion | Partial | GIO trash for Vim keys; no trash browser, restore UI, or empty action |
| Yank and unyank | Covered | Internal yank, literal paths, optional desktop publication |
| Copy path/URL/name/stem | Partial | Hovered entry and X11/Wayland; no OSC 52 |
| Blocking/background shell | Partial | Positional paths, output capture, cancellation; no full placeholder/rule system |
| Task manager | Partial | Queue, counts, current-file bytes, cancellation, error/output inspection; one worker |
| Directory watching | Partial | Active local Linux directory and side-pane listings; inactive tabs refresh on switch |
| Text/code previews | Partial | Existing bounded index and syntax definitions; work runs on UI thread |
| Image/graphics previews | Partial | Kitty/Sixel/ANSI; no complete terminal or multiplexer coverage |
| PDF/SVG/video/font/JSON preview pipelines | Partial | Library capabilities and text fallback do not cover all preview formats |
| Archive preview/extraction | Partial | CupidArchive previews; no general extraction workflow or full format coverage |
| Spot inspector | Partial | File-info popup; no multi-file table, swipe, or cell-copy UI |
| Mouse click, wheel, and drag | Covered | Click to hover or reveal side-pane entries, right-click to enter/open, wheel to scroll, drag to select |
| Input editing/history/completion | Partial | Basic UTF-8 editing; no modal input, undo/history, or completion picker |
| Help/which-key | Partial | Scrollable, searchable help and chord indicator; no action picker or action sequences |
| TOML keymaps/themes/configuration | Missing | CupidConf bindings and two profiles remain |
| Lua plugin API and preset plugins | Missing | CupidScript remains; the Lua runtime is not implemented |
| Async fetch/preload/peek/seek/spot lifecycle | Missing | Existing hooks do not implement this lifecycle |
| Plugin/package manager and flavors | Missing | No package management commands |
| IPC, emit, pub/sub, remote control | Missing | No IPC command interface |
| Local/archive/remote/trash URL model | Missing | Local filesystem paths remain |
| SFTP and remote transfers/caches | Missing | No remote browser backend |
| CLI cwd/chooser output | Covered | Keymap override and Q cwd suppression |
| Multiple startup entries and file reveal | Missing | One startup directory |
| Native Windows/macOS behavior | Missing | This checkpoint targets Linux/WSL |
| Notifications and theme components | Partial | Path/tab header, mode/task status, and ncurses colors; no configurable component/theme system |

## Verification

    make CC=clang
    make test CC=clang
    make test-libs CC=clang
    python3 tests/test_terminal_smoke.py
    python3 tests/test_scroll_smoke.py
    python3 tests/test_browser_smoke.py
    python3 tests/test_cli_output.py

The layout test checks pane bounds across terminal sizes and ratios. Browser session tests cover
side-pane navigation, mouse dragging, tab ownership, history branching, visual ranges, selections
across directories and hidden toggles, scroll margins, and sorting beyond 200 entries. The task test checks recursive copying, symlink traversal, source/destination
conflicts, overwrite, literal shell paths, cancellation, moving, and recoverable trash.
The native MIME test checks CupidMIME routing and nonblocking special-file handling.
The Vim terminal test covers keys, selection reaching plugins, tabs, history, filtering,
external changes, copying selections from multiple directories without X11, and trash that survives
shutdown.
