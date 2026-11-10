# WebM and Matroska detection

CupidMIME parses selected EBML metadata to identify WebM and Matroska. A bare EBML signature or an embedded `webm` string is insufficient. The rule needs a supported EBML header, an exact document type, a Segment, and checked track identification fields.

## Integer and element boundaries

Element identifiers use one through four bytes and retain their marker bit. Data-size integers use one through eight bytes and remove the marker bit. The parser rejects zero marker bytes, reserved or nonminimal identifiers, and widths beyond its supported or declared limits. Size integers may use a longer encoding than necessary. The all-ones size value is treated as unknown, never as a byte count.

Every element header is checked against both the available window and its declared parent. A child extending past a known parent is malformed even when the file contains enough subsequent bytes. A declared child that fits its parent but exceeds the available window is truncated. Size arithmetic uses 64-bit values; an untrusted length is compared before it becomes an offset.

The parser is iterative. It follows only the levels needed for header and track metadata, skips other finite elements by length, and does not recursively descend through arbitrary media payloads.

## Supported header profile

The EBML header must have a finite, fully available extent and one `DocType` field. The supported values are exactly `webm` and `matroska`. Normal null padding is allowed; nonzero bytes after a string terminator remain ambiguous. Other document types do not receive an invented MIME name.

The detector checks duplicate singleton header fields, writer/read-version consistency, identifier-width limits and size-width limits. Missing numeric fields use their EBML defaults, as do empty fields with a defined default. The supported profile requires EBML read version 1, maximum identifier length 4, maximum size length from 1 through 8, and document read version at most 2 for WebM or 4 for Matroska. Newer writer versions remain possible when their declared reader requirements fit this profile. Unsupported reader profiles stay ambiguous.

Void and CRC-32 elements may precede the Segment. Their extents are checked, but CRC values are not verified. Other layouts before the Segment remain unresolved.

## Tracks and MIME selection

One finite, complete Tracks table is supported. Track entries need a nonzero TrackNumber, a nonzero TrackType, and a nonempty ASCII CodecID. Duplicate role/identity/codec fields, duplicate track numbers, duplicate Tracks tables, and contradictory audio/video metadata are rejected. The rule checks a maximum of 128 entries, using a fixed stack array to track their numbers. More entries remain ambiguous rather than triggering allocation or an unbounded pairwise scan.

WebM video declarations currently support `V_VP8` and `V_VP9`; audio declarations support `A_OPUS` and `A_VORBIS`. Other codecs, including AV1, remain unresolved in this release. Matroska permits other nonempty codec IDs. Known `A_`/`V_` declarations must not contradict the track role, and an audio track must not carry Video settings or vice versa. Codec configuration payloads are not validated.

| Evidence | Result |
| --- | --- |
| Supported document with a checked video track | `video/webm` or `video/matroska` |
| Complete supported Segment view, only audio tracks, complete input within the scan limit | `audio/webm` or `audio/matroska` |
| Unknown document/profile/codec, missing role evidence, other-only tracks or unsupported layout | `application/octet-stream` with `CM_AMBIGUOUS` when the condition is observed |

A known prefix can establish declared video presence. It cannot establish an audio-only resource, even when it contains a complete-looking Segment. Truncation or an inspection stop after video metadata can retain the video MIME name with condition flags. A contradiction found during the supported walk leaves the binary fallback with `CM_MALFORMED`.

Known-sized Segments are checked against their declared end. An unknown-sized Segment is supported through the end of a complete supplied resource when all inspected children have finite extents. Unknown-sized children, including Clusters, stop inspection. Multiple EBML documents or Segments, trailing data outside a known Segment, and unresolved boundaries prevent audio-only specialization. These layouts can be valid in broader EBML usage; a policy restriction alone is not proof of corruption.

## Validation limits

These rules identify declared container and track metadata. They do not decode blocks, validate codecs, verify EBML CRC elements, parse SeekHead references or Cues, inspect nested Cluster contents, or validate every Matroska schema requirement. TrackUID uniqueness, track numbers referenced by blocks, timestamps, enabled-track semantics, and complete Video/Audio settings are unchecked. Some valid files fall outside the supported layout; constructed metadata can identify a container whose media payload is unusable.

The names `audio/webm` and `video/webm` follow the WebM project's container guidelines. Matroska uses the registered `audio/matroska` and `video/matroska` names rather than the older `x-matroska` aliases.

## Sources

- [RFC 8794](https://www.rfc-editor.org/rfc/rfc8794.html): EBML integer encodings, element sizes, header fields and defaults.
- [RFC 9559](https://www.rfc-editor.org/rfc/rfc9559.html) and the [Matroska element specification](https://www.matroska.org/technical/elements.html): Matroska profiles, Segment/Tracks metadata and media types.
- [WebM container guidelines](https://www.webmproject.org/docs/container/): WebM document type, codecs and audio/video MIME names.
- IANA registrations for [audio/matroska](https://www.iana.org/assignments/media-types/audio/matroska) and [video/matroska](https://www.iana.org/assignments/media-types/video/matroska).
