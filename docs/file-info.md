# File information

Press Tab in the Vim profile, or the configured file-info key in the original profile
(Ctrl+T by default), to open the inspector.

The inspector shows the selected paths. With no selection, it uses the visible listing and
starts at the hovered entry. Files outside the current directory remain available when they
belong to the selection. Inspection keeps the selection intact.

## Controls

| Keys | Action |
| --- | --- |
| j/k, Up/Down | Next or previous field |
| h/l, Left/Right | Previous or next file |
| g/G, Home/End | First or last field |
| PageUp/PageDown | Move by a page of fields |
| c then c | Copy the selected field's full value |
| r | Refresh metadata |
| Tab, Esc, Ctrl+C, q/Q | Close |
| F1, ~ | Open help |

Copying uses the existing text clipboard backends, including the terminal fallback. Displayed
values are clipped and control characters are shown safely; copying retains the original value,
including line breaks in names and paths. The selected field highlights its entire row.

Swiping files stays within the inspector's path list and keeps the selected field when it
is available in the next file. On close, the browser hovers the inspected
file when it remains in the visible listing. Inspecting a selection outside the current view
does not change the current directory.

## Fields and updates

Fields include the name, full path, object type, MIME type for regular files, size, permissions,
owner and group, timestamps, inode number, and hardlink count. Symbolic links show their own
metadata and target text, including broken links. FIFOs, sockets, and device nodes are inspected
without opening their contents.

MIME types use CupidFM's existing detection and cache. A failed system backend can still use
the native detector. Birth time is shown as a dash when the filesystem does not provide it.
Changed time is the metadata-change time, not a substitute for creation time.

For directories, Entry size describes the filesystem entry. Directory size uses the background
size cache and updates while the inspector remains open. Pending calculations and cache limits
have explicit labels. Other metadata refreshes when switching files or pressing r. A deleted
or inaccessible path keeps its name and path fields and displays the error.

The table has a fixed field set. It does not provide a grid summarizing every selected file
at once or a configurable inspector plugin lifecycle.

## Verification

    make test CC=clang
    python3 tests/test_inspector_smoke.py

Tests cover literal names and copied paths, file navigation, selection preservation, directory
size updates, broken and deleted links, FIFOs, resizing, permissions and metadata, and both
browser profiles. The core metadata test also checks refresh ownership and hardlink counts.
