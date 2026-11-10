# MP3 and ID3 detection

CupidMIME identifies MPEG-1, MPEG-2 and MPEG-2.5 Layer III audio as `audio/mpeg`. It requires three complete compatible frames, starting at offset zero or immediately after supported leading ID3v2 tags. It does not search arbitrary payload bytes for a sync pattern.

## Frame evidence

Each checked header must have the MPEG sync bits and Layer III identifier, a supported version, a nonreserved sample-rate index, a nonreserved bitrate index, and a nonreserved emphasis value. Free-format bitrate index zero stays unresolved because this implementation does not infer its frame length.

Frame lengths use the Layer III bitrate table and the encoded sample rate. MPEG-1 uses `floor(144 * bitrate / sample_rate) + padding`; MPEG-2 and MPEG-2.5 use a scale of 72. The sample-rate base values are 44,100, 48,000 and 32,000 Hz, divided by two for MPEG-2 and by four for MPEG-2.5. Computation uses integer arithmetic. The resulting extent must fit within the inspection window and leave room for the fixed header, optional CRC field and version/channel-dependent side information.

The next header must appear at that computed boundary. Three complete frames must agree on version, sample rate and channel count. Bitrate and padding may change, so variable-bitrate streams are supported. Stereo and joint-stereo modes may differ while preserving two channels. A mono/stereo transition within these frames remains unresolved.

A complete three-frame match receives `CM_STRUCTURE`. The detector then stops; later frames, trailing tags and extra bytes are not inspected. It does not verify protected-frame CRCs, side-information contents, bit reservoirs or decoded samples. Constructed headers around arbitrary payloads can still pass identification. This is format evidence rather than a decoder verdict.

## Leading ID3 tags

ID3 alone does not establish MP3. The tag specification permits other encoded audio and standalone metadata. CupidMIME checks supported leading tag headers and skips to the declared outer boundary before looking for Layer III frames.

Supported major versions are 2, 3 and 4. The detector checks forbidden version/revision values, version-specific reserved header flags, and the four-byte synchsafe size. Tag extents must fit the inspection window. For version 3 or 4 tags with an extended-header flag, it checks the minimum and declared extended-header extent inside the tag. For version 4 footers, the `3DI` marker, version, flags and size must agree with the header; the footer's ten bytes are outside the declared body size.

Consecutive leading tags are allowed. Even an empty-body tag advances the cursor by ten bytes, so repeated tags cannot stall the walk. Unsynchronization and compressed tag contents are skipped using the outer declared size. Tag frames, text encodings, unsynchronization correctness, extended-header contents, optional metadata CRCs and padding contents are not validated.

An ID3-tagged FLAC file currently stays unresolved; this code does not dispatch from an ID3 boundary into every other detector. MPEG layers I/II, free-format Layer III, unsupported ID3 versions, interspersed metadata, damaged streams requiring resynchronization, and files with fewer than three complete frames also remain outside the supported positive rule.

## Conditions and partial input

Unconfirmed inputs retain `application/octet-stream`. A missing declared tag, footer or candidate-frame extent sets `CM_TRUNCATED`. Unsupported ID3 versions, metadata without confirmed Layer III data, free-format rates and incompatible candidate parameters set `CM_AMBIGUOUS`. An unavailable three-frame run can carry both flags. Reserved values, invalid candidate headers or contradictory checked tag fields set `CM_MALFORMED`.

These flags describe the checks performed. A failed raw sync candidate may be unrelated binary data; it is not proof that the resource was intended to be an MP3. A complete buffer containing only a supported ID3 tag is ambiguous rather than necessarily truncated, since standalone metadata is allowed.

A known prefix can establish `audio/mpeg` once it contains three complete compatible frames; `CM_PARTIAL` remains set. This differs from `audio/ogg`, whose audio-only claim requires establishing the complete supported stream layout. A tag that extends beyond the 1 MiB inspection limit stays unconfirmed, even when the caller has supplied the entire original resource.

## References

The [WHATWG MP3 sniffing section](https://mimesniff.spec.whatwg.org/#signature-for-mp3-without-id3) provides a frame-boundary approach and bitrate/sample-rate tables. CupidMIME uses its own bounded three-complete-frame policy and does not claim browser algorithm conformance. The [ID3v2.2](https://id3.org/id3v2-00), [ID3v2.3](https://id3.org/id3v2.3.0), and [ID3v2.4 structure](https://id3.org/id3v2.4.0-structure) specifications define tag extents and flags. IANA registers the [audio/mpeg](https://www.iana.org/assignments/media-types/audio/mpeg) spelling, which covers MPEG audio more broadly than the Layer III subset detected here.
