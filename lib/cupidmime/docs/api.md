# Library and CLI API

Include `cupidmime.h` and link `libcupidmime.a`. The public API is C99 and has C++ linkage guards. No library call allocates memory or changes process state. The library has no mutable global data, locale dependency, filesystem access, or dependency on libmagic.

`cm_detect(data, size, &result)` takes a complete byte buffer. `cm_detect_prefix` takes a known prefix. The caller owns the buffer and must keep at least `size` bytes readable during the call. The detector examines at most `CM_SCAN_LIMIT` (1 MiB); larger buffers automatically receive `CM_PARTIAL`. The result's `examined` field is the inspection window size, not a count of individual byte loads.

Both calls return `CM_OK` when identification ran, including unknown, empty, or malformed input. A null output pointer, or null data with nonzero size, returns `CM_INVALID_ARGUMENT` without changing the output. Null data with zero size is allowed. A zero-length buffer returns `application/octet-stream`.

The result contains static strings. Do not free or modify them. `mime` is a lowercase type/subtype without parameters. `reason` explains the current rule; its wording may change. Do not parse it as an identifier.

| Evidence | Meaning |
| --- | --- |
| `CM_FALLBACK` | No supported format was established |
| `CM_TEXT` | The inspected window passed the text policy |
| `CM_SIGNATURE` | A format signature or leading text token matched |
| `CM_STRUCTURE` | The documented format fields were checked |

These categories describe the kind of evidence, not a probability or a validation guarantee.

Flags are independent bits:

| Flag | Meaning |
| --- | --- |
| `CM_TRUNCATED` | A recognized header or declared extent needs more bytes than the inspection window contains |
| `CM_MALFORMED` | A constraint checked by the detector failed |
| `CM_PARTIAL` | The caller supplied a prefix, or the buffer exceeded the scan limit |
| `CM_AMBIGUOUS` | A container subtype or format candidate remains unresolved |

Generic ZIP retains `CM_AMBIGUOUS` even after the supported directory checks pass. It means the subtype remains unresolved, not that directory corruption was found. Contradictory checked ZIP metadata adds `CM_MALFORMED` while retaining the generic family. See [ZIP and EPUB detection](zip.md) for supported layouts and the effect of partial input.

Generic Ogg also retains `CM_AMBIGUOUS`. Its audio subtype requires a complete supported single stream; `cm_detect_prefix` never returns `audio/ogg`. Native FLAC can be established from a complete STREAMINFO block in a prefix. See [audio detection](audio.md).

ID3 metadata and unconfirmed Layer III candidates can retain the binary fallback with `CM_AMBIGUOUS`. Three complete compatible Layer III frames establish `audio/mpeg`, including in a known prefix. Truncated candidates remain unconfirmed. See the [MP3 policy](mpeg.md) for tag checks and unsupported cases.

EBML document and track metadata can establish declared video presence in a prefix. Audio-only WebM/Matroska requires a complete supported resource view; neither audio type is returned by `cm_detect_prefix`. Unknown profiles and unresolved track/layout evidence keep the binary fallback. See the [EBML policy](ebml.md).

Font types require complete supported directories, but their declared payloads can extend beyond the window. A positive font MIME type may therefore carry `CM_TRUNCATED` and `CM_PARTIAL`. Compressed WOFF table contents are not decoded. See the [font policy](fonts.md) for caps, sharing rules and readable-header checks.

A RIFF or ZIP result can retain its MIME type while carrying `CM_TRUNCATED`: enough header evidence remains to identify the family. An incomplete PNG IHDR instead yields the binary fallback. `CM_TRUNCATED` describes the available bytes; with `CM_PARTIAL`, it does not establish that the original file is truncated. Flags report only conditions the detector observes. Zero flags do not establish completeness.

## CLI

```sh
cupidmime [-v] [--] FILE...
cupidmime --help
cupidmime --version
```

Use `-` for standard input. Each result prints `path: type/subtype`. `-v` adds numeric evidence, the flags bitmask, window size, and reason. Use `--` before paths beginning with a dash. Multiple files are processed in order; a read error does not prevent later files from being inspected.

The CLI reads at most 1 MiB plus one byte per input to distinguish a bounded prefix from a complete input. It supports pipes without seeking. It does not drain a pipe after reaching the limit. In a pipeline, the producer may therefore observe a closed pipe.

Exit status is 0 for successful reads, even if the type is unknown or a format is malformed; 1 for allocation or file I/O errors; and 2 for usage errors. The CLI output is intended for people. Filenames are printed literally and may contain newlines or terminal control characters; use the library for structured integration.

The API is at version 0.1.0. ABI stability across future releases is not promised yet.
