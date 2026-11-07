# Standards and source notes

MIME standards define media types and message representation. They do not make a content detector a validator. CupidMIME uses them for names and policy, with format specifications supplying header constraints.

| Reference | Use in CupidMIME |
| --- | --- |
| [RFC 2045](https://www.rfc-editor.org/rfc/rfc2045) | MIME type/subtype and parameter model. API output is the media type essence; transfer encodings are not decoded. |
| [RFC 2046](https://www.rfc-editor.org/rfc/rfc2046) | Core media-type families and generic binary fallback. This library does not parse multipart messages or implement mail defaults. |
| [RFC 6838](https://www.rfc-editor.org/rfc/rfc6838) | Media-type naming and registration conventions. Detection rules do not create new registrations. |
| [IANA registry](https://www.iana.org/assignments/media-types/) | Registered media names and links to their specifications. The compiled rules are a selected set, not a downloaded registry. Compatibility names are identified in the coverage table. |
| [WHATWG MIME Sniffing](https://mimesniff.spec.whatwg.org/) | Signature and token-boundary reference. CupidMIME has no HTTP context, supplied MIME type, or browser execution policy, and does not claim algorithm conformance. |
| [Shared MIME-info](https://specifications.freedesktop.org/shared-mime-info/latest/ar01s02.html) | Reference for magic priorities, aliases and subclass relationships. CupidMIME uses explicit rule order and generic container fallback. It does not load magic XML, filename globs, aliases, or subclass databases. |

## Format references

- [PNG specification](https://www.w3.org/TR/png-3/): IHDR fields and legal color/bit-depth combinations.
- [SQLite file format](https://sqlite.org/fileformat.html): database header and page-size encoding.
- [PKWARE APPNOTE](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT): ZIP local headers and end records.
- [EPUB Open Container Format](https://www.w3.org/TR/epub-33/#sec-zip-container) and [IANA registration](https://www.iana.org/assignments/media-types/application/epub%2Bzip): stored mimetype member and media-type spelling. [ZIP policy](zip.md) documents CupidMIME's narrower supported archive layouts.
- [RFC 1952](https://www.rfc-editor.org/rfc/rfc1952.html): gzip fixed header fields.
- [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259.html): JSON grammar. CupidMIME's object/array restriction, depth bound, BOM tolerance, and surrogate pairing policy are detection choices.
- [RFC 9639](https://www.rfc-editor.org/rfc/rfc9639.html), [RFC 3533](https://www.rfc-editor.org/rfc/rfc3533.html), [RFC 7845](https://www.rfc-editor.org/rfc/rfc7845.html), and the [Vorbis I specification](https://xiph.org/vorbis/doc/Vorbis_I_spec.html): native FLAC metadata, Ogg pages and checksums, and supported codec identification headers. The [audio policy](audio.md) records the narrower detection checks.
- [ID3v2.2](https://id3.org/id3v2-00), [ID3v2.3](https://id3.org/id3v2.3.0), [ID3v2.4 structure](https://id3.org/id3v2.4.0-structure), [WHATWG MP3 sniffing](https://mimesniff.spec.whatwg.org/#signature-for-mp3-without-id3), and the [MPEG audio registration](https://www.iana.org/assignments/media-types/audio/mpeg): leading tag extents, frame-boundary evidence and MIME spelling. The [MP3 policy](mpeg.md) describes the three-frame requirement and unsupported cases.
- [RFC 8794](https://www.rfc-editor.org/rfc/rfc8794.html), [RFC 9559](https://www.rfc-editor.org/rfc/rfc9559.html), and the [WebM container guidelines](https://www.webmproject.org/docs/container/): EBML encodings, container profiles, track metadata and MIME names. The [EBML policy](ebml.md) records the supported subset and the stronger evidence required for audio-only results.
- [OpenType font files](https://learn.microsoft.com/en-us/typography/opentype/spec/otff), [WOFF 1](https://www.w3.org/TR/WOFF/), and [RFC 8081](https://www.rfc-editor.org/rfc/rfc8081.html): font directories, wrappers and MIME names. The [font policy](fonts.md) lists checked fields and the unsupported font profiles.

These references guide individual checks. The [coverage table](formats.md) records what the code actually verifies and what it leaves unchecked.
