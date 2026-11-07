# FLAC and Ogg detection

Audio detection uses the same bounded buffer API as other formats. It does not decode samples or load codec libraries. Tests can optionally use FFmpeg to check files produced by an independent encoder; FFmpeg is not a build or runtime dependency.

For MPEG Layer III frames and leading ID3 metadata, see the separate [MP3 policy](mpeg.md).

## Native FLAC

The detector requires the `fLaC` marker followed by a complete 34-byte STREAMINFO block. It checks minimum and maximum block sizes, the ordering of known minimum/maximum frame sizes, and the encoded sample depth. Block sizes must be at least 16 samples, the maximum must not be smaller than the minimum, and sample depth must be at least 4 bits. The field encoding limits sample depth to 32 bits and channel count to 1 through 8.

A valid STREAMINFO establishes `audio/flac`. The detector then walks metadata headers and their declared payload extents until the last-block flag is found. A second STREAMINFO block or forbidden metadata type 127 sets `CM_MALFORMED`. Missing headers or payload bytes set `CM_TRUNCATED`. The FLAC type remains available when a later metadata check fails.

Unknown frame sizes encoded as zero are allowed. A zero sample rate is also allowed because RFC 9639 permits it for non-audio data. The media-type name describes the FLAC format; it does not establish that the content is playable audio.

Metadata payloads beyond STREAMINFO are skipped by length. The detector does not parse comments, pictures or seek tables, validate audio frames, compute the decoded-audio MD5, or require an audio frame after the last metadata block. A prefix can establish FLAC from STREAMINFO while retaining `CM_PARTIAL`.

## Ogg page checks

The `OggS` capture pattern establishes the generic `application/ogg` family with `CM_AMBIGUOUS`. Before a complete page is checked, the evidence category is `CM_SIGNATURE`. A page passing its structural and CRC checks raises it to `CM_STRUCTURE`.

The supported walk checks page version zero, reserved header bits, segment-table bounds, the sum of lacing values, page payload extent, and each available complete page's CRC. Ogg uses an MSB-first CRC with polynomial `0x04c11db7`; the stored checksum field is treated as zero during the calculation. It also checks that the first page begins a stream at sequence zero without a continued packet.

For a single logical stream, later page sequence numbers must increase and packet continuation flags must agree with the preceding lacing table. An end page cannot leave a packet unfinished. Missing bytes or a missing end page set `CM_TRUNCATED`; a failed checked constraint sets `CM_MALFORMED`. These conditions retain the generic family.

Chained streams, new beginning pages and changes in stream serial number stop the walk with a generic ambiguous result. They can be valid Ogg, so this restriction does not itself mark them malformed. Pages beyond that point are not checked. Generic Ogg is a family identification, not a claim that the stream satisfies the media registration's full content requirements.

## Audio specialization

`audio/ogg` requires the entire supplied resource to fit within the scan window, one logical stream from beginning through end, and a supported audio identification packet occupying the beginning page alone:

- Opus version 1, mapping family zero, a 19-byte identification packet, and one or two channels.
- Vorbis version zero, a 30-byte identification packet, nonzero channel count and sample rate, valid ordered block-size exponents from 6 through 13, and a set framing bit.

Other Opus versions or mapping families and other Vorbis versions stay generic. Known identification packets with invalid checked fields set `CM_MALFORMED`. Packets split across beginning pages and unknown codec signatures stay generic. A known prefix never receives `audio/ogg`, even if it contains a complete-looking shorter stream: later streams could change the classification.

Audio specialization identifies the declared codec family and the checked single-stream page layout. It does not validate comment/setup packets, granule-position semantics, codec payloads or decoded samples. An arbitrary payload behind a plausible identification packet can still pass these container checks. Ogg FLAC, Speex, Theora, Skeleton and other mappings are not specialized yet.

## Sources

Native FLAC checks follow [RFC 9639](https://www.rfc-editor.org/rfc/rfc9639.html). Ogg page layout and checksums follow [RFC 3533](https://www.rfc-editor.org/rfc/rfc3533.html). Codec identification fields come from [RFC 7845](https://www.rfc-editor.org/rfc/rfc7845.html) and the [Vorbis I specification](https://xiph.org/vorbis/doc/Vorbis_I_spec.html). Media-type spellings are listed by IANA for [FLAC](https://www.iana.org/assignments/media-types/audio/flac), [Ogg audio](https://www.iana.org/assignments/media-types/audio/ogg), and [generic Ogg](https://www.iana.org/assignments/media-types/application/ogg).
