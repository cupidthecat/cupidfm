# Yazi comparison and browser guide

CupidFM uses the local Yazi checkout at commit
[6882e1ac279b3df2722aaa0ab5dc049ce02d21eb](https://github.com/sxyazi/yazi/tree/6882e1ac279b3df2722aaa0ab5dc049ce02d21eb)
as its behavior reference. This is an implementation checkpoint, not a claim of complete parity.

## Choose a keymap

CupidFM keeps its original keymap by default. Add this setting to ~/.cupidfmrc:

    keymap=yazi

You can also run ./cupidfm --keymap=yazi. The Yazi profile starts with alphabetical sorting,
directories first, and hidden files disabled. Tabs keep their directory, cursor, filter,
selection, sorting preferences, and navigation history. Switching tabs refreshes their contents.

The original profile retains F1 to exit and Tab to switch panes. Its Ctrl+D delete and the plugin
fm.delete() API still use the existing session trash and undo mechanism. In the Yazi profile,
d uses recoverable system trash through GIO, and D requests permanent deletion. Both ask for
confirmation. System trash survives shutdown; session trash does not.

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
active Yazi tab after external changes, with a 100 ms debounce.

## Shell integration

    cupidfm --keymap=yazi --cwd-file=/tmp/cupidfm-cwd
    cupidfm --keymap=yazi --chooser-file=/tmp/cupidfm-selection

The cwd file contains the final directory without a trailing newline. Q leaves it untouched.
Opening with the chooser option writes the selected paths, one per line, and exits. Relative
output paths are resolved against the directory from which CupidFM was launched.

## Libraries

CupidMIME is pinned in lib/cupidmime to upstream commit
9db5a59401c077640463b838d5141ba497695ff2. Recognized binary formats use its bounded byte detector.
libmagic supplies source-language types, descriptions, encodings, and format fallbacks.
Native detection does not open directories, FIFOs, or device nodes. Cache entries distinguish
the detection backends and recheck file identity before retaining a result.

GIO supplies copying, moving, recoverable trash, and cancellation. GLib/GIO development headers
and pkg-config are required at build time. CupidFM adds directory traversal without following
symlinks, rejects copying into the source, and keeps ncurses on the UI thread. File tasks run
through one worker. The task list retains up to 128 entries and reclaims a finished entry when full.
The byte counter describes the current file, not an aggregate estimate for a directory tree.

No existing Cupid library source was modified for this checkpoint. All six pinned library
test suites pass with Clang. Library fixes must be made and tested upstream before updating
their snapshots and source pins here.

## Coverage and remaining work

"Covered" identifies behavior exercised by the implementation and its regressions.
"Partial" identifies a working subset with material differences from Yazi.

| Yazi system | Status | Coverage or remaining work |
| --- | --- | --- |
| Step, top/bottom, and page movement | Covered | Wrapping steps and clamped page movement |
| Parent/child navigation | Covered | Leaving restores the child entry |
| Back/forward history | Covered | Per-tab history, including hovered names |
| Hidden toggle | Covered | Active listing and per-tab preference |
| Alphabetical, natural, extension, size, mtime, btime, random sorting | Partial | No transliteration, custom ranks, configurable fallback, or async scan |
| Directory grouping and reverse sorting | Covered | Directories remain first |
| Scrolloff and pane ratios | Missing | Existing two-pane layout remains |
| Individual, all, inverse, and visual selection | Partial | Entry-based selection does not span directories or survive hiding selected entries |
| Tabs, naming, cycling, movement, closing | Covered | Independent state, insertion after current tab, nine-tab limit |
| Find and filter | Partial | Submitted POSIX regex; no live filter or Rust regex compatibility |
| Recursive filename/content search | Missing | fd and rg search views are not wired |
| fzf/zoxide navigation | Missing | No built-in integration |
| Openers and open-with | Partial | Editor/xdg-open and editable command; no MIME rule tables or opener picker |
| File creation | Partial | One file or nested directory path; no bulk create |
| Rename | Partial | Single-entry prompt; no bulk workflow or cursor presets |
| Copy/move/paste and overwrite | Partial | Background recursion; no conflict policy editor or aggregate progress estimate |
| Absolute/relative/hard links | Covered | Yanked local paths, preserving existing destinations |
| Trash and permanent deletion | Partial | GIO trash for Yazi keys; no trash browser, restore UI, or empty action |
| Yank and unyank | Covered | Internal yank, literal paths, optional desktop publication |
| Copy path/URL/name/stem | Partial | Hovered entry and X11/Wayland; no OSC 52 |
| Blocking/background shell | Partial | Positional paths, output capture, cancellation; no full placeholder/rule system |
| Task manager | Partial | Queue, counts, current-file bytes, cancellation, error/output inspection; one worker |
| Directory watching | Partial | Active local Linux directory; inactive tabs refresh on switch |
| Text/code previews | Partial | Existing bounded index and syntax definitions; work runs on UI thread |
| Image/graphics previews | Partial | Kitty/Sixel/ANSI; no complete terminal or multiplexer coverage |
| PDF/SVG/video/font/JSON preview pipelines | Partial | Library capabilities and text fallback do not cover all Yazi plugins |
| Archive preview/extraction | Partial | CupidArchive previews; no general extraction workflow or full format coverage |
| Spot inspector | Partial | File-info popup; no multi-file table, swipe, or cell-copy UI |
| Mouse click, wheel, and drag | Missing | No browser mouse dispatch |
| Input editing/history/completion | Partial | Basic UTF-8 editing; no modal input, undo/history, or completion picker |
| Help/which-key | Partial | Static help and chord indicator; no searchable picker or action sequences |
| TOML keymaps/themes/configuration | Missing | CupidConf bindings and two profiles remain |
| Lua plugin API and preset plugins | Missing | CupidScript remains; Yazi plugins cannot load directly |
| Async fetch/preload/peek/seek/spot lifecycle | Missing | Existing hooks do not implement this lifecycle |
| Plugin/package manager and flavors | Missing | No ya pkg equivalent |
| IPC, emit, pub/sub, remote control | Missing | No ya protocol equivalent |
| Local/archive/remote/trash URL model | Missing | Local filesystem paths remain |
| SFTP and remote transfers/caches | Missing | No remote browser backend |
| CLI cwd/chooser output | Covered | Keymap override and Q cwd suppression |
| Multiple startup entries and file reveal | Missing | One startup directory |
| Native Windows/macOS behavior | Missing | This checkpoint targets Linux/WSL |
| Notifications and theme components | Partial | Existing ncurses notifications and colors |

Source locations used for the comparison:

- yazi-config/preset/keymap-default.toml and yazi-default.toml
- yazi-actor/src/mgr, tasks, input, spot, cmp, which, and help
- yazi-widgets/src/step.rs, yazi-fs/src/sorter.rs, and yazi-core/src/tab
- yazi-scheduler, yazi-watcher, yazi-vfs, yazi-sftp, and yazi-dds
- yazi-plugin/preset/plugins, yazi-cli, and yazi-boot/src/args.rs

The next architectural work is a persistent path-based selection model and an asynchronous
directory snapshot. Those are needed for cross-directory operations, large sorted directories,
remote URLs, and search views to match Yazi without adding more special cases to the UI loop.

## Verification

    make CC=clang
    make test CC=clang
    make test-libs CC=clang
    python3 tests/test_terminal_smoke.py
    python3 tests/test_scroll_smoke.py
    python3 tests/test_yazi_smoke.py
    python3 tests/test_cli_output.py

The browser session test checks tab ownership, history branching, visual ranges, and sorting
beyond 200 entries. The task test checks recursive copying, symlink traversal, source/destination
conflicts, overwrite, literal shell paths, cancellation, moving, and recoverable trash.
The native MIME test checks CupidMIME routing and nonblocking special-file handling.
The Yazi terminal test covers keys, selection reaching plugins, tabs, history, filtering,
external changes, paste without X11, and trash that survives shutdown.
