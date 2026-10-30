# CupidArchive

CupidArchive is a C library for listing and extracting archives. It uses zlib, libbz2, and liblzma for decompression.

## Build

Install a C compiler, Make, and the zlib, bzip2, and xz development headers. Python 3 and GNU tar are used by the regression suite.

```sh
make -j
make test
```

Include `cupidarchive.h` and link `libcupidarchive.a -lz -lbz2 -llzma`. The build tracks compiler flags when switching between normal and sanitizer objects.

## Formats

| Reader | Supported data |
| --- | --- |
| TAR | USTAR, GNU/PAX metadata, and old GNU/PAX sparse variants |
| ZIP | Store/Deflate, ZIP64, central directories, and local-header recovery |
| 7z | Copy/LZMA1/LZMA2, multiple files/folders, solid substreams, and encoded tables |
| gzip, bzip2, xz | Standalone compressed files or archive wrappers, including concatenated members |

7z folders use one coder each. Encryption, multi-volume data, coder/filter chains, anti-items, and Unix special-file records are unsupported. ZIP encrypted entries and symlink/hardlink interpretation are unsupported.

## Use the library

Open with `arc_open_path` or `arc_open_stream`, iterate with `arc_next`, and release each returned `ArcEntry` with `arc_entry_free`. Use `arc_open_data` to read the current file, then close the data stream before advancing or closing its reader. `arc_format` reports the decoded container.

A successful `arc_open_stream` reader owns its supplied stream; a failed open leaves ownership with the caller. Standalone compressed entries use size zero for unknown decoded length, so read until EOF.

Extraction stages regular files and traverses destination parents through directory descriptors. A failed member preserves its old destination; successful earlier members remain. TAR hardlinks, sparse holes, and deferred directory metadata are supported. Permissions and timestamps are preserved; ownership is not restored.

## Documentation

- [Wiki home](https://github.com/cupidthecat/cupidarchive/wiki): formats, APIs, and limits.
- [Full reference](https://github.com/cupidthecat/cupidarchive/wiki/Reference) or its [source copy](docs/reference.md): architecture and complete C examples.
- [7z notes](https://github.com/cupidthecat/cupidarchive/wiki/SevenZip): multi-file/solid behavior and validation.
- [Test fixtures](tests/fixtures/README.md): independent producers and regeneration commands.

`ArcLimits` bounds entry counts, names, metadata, decoded bytes, and extraction depth. Zero fields select defaults; the default decoded limit is 1 GiB. Listing metadata alone does not validate file contents. See the reference for error and partial-extraction behavior.

Licensed under [GPL-3.0](LICENSE).
