# Archive extraction

Press e to extract the selected archives beside their source files. With no selection, the
command uses the hovered file. Press E to choose an existing output directory first; Tab
completes directory names. Both commands work in either browser profile. Configured bindings
in the original profile take precedence.

Opening a recognized archive also queues extraction. O offers Extract here and Reveal parent
directory. Custom MIME rules can choose other openers. The built-in extract opener always
queues jobs; defining opener.extract.command replaces it with a shell command, whose block
and orphan settings then apply. See examples/config/openers.cupidconf.

## Output folders

Each archive gets a new folder. For example, source.tar.gz becomes source. Existing folders,
files, and symlinks are preserved; collisions use source_1, source_2, and so on, up to 9999.
Selected archives from different directories normally extract beside each source. E sends
all of them to the chosen directory.

CupidFM decodes into a private staging directory under the output parent, then publishes the
completed folder without replacing an existing destination. Failed decoding removes the
staging tree. Cancelling before placement stops and reaps the worker and removes its staged
output. A folder already placed before cancellation remains. A cleanup failure is reported
in task details.

The output parent must exist and be writable. Folder paths retain the browser's 1023-byte
limit, with space reserved for a collision suffix. Archive member names and decoding work
also follow CupidArchive's limits, including a 1 GiB decoded-byte limit and 64 path levels.

Archive previews keep each member on one row. Names and link targets display line breaks
and tabs as escaped text, and clipping uses terminal cells so multibyte names remain intact.

## Tasks and metadata

Use w to inspect extraction jobs. Arrows or j/k move through the list, g/Home and G/End jump
to either end, and PageUp/PageDown move by a page. Enter shows the archive, output path, or
error details; x cancels a job. q, w, or Esc closes the task list.

Each archive is one task. The browser remains usable during decoding. Progress reports archive
completion, without a live extraction percentage or aggregate byte estimate. Failed shell
and extraction jobs retain their captured diagnostics alongside the exit error.

Regular rwx permissions, including executable bits, and supported modification times are
restored. Ownership and special mode bits are not restored. Cleanup can make private staged
directories writable without following their symlinks; it does not change published folders
or external link targets.

## Formats and remaining work

CupidArchive supplies TAR, supported ZIP and 7z layouts, and gzip, bzip2, and xz wrappers.
Supported 7z methods include Copy, LZMA1, and LZMA2, with the library's solid-folder restrictions.
Standalone compressed files are placed inside their output folder. Empty archives produce
empty folders.

This workflow does not support every archive format, encrypted entries, password prompts,
or a merge/overwrite policy. It always keeps an enclosing output folder, including archives
with one member, and does not recursively unpack archive members. The library's
[reference](../lib/cupidarchive/docs/reference.md) describes format and extraction limits.

## Verification

    make test CC=clang
    python3 tests/test_extract_smoke.py

The terminal test covers archive opening, both browser profiles, output-directory choice,
numbered collisions, literal shell punctuation and newlines, ZIP/TAR/7z, compression wrappers,
symlinks and hardlinks, executable permissions, invalid paths and checksums, read-only staging
cleanup, cancellation, and recovery. A FIFO source must fail promptly.
