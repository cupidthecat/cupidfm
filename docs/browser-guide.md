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
Rename accepts selected paths, preserves existing destinations, and keeps successful moves selected
under their new names. Selected paths use a reviewed editor list in the Vim profile. File information and
permissions still use the hovered entry. Esc leaves search and filtering together, finishes visual mode, or clears selection. A directory and its descendants cannot be selected together.
Reloading prunes missing paths and finishes any visual range; hidden selections remain active.

## Browser keys

| Keys | Action |
| --- | --- |
| j, k, up/down | Next or previous file, wrapping at the ends |
| h, l, left/right | Leave or enter a directory |
| gg, G, Home, End | First or last file |
| Ctrl+U/D, Ctrl+B/F, PageUp/Down | Half-page or full-page movement, clamped to the list |
| H, L | Back or forward in directory history |
| z, Z | Jump with fzf, or choose a directory from zoxide history |
| Space | Toggle the hovered entry and move down one position |
| v, V | Select or unset a range, including across the list boundary |
| Ctrl+A, Ctrl+R | Select all visible entries or invert selection |
| Esc | Leave search/filter together; otherwise finish visual mode or clear selection |
| J, K | Seek five lines down or up in the preview |
| f | Filter names live with a regular expression |
| /, ?, n, N | Find next/previous, then repeat in either direction |
| s, S | Recursive filename/content search |
| Ctrl+S | Leave recursive search or clear the filter |
| . | Toggle hidden entries |
| o, Enter, O | Open, or enter an opener command |
| y, x, p, P | Yank, cut, paste, or paste with overwrite enabled |
| Y, X | Clear the yank |
| -, _, Ctrl+_ | Create absolute symlinks, relative symlinks, or hardlinks from the yank |
| a | Create a file; end the name with / for a directory |
| A | Review an editor list and queue creation of several files/directories |
| r | Rename one entry, or edit a name list for multiple selections |
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
and "$1" for the first. Filenames are not interpolated into command text. Default openers use
$EDITOR for text and xdg-open for other files. MIME/path rules can select custom blocking,
tracked, or detached commands. O offers common opener choices and a custom command.
See [openers and bulk rename](openers-and-renaming.md) for configuration and batch behavior.

In the task manager, arrows or j/k select a task, Enter shows its error or captured shell output,
x cancels it, and w or Esc closes the window. Quit asks before cancelling unfinished tasks.
Cancellation can leave completed items and partially copied destinations. Check the task details
before retrying a batch.

File yanks are held in the process, so copying between tabs does not require a display server.
CupidFM also publishes its file clipboard payload through X11 or Wayland when available.
Text copying tries wl-copy on Wayland and xclip on X11, then emits OSC 52 when stdout is a
terminal. This also supports copying text over a remote terminal session. A tmux session gets
a passthrough wrapper; allow-passthrough must be enabled there. The terminal decides whether
to accept clipboard writes. The fallback emits at most 1 MiB per copy; it does not read the
terminal clipboard or transfer files. The internal file yank remains independent.

## Help

Run ./cupidfm --help for command-line options, browser keys, syntax setup, and launch examples.
In the Vim profile, F1 or ~ opens help; the original profile uses its configured help key.
The help screen shows the active browser profile and configured editor keys.

Use j/k or the arrow keys to scroll, PageUp/PageDown for a page, and Home/End for either end.
Press / to fuzzy-find help commands. Results update as you type and rank closer matches first.
Queries ignore case and spaces: ntab finds the new-tab command. Enter keeps the results;
j/k, arrows, or n/N scroll through them. Backspace edits the query and Ctrl+U clears it.
Esc or uppercase Q closes help immediately, including while editing a query. Lowercase q closes help outside query editing. Text wraps when the terminal is resized.

Some commands use two keys. Press t, then t to create a tab, or t, then r to rename it.
The g, comma, m, and c prefixes cover navigation, sorting, details, and copying text.
Pressing a prefix shows its available next keys in the notification line; Esc cancels it.

## Recursive search and input

Press s to search filenames with fd or fdfind, or S to search file contents with rg.
Both accept regular expressions, use smart case, and respect the tools' ignore files and
the tab's hidden-file setting. Install fd-find and ripgrep on Debian/Ubuntu systems.
Results stream into the current tab while navigation remains available. The status line
shows whether the search is running, complete, or failed.

Results use paths relative to the physical search root. Preview, opening, yank, copying,
moving, deletion, rename, and chooser output use their physical paths. Enter a result
directory with l or Right. Ctrl+S stops and reaps the search process, clears the view's
filter, and restores the root's original hovered entry. Esc leaves recursive search, find, and filtering together. Switching back to a search tab reruns
its query; a new tab starts in the physical root directory.

The browser supports paths up to its existing 1023-byte limit. Longer search results
are omitted. Only the active search tab drains results; inactive search processes can
pause when their output pipe fills. Root-directory watching does not watch every
descendant. Use s or S again to refresh a query after nested external changes.

The f filter updates while you type and keeps the selection. Esc restores its previous
query and cursor. A single-file rename starts before the extension, or at the end of a
directory name. Home/End and Ctrl+A/E move to either end of the prompt; Ctrl+U/K delete
before/after the cursor, and Ctrl+W deletes the previous word. Ctrl+Z or Ctrl+_ undo;
Ctrl+Y redoes. Editing respects UTF-8 character boundaries.

Filters, directory paths, rename prompts, recursive queries, and shell commands keep separate
session histories. Up/Down or Ctrl+P/N recall them and return to the draft after the newest
entry. Only submitted values are recorded. Each history keeps up to 32 entries, with at
most 16 histories in memory; confirmation prompts have no history.

Tab completes a path component in directory, creation, and rename prompts. A single match
fills the component; several matches open the fuzzy picker. Later path components are
retained. Directory navigation offers directories only and expands ~/. Filename prompts
complete literal paths. Hidden names appear when the component begins with a dot.
Prompts retain typed text when the terminal is resized. Oversized resolved paths fail instead
of being silently truncated.

## Fuzzy navigation

Press z to run fzf in the physical current directory. One chosen directory opens in CupidFM;
one chosen file reveals its parent and moves the cursor to that file. Multiple choices add
selections when nothing was selected. With an existing selection, fzf receives only those
paths and multiple returned choices are removed from the selection. A single returned choice
reveals its location while retaining the selection. A returned hidden name enables hidden
entries in that tab. Selected paths cross the process boundary
as NUL-delimited records.

Press Z to choose from the existing zoxide directory database. Set up that history through
your shell; CupidFM does not automatically add entries. The current directory is excluded.
The external tools keep their normal environment settings, including FZF_DEFAULT_OPTS and
_ZO_FZF_OPTS. The terminal is restored after the picker exits or is cancelled.

Install fzf and zoxide to enable these commands. Filename generation with no prior selection
uses fzf's own default command or walker. Its filename handling depends on that configuration;
selected-path input and returned paths use NUL delimiters. Both commands retain the browser's
existing 1023-byte path limit.

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

## Launch entries

Pass up to nine file or directory paths to open them in tabs, in argument order:

    cupidfm src/app/main.c ~/Documents /tmp

The first tab is active. A file argument opens its parent and hovers its literal name,
including a broken symlink. A symlink to a directory enters the target directory.
Hidden file arguments enable hidden entries in that tab. Startup plugin hooks receive the
initial tab's directory, hovered name, and listing. Relative arguments resolve from
the launch directory. Use -- before a filename beginning with a dash.

CupidFM checks every entry before starting the terminal interface. A missing, unreadable,
or oversized entry fails the launch with a diagnostic. Omit the entries to use the launch
directory. The existing nine-tab and 1023-byte path limits also apply here.

## Shell integration

    cupidfm --keymap=vim --cwd-file=/tmp/cupidfm-cwd
    cupidfm --keymap=vim --chooser-file=/tmp/cupidfm-selection

The cwd file contains the final directory without a trailing newline. Q leaves it untouched.
Opening with the chooser option writes the selected paths, one per line, and exits. Relative
output paths are resolved against the directory from which CupidFM was launched.

## Libraries

CupidMIME is pinned in lib/cupidmime to upstream commit
8dfde2bb9e59647314e47547380cc99df0e47fbf. Recognized binary formats and supported XML subtypes
use its bounded byte detector. SVG root tags and namespaces distinguish images from generic XML;
XHTML, Atom, and RSS retain their text previews. Generic XML permits a more specific fallback.
ELF objects, executables, and core dumps retain specific types; dynamic ELF permits system
refinement so position-independent executables do not get stuck with a generic label.
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
| Startup files/directories | Covered | Up to nine entries, file reveal, hidden files, broken links, and preflight errors |
| Tabs, naming, cycling, movement, closing | Covered | Independent state, insertion after current tab, nine-tab limit |
| Find and filter | Partial | Live filter with rollback and history; POSIX regex still differs from the full regex language |
| Recursive filename/content search | Partial | Streamed fd/fdfind and rg views, NUL paths, cancellation, filtering, and physical file actions |
| fzf/zoxide navigation | Partial | External interactive navigation, selected-path narrowing, file reveal, directory jumps; no built-in database setup |
| Openers and open-with | Partial | MIME/path rules, common fuzzy picker, grouped dispatch, blocking/tasks/orphan modes; no full placeholder language |
| File creation | Partial | Single paths and reviewed editor batches, including parent directories; no overwrite confirmation |
| Rename | Partial | Cursor before extension for files, reviewed editor batches, dependency ordering, and destination protection |
| Copy/move/paste and overwrite | Partial | Background recursion; no conflict policy editor or aggregate progress estimate |
| Absolute/relative/hard links | Covered | Yanked local paths, preserving existing destinations |
| Trash and permanent deletion | Partial | GIO trash for Vim keys; no trash browser, restore UI, or empty action |
| Yank and unyank | Covered | Internal yank, literal paths, optional desktop publication |
| Copy path/URL/name/stem | Partial | Hovered physical paths, X11/Wayland, OSC 52 text writes and tmux framing; no terminal clipboard read or rich file-transfer protocol |
| Blocking/background shell | Partial | Positional paths, output capture, cancellation; no full placeholder/rule system |
| Task manager | Partial | Queue, counts, current-file bytes, cancellation, error/output inspection; one worker |
| Directory watching | Partial | Active local Linux directory and side-pane listings; inactive tabs refresh on switch |
| Text/code previews | Partial | Existing bounded index and syntax definitions; work runs on UI thread |
| Image/graphics previews | Partial | Kitty/Sixel/ANSI; no complete terminal or multiplexer coverage |
| PDF/SVG/video/font/JSON preview pipelines | Partial | Library capabilities and text fallback do not cover all preview formats |
| Archive preview/extraction | Partial | CupidArchive previews; no general extraction workflow or full format coverage |
| Spot inspector | Partial | File-info popup; no multi-file table, swipe, or cell-copy UI |
| Mouse click, wheel, and drag | Covered | Click to hover or reveal side-pane entries, right-click to enter/open, wheel to scroll, drag to select |
| Input editing/history/completion | Partial | UTF-8 editing, purpose-specific history, undo/redo, component completion and fuzzy picker; no modal input |
| Help/which-key | Partial | Scrollable, searchable help and chord indicator; no action picker or action sequences |
| Complete keymap/theme/component configuration | Missing | CupidConf bindings and two profiles remain |
| Complete plugin API and bundled workflows | Missing | Current CupidScript hooks cover a subset of the browser lifecycle |
| Async fetch/preload/peek/seek/spot lifecycle | Missing | Existing hooks do not implement this lifecycle |
| Plugin/package manager and flavors | Missing | No package management commands |
| IPC, emit, pub/sub, remote control | Missing | No IPC command interface |
| Local/archive/remote/trash URL model | Missing | Local filesystem paths remain |
| SFTP and remote transfers/caches | Missing | No remote browser backend |
| CLI cwd/chooser output | Covered | Keymap override and Q cwd suppression |
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
    python3 tests/test_recursive_search.py
    python3 tests/test_open_bulk_smoke.py
    python3 tests/test_jump_smoke.py

The layout test checks pane bounds across terminal sizes and ratios. Browser session tests cover
side-pane navigation, mouse dragging, tab ownership, history branching, visual ranges, selections
across directories and hidden toggles, scroll margins, and sorting beyond 200 entries. The task test checks recursive copying, symlink traversal, source/destination
conflicts, overwrite, literal shell paths, cancellation, moving, and recoverable trash.
The native MIME test checks CupidMIME routing, SVG and XHTML subtypes, generic XML refinement,
and nonblocking special-file handling. Header and help tests cover path/tab separation, narrow
screens, profile-specific shortcuts, fuzzy matching, and text wrapping.
The Vim terminal test covers keys, selection reaching plugins, tabs, history, filtering,
external changes, copying selections from multiple directories without X11, and trash that survives
shutdown.

The CLI tests cover startup tabs, file reveal beyond the initial batch, hidden names, broken
symlinks, newline names, invalid entries, and relative cwd/chooser outputs. Search tests cover
streamed filename and content results, tab ownership, cancellation, live filtering, physical
file operations, and terminal clipboard output. Opener tests cover literal shell arguments,
common choices, detached launches, reviewed rename and creation batches, and conflicts.
Navigation tests exercise controlled backends and the real fzf and zoxide commands.

See [browser responsiveness](performance-input.md) for benchmark commands, measured results, and input behavior.

In the original profile, Ctrl+F opens live filename search. One Escape cancels the prompt,
clears its filter, and restores the hovered entry. Enter keeps the results; Escape from
the browser clears that filter. Up/Down and PageUp/PageDown move through matches.
Idle search rebuilds results only after another listing batch arrives. Queued typing
skips intermediate preview paints, and queued Escape takes priority over another paint.
An already-running synchronous preview must still finish before input can be read.
