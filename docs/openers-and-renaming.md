# Openers and bulk rename

In the Vim browser profile, o or Enter opens the selection, or the hovered file when nothing
is selected. Use l or Right to enter a directory; opening a folder uses its configured opener. Files with the same default opener run
together; different file types can use different commands.

Press O to choose an opener. The list contains choices shared by every selected file, followed
by Custom command. Use j/k or arrows to select, / for live fuzzy matching, Enter to choose, and
Esc to clear the query or cancel. A custom command receives selected paths in "$@".

## Configure openers

Copy examples/config/openers.cupidconf to ~/.cupidfm/openers.cupidconf, or set CUPIDFM_OPENERS
to another file. CupidFM rereads it when opening files or starting a bulk rename. An invalid
configuration reports an error and leaves the files unopened.

Each opener has a name and these fields:

| Field | Meaning |
| --- | --- |
| command | Shell command; "$@" contains the literal selected paths |
| description | Text shown in the opener picker; defaults to the name |
| block | Wait with the terminal available to the command |
| orphan | Detach a nonblocking command so it can outlive CupidFM |
| for | any, unix, linux, macos, windows, or android; Linux/WSL uses any, unix, and linux |

A blocking opener takes precedence over its orphan setting. Other nonblocking commands run
as tasks, where w shows their status, errors, output, and cancellation control. Detached
commands have their own session and discard terminal input and output.

Rules use rule.NAME.path, rule.NAME.mime, and rule.NAME.use. Their first appearance determines
their order. All selectors present on a rule must match. Path patterns containing a slash match
the full absolute path; other patterns match the filename. MIME patterns match the detected
type. Patterns support shell-style wildcards and brace alternatives such as *.{png,jpg}.
The use field lists opener names separated by commas or spaces; repeated use fields append
choices. The first matching rule replaces the built-in choice list for that file.

Built-in text openers use $EDITOR, or vi when unset. Image and other files use xdg-open; media
opens detached. Reveal opens each file's parent directory. A custom rule can override these
openers by name or define another one. Use folder/* for a directory MIME selector.

## Rename several entries

Select entries and press r to edit a name list. The configured original-profile rename key also
accepts multiple selections. CupidFM writes their paths relative to their common parent into a private
temporary file and opens the first matching blocking text opener. One line corresponds to one
selected entry. Change the names without adding, removing, or reordering lines. Absolute
destinations are accepted; relative destinations use the common parent.

After the editor exits successfully, CupidFM shows the changed paths. Enter approves the batch;
Esc cancels it. A hovered entry with no selection uses the single-entry prompt, with the cursor before its
extension. Directory names start with the cursor at the end.

Rename chains run from the free end so one move can release the next destination. Existing
destinations outside the batch, duplicate destinations, malformed lists, and overlapping sources
are rejected before approval. The destination's parent must already exist. Sources are checked
against their original device and inode before each rename; a replaced source is left alone.

Each move preserves an existing destination. Rename cycles fail without replacing either file.
Independent successes remain in place, failures are listed, and selections follow successful
renames. A batch has no undo entry. Names containing line breaks cannot use the line-based list.

## Create several entries

Press A in the Vim profile to open an empty list in the configured blocking text opener.
Write one name per line; end a directory name with /. Blank lines are ignored. Paths are
relative to the current physical directory unless they start with /. Parent directories
are created as needed.

CupidFM shows the proposed paths after the editor exits. Enter queues the whole list;
Esc cancels it. The review's fuzzy query only changes which rows are visible. It does
not change which entries will be created. Use w to inspect progress, errors, or cancel
the task. Existing files are preserved, existing directories are accepted, and a failed
entry does not prevent later entries from being attempted. Cancellation keeps completed
entries. Bulk creation has no undo entry.

Lists accept up to 65536 paths within the browser's existing path-length limit. Embedded
NUL bytes or oversized entries reject the list before anything is queued. Line breaks
cannot form part of a name in this format.

## Check the workflows

Run make test for opener rules, picker behavior, rename ordering, conflicts, and source replacement.
Run python3 tests/test_open_bulk_smoke.py for grouped dispatch, literal shell arguments,
common opener choices, detached commands, review cancellation, and selection updates.

Recognized archives offer the built-in extract opener. See [archive extraction](archive-guide.md) for jobs, output folders, and limits.
