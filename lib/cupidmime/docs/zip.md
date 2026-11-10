# ZIP and EPUB detection

ZIP detection starts with an offset-zero local-header or empty-archive end-record signature. A complete fixed local header with a nonempty filename establishes the generic `application/zip` family. If its name, extra fields or declared compressed data exceed the window, the result also carries `CM_TRUNCATED`.

Known prefixes, including inputs larger than the 1 MiB scan limit, stay generic. The detector does not treat an end record found in a prefix as the end of the original resource.

## Directory checks

For a complete buffer, the detector searches the final 65,557 bytes for an end-of-central-directory record whose comment ends exactly at the buffer boundary. This accommodates the 65,535-byte ZIP comment limit. If more than one record can satisfy that condition, the result remains ambiguous. A missing record or incomplete comment receives `CM_TRUNCATED`; this does not distinguish a truncated archive from an archive followed by unsupported trailing data.

For ordinary ZIP, the detector checks:

- Per-disk and total entry counts agree.
- The directory fits before the end record, and its declared size matches the entries walked.
- Each central entry's filename, extra field and comment fit within the directory.
- Each referenced local header fits before the directory. Local filenames, general-purpose flags, compression methods, CRC fields and size fields agree with central metadata under the supported descriptor rules.
- Compressed data stays before the directory; stored entries have equal compressed and uncompressed sizes.
- Extra fields have complete length-delimited records.
- Ordinary data descriptors, with or without their optional signature, agree with the directory. A CRC equal to the descriptor signature is handled as data when the unsigned representation matches.

The supported walk requires central entries in physical local-record order, with local records contiguous from offset zero through the directory. Reordered entries, gaps and overlapping references stop the walk and remain generic. This restriction keeps inspection linear without allocation or a sorting table. Reordered directories can be valid ZIP files, so the layout restriction does not set `CM_MALFORMED`.

ZIP64 markers and extra fields, split archives, encrypted entries and compression methods other than stored or DEFLATE also stop the walk with a generic ambiguous result. Directory gaps may contain extensions the detector does not support. Unsupported metadata is not, by itself, classified as malformed. A detected contradiction or field crossing a checked boundary sets `CM_MALFORMED` while retaining the generic ZIP family.

## EPUB recognition

`application/epub+zip` requires all supported directory checks to finish, an ordered contiguous layout, and exactly one literal `mimetype` entry. That entry must be the first physical member, use stored compression, have no extra fields in either header, and contain exactly the 20 ASCII bytes `application/epub+zip`. Its CRC must agree with those bytes. Only the UTF-8 filename flag is allowed on the marker entry. Any data descriptor anywhere in the archive prevents subtype recognition in this release.

The rule is deliberately stricter than recognizing marker bytes at fixed offsets. A valid-looking marker with a missing directory, conflicting directory name, wrong CRC, duplicate marker, encrypted member or known partial view stays generic. A compressed marker also stays generic. Names such as `word/document.xml` do not establish Office content.

This identifies an EPUB container marker, not a conforming publication. The detector does not parse `META-INF/container.xml`, package documents, resources, signatures or encryption manifests. It does not decompress DEFLATE content, verify other entry CRCs, normalize Unicode path aliases, or detect duplicate names other than literal `mimetype`. Extra-field contents other than ZIP64 markers are not interpreted. Those checks belong to a fuller archive or publication validator.

The field layout follows [PKWARE APPNOTE](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT). The marker requirements come from the [EPUB Open Container Format](https://www.w3.org/TR/epub-33/#sec-zip-container) and [IANA's EPUB media-type registration](https://www.iana.org/assignments/media-types/application/epub%2Bzip).
