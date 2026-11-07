# Format coverage

This table describes the checks in the source. A structural result means these particular checks passed; it does not mean the whole file is valid.

| Family | Returned type | Checks and remaining limits |
| --- | --- | --- |
| PNG | `image/png` | Signature, complete IHDR extent, length, dimensions, color/bit-depth combinations, compression/filter/interlace fields; CRC and later chunks unchecked |
| GIF | `image/gif` | 87a/89a signature, logical screen header, nonzero dimensions; tables and frames unchecked |
| JPEG | `image/jpeg` | Start-of-image and marker prefix; segments and image data unchecked |
| WebP | `image/webp` | RIFF extent, first chunk tag, all available chunk lengths and padding extents; codec payload and padding byte values unchecked |
| WAVE | `audio/wav` | RIFF extent, WAVE form, all available chunk lengths and padding extents; audio payload unchecked |
| AVI | `video/x-msvideo` | RIFF extent, AVI form, all available outer chunk lengths and padding extents; nested lists and stream payload unchecked |
| Native FLAC | `audio/flac` | STREAMINFO fields, metadata order and extents; frames and other metadata payloads unchecked |
| MP3 | `audio/mpeg` | Three complete compatible MPEG-1/2/2.5 Layer III frame extents, optional leading ID3v2 bounds and footer agreement; payload, frame CRCs and later frames unchecked; see [MP3 policy](mpeg.md) |
| Ogg | `application/ogg`, `audio/ogg` | Available page extents, lacing, CRCs and supported single-stream sequence/continuation checks; complete supported Opus/Vorbis streams may specialize; see [audio policy](audio.md) |
| WebM / Matroska | `video/webm`, `audio/webm`, `video/matroska`, `audio/matroska` | Supported EBML profile, DocType, element extents and track-role/codec metadata; audio-only needs a complete supported view; blocks and full schema unchecked; see [EBML policy](ebml.md) |
| Fonts | `font/ttf`, `font/otf`, `font/collection`, `font/woff` | Bounded SFNT/collection/WOFF directories, table/optional-block extents, required table presence and selected readable metrics; no decompression or glyph validation; see [font policy](fonts.md) |
| ZIP | `application/zip` | Local extent, ordinary end record, directory counts/bounds, supported local/directory metadata and descriptors; unsupported layouts and features stay generic; see [ZIP policy](zip.md) |
| EPUB container | `application/epub+zip` | Complete supported ZIP layout, one first stored mimetype entry, exact payload and marker CRC; publication content unchecked |
| gzip | `application/gzip` | Fixed header, DEFLATE method, reserved bits, extra field extent, name/comment terminators and optional header CRC; minimum payload/trailer space; DEFLATE, trailer CRC and concatenated members unchecked |
| PDF | `application/pdf` | Offset-zero version line with major 1 or 2 and one decimal minor digit; objects and EOF unchecked |
| ELF | `application/x-elf` | Identification, class, byte order, version and file header length; type and section tables unchecked |
| SQLite | `application/vnd.sqlite3` | Database magic, 100-byte header, page size, read/write versions and payload fractions; pages unchecked |
| WebAssembly | `application/wasm` | Magic and version 1; sections unchecked |
| ISO BMFF | `video/mp4`, `audio/mp4`, `image/avif` | Complete 32-bit ftyp box, alignment, major and compatible brands; extended boxes and media tracks unsupported |
| ustar | `application/x-tar` | 512-byte header, ustar marker and unsigned header checksum; members and archive termination unchecked |
| JSON | `application/json` | Complete UTF-8 object or array, bounded grammar parser; policy details in [detection](detection.md) |
| HTML | `text/html` | Selected leading tokens and delimiters; not a document validator |
| XML | `application/xml` | Declaration prefix; not a document validator |
| Text | `text/plain` | ASCII/UTF-8 policy; no charset parameter inferred |
| Other or empty | `application/octet-stream` | Conservative fallback |

`application/x-elf`, `application/x-tar`, and `video/x-msvideo` are compatibility names. WebM MIME names follow the WebM project guidelines. These spellings should not be read as a claim that all returned names are registered with IANA. Aliases and subclass queries are not part of this API.

Current gaps include Office and other ZIP subtypes, ZIP64 and broader archive layouts, full archive validation, MPEG layers I/II and free-format rates, other Ogg codec mappings and stream layouts, broader EBML layouts and WebM codecs, WOFF2 and legacy font profiles, PE executables, legacy text encodings, and a representative external fixture corpus. See [open issues](https://github.com/cupidthecat/cupidMIME/issues) for scoped follow-up work. There is no claim of parity with libmagic or Shared MIME-info.
