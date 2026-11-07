# Font container detection

CupidMIME identifies modern SFNT TrueType/OpenType fonts, font collections and WOFF 1 from checked directories and selected readable headers. It does not render text, interpret glyph programs or decompress font tables. The MIME names are `font/ttf`, `font/otf`, `font/collection` and `font/woff`.

## SFNT profile

Supported SFNT version values are `0x00010000` for TrueType outlines and `OTTO` for CFF/CFF2 OpenType outlines. The directory must be complete in the inspection window. The detector derives the search fields from the table count and checks their stored values, rather than trusting them as search instructions.

Table tags must be printable ASCII in strictly increasing order. Duplicate tags, unaligned offsets, table collisions and overlaps with protected headers/directories are rejected. Counts from 1 through 128 are inspected; larger counts within the SFNT field's supported range remain ambiguous. Zero or out-of-range counts are malformed. Offset/length arithmetic uses widened values before any buffer access.

The profile requires the common `OS/2`, `cmap`, `head`, `hhea`, `hmtx`, `maxp`, `name` and `post` tables, with minimum declared lengths. TrueType also needs `glyf` and `loca`; CFF OpenType needs `CFF ` or `CFF2`. The minimum `OS/2` size is 78 bytes. Older Apple flavors, historic shorter table variants and bitmap-only profiles are not covered by this rule.

When the relevant uncompressed fields are available, the detector checks:

- `head` version, magic number, units-per-em range, location format and glyph-data format.
- `maxp` version for the outline flavor and nonzero glyph count.
- Horizontal metric count and the required `hmtx` extent against that glyph count.
- The minimum `loca` extent implied by glyph count and location format.

It does not check every field in these tables. A complete directory may establish the font type while declared table data lies outside the available window; that result carries `CM_TRUNCATED`. In a known prefix or scan-limited buffer, this flag does not establish that the original file is damaged.

## Collections

Collection headers support TTC versions 1.0 and 2.0, including CFF OpenType faces. Up to 32 faces are inspected. Every face directory must be present, aligned and separate from the collection header and other face directories. Each face then passes the supported SFNT checks using offsets from the beginning of the collection.

Tables may be shared across faces. Overlapping cross-face references are accepted only when their tag, checksum field, offset and length records are identical. Partial overlap or contradictory sharing metadata is rejected. Version 2 signature fields must agree on presence and extent and must not overlap protected metadata or tables. The signature payload itself is not verified.

Fixed table/face limits bound the pairwise overlap and sharing checks without allocating memory. Counts above these limits remain unresolved instead of expanding inspection work with untrusted counts.

## WOFF 1

WOFF detection requires its complete fixed header and table directory, a supported SFNT flavor, a zero reserved field, and a plausible declared file length. Table tags, alignment and overlap rules are checked against stored extents, including table padding extents. A stored length cannot exceed the original length, and a positive original length cannot have a zero-byte stored representation.

The detector recomputes `totalSfntSize` from the original table lengths and their padding, using 64-bit arithmetic. It checks metadata/private-block presence fields, alignment, ordering and extents, and prevents those blocks from overlapping tables or each other. Declared bytes beyond the available window set `CM_TRUNCATED`; extents beyond the WOFF file's declared length are malformed. Extra available bytes after that declared length are rejected.

Required table presence and minimum original lengths are checked even for compressed tables. When stored and original lengths are equal, selected header fields can also be checked as raw data. Compressed data is left untouched. A plausible directory around damaged compressed bytes can therefore still identify `font/woff`.

WOFF2 is not implemented yet. Its full `wOF2` signature currently yields the binary fallback with `CM_AMBIGUOUS`; no `font/woff2` claim is made.

## Validation limits and testing

The detector does not verify table checksums, checksum adjustment, signature authenticity, compression streams, metadata XML, character maps, glyph outlines/programs, CFF programs, shaping tables, padding byte values or complete font conformance. It checks the `loca` table's required extent but not every glyph offset inside it. Empty glyph data and extra platform-specific tables may be present. A positive result is not evidence that a renderer can use the font safely.

Core C tests use synthetic metadata fixtures and exact-allocation prefixes. Optional `make integration-fonts` checks an installed font, a WOFF wrapper and a shared-table collection through both CupidMIME and Fontconfig's `fc-scan`. The reference wrapper writer uses Python's standard library. No font files are bundled; temporary derivatives are removed after the test. Use a complete source font within the scan limit for the no-condition-flags integration check.

## Sources

The [OpenType font-file specification](https://learn.microsoft.com/en-us/typography/opentype/spec/otff), [head](https://learn.microsoft.com/en-us/typography/opentype/spec/head), [maxp](https://learn.microsoft.com/en-us/typography/opentype/spec/maxp), [hhea](https://learn.microsoft.com/en-us/typography/opentype/spec/hhea), and [loca](https://learn.microsoft.com/en-us/typography/opentype/spec/loca) descriptions define the directory and checked fields. The [WOFF 1 specification](https://www.w3.org/TR/WOFF/) defines wrapper extents and reconstruction size. [RFC 8081](https://www.rfc-editor.org/rfc/rfc8081.html) and the IANA registrations for [ttf](https://www.iana.org/assignments/media-types/font/ttf), [otf](https://www.iana.org/assignments/media-types/font/otf), [collection](https://www.iana.org/assignments/media-types/font/collection) and [woff](https://www.iana.org/assignments/media-types/font/woff) supply the MIME names.
