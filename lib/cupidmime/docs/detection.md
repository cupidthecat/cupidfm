# Detection policy

CupidMIME identifies bytes without consulting extensions, paths, HTTP headers, the system MIME database, or a remote service. It uses a fixed set of rules compiled into the library.

Binary rules run first. A recognized binary family stops detection even when a checked field is invalid. This prevents an invalid PDF header, for example, from being reclassified as ordinary text. Prefixes of selected strong signatures receive `CM_TRUNCATED` when at least two matching bytes are available. One-byte fragments remain unknown.

Text detection runs only after binary rules fail. It accepts ASCII printable characters, tab, line feed, carriage return, form feed, and well-formed UTF-8. It rejects NUL, other ASCII controls, DEL, invalid continuation bytes, overlong encodings, surrogates, and values above U+10FFFF. This is a text heuristic: valid Unicode may still encode unusual controls, and random bytes can occasionally pass it. UTF-16 and UTF-32 currently remain binary fallback.

An incomplete UTF-8 sequence at the end of a known prefix is allowed only when its available bytes can still form a valid sequence. A complete input with that same sequence falls back to binary. Prefix text cannot establish the encoding of bytes outside the window.

After an optional UTF-8 BOM and ASCII whitespace, a complete JSON object or array receives `application/json`. The parser checks strings and escapes, surrogate pairs, number syntax, delimiters, and trailing bytes. It accepts duplicate object keys without resolving them. It does not convert numbers or enforce a numeric range. Nesting is limited to 64 recursive levels. Deeper input remains `text/plain`. JSON scalars also remain text because a word such as `true` or a number alone is weak evidence. Known prefixes never receive a JSON classification.

Selected HTML tokens are matched case-insensitively and require a delimiter. `htmlish` does not match `html`. An XML declaration prefix is case sensitive and requires whitespace after `xml`. These are token rules, not HTML or XML parsers. They do not verify the rest of the document.

## Ambiguity and limits

Font detection checks supported SFNT, collection and WOFF directories before selecting a font MIME type. Table offsets are not dereferenced until the relevant bytes are available. Readable uncompressed headers receive additional checks; compressed WOFF tables remain unchecked. A directory can establish a type while missing declared payload bytes add a truncation flag. The [font policy](fonts.md) records the limits and unsupported profiles.

EBML detection follows supported header and track metadata rather than searching for document-type strings. Declared video presence can establish WebM or Matroska in a prefix; an audio-only result needs a complete supported view. Unknown-sized children, unknown profiles, unsupported codecs and conflicting metadata prevent that audio-only claim. The [EBML policy](ebml.md) lists the checks and the schema/payload limits.

FLAC detection checks STREAMINFO and metadata extents. Ogg detection checks available pages and their CRCs; only a complete supported single logical stream can receive an audio subtype. Known prefixes and unsupported stream layouts remain generic. See the [audio policy](audio.md) for codec-header checks and payload-validation limits.

MP3 detection requires three compatible Layer III frames at computed boundaries, optionally after bounded leading ID3v2 tags. ID3 metadata alone, isolated sync bytes and unsupported candidate parameters do not establish audio. A sufficiently long known prefix can establish this frame evidence. See the [MP3 policy](mpeg.md) for supported versions and validation limits.

RIFF chunk lengths include an even-byte padding requirement. The detector walks available outer chunks, rejects extents beyond the declared container, and flags chunks that extend beyond the supplied window. It does not interpret WAVE audio parameters or nested AVI lists. gzip optional fields must fit and strings must terminate; an optional header CRC is checked. The compressed stream and trailer are still unchecked. A header-only gzip result carries `CM_TRUNCATED` when even the minimum payload and trailer space is absent.

ZIP receives a generic type even when an entry name resembles an Office document. EPUB identification requires a complete supported ZIP directory and an intact first stored `mimetype` member. Partial or unsupported archive views stay generic. The [ZIP policy](zip.md) describes the checks and restrictions. The library does not decompress content or follow nested archives. Unknown RIFF forms and ISO BMFF brands retain the binary fallback with `CM_AMBIGUOUS`. For BMFF, AVIF brands take precedence over the MP4 family; M4A takes precedence over generic MP4 brands. The detector does not inspect media tracks or identify every incompatible brand combination.

Rule order is deterministic. This release reports one result and does not enumerate polyglots. A file with one format's header and another format's content can still match the first header. Some formats, including scripts and source code, have no unique content signature and remain text.

Every offset read is guarded by the available window. Integer fields are decoded byte by byte rather than through unaligned casts. Container length comparisons use widened arithmetic or subtraction. Text parsing is linear in the bounded window, with recursion capped at 64. Binary checks do not decompress data. These limits bound inspection work; they do not establish that identified files are safe for another parser.

For uploads, treat the result as one input to your application's policy. Validate a supported format with its actual decoder before processing it. A MIME name alone cannot establish that the content is harmless.
