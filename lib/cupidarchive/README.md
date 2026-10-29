# CupidArchive

CupidArchive is a C library for listing and extracting archive contents. It uses zlib, libbz2, and liblzma for decompression.

## Overview

CupidArchive provides a clean 3-layer architecture for reading archive files:

1. **IO Layer** - Safe stream abstraction with byte limits to prevent zip bombs
2. **Filter Layer** - Decompression wrappers (gzip, bzip2, deflate, xz)
3. **Format Layer** - Archive format parsers (TAR, ZIP)

The library is designed with safety as a primary concern:
- **ArcLimits** are enforced across parsing, decompression, and extraction
- Streams support byte limits; decompression and extraction use the configured decoded limit
- Extraction is **openat()-anchored** and rejects path traversal (Zip-Slip)
- Decompression filters treat **truncated input as an error** (no silent partial output)

## Current Support

- **Formats:** TAR (ustar, PAX, GNU long names, and GNU/PAX sparse files), ZIP (central directory + streaming mode, ZIP64 support), 7z (Copy/LZMA1/LZMA2, multiple files and folders, solid substreams), compressed single files (.gz, .bz2, .xz as virtual archives)
- **Compression:** gzip (zlib), bzip2 (libbz2), deflate (zlib, for ZIP), xz/lzma (liblzma)
- **Entry Types:** Regular files, directories, symlinks, hardlinks (TAR only), files and directories (ZIP)
- **Operations:** Reading, previewing, and **extraction**

With XZ filter support you can treat `.tar.xz` as a native archive, and `.xz` / `.txz` single files appear as pseudo archives (one entry) through `arc_compressed.c`, just like `.gz` and `.bz2` files. All compressed single files are presented as virtual archives with a single entry.

### Reader and extraction behavior

Format detection works through gzip, bzip2, and xz wrappers. Ordinary compressed files are presented as one-entry archives. Compressed ZIP and 7z inputs are spooled to an anonymous temporary file for random access. Decompression limits apply across concatenated compression members.

Extraction traverses destination parents through directory descriptors and rejects symlink ancestors. Regular files are staged beside the destination, then renamed after data and metadata updates succeed. TAR hardlinks, including forward references, preserve inode links. Directory metadata is applied after children.

### Limits of support

The library reads and extracts archives; it has no creation or modification API. Extraction preserves permissions and timestamps but does not restore ownership. ZIP encryption and ZIP symlink/hardlink interpretation are unsupported.

7z supports multiple files, separate folders, and solid substreams when each folder uses one Copy, LZMA1, or LZMA2 coder. Encoded file tables and UTF-16 names are supported. Encryption, multiple volumes, coder/filter chains, anti-items, and Unix special-file entries are unsupported. Defined folder and file CRCs are verified when their data is read. Listing alone does not validate file contents.

## Limits (ArcLimits)

Most public APIs use safe defaults, but you can pass explicit limits via the `_ex` APIs:

```c
const ArcLimits *arc_default_limits(void);
ArcReader *arc_open_path_ex(const char *path, const ArcLimits *limits);
ArcReader *arc_open_stream_ex(ArcStream *stream, const ArcLimits *limits);
```

Limits include:
- `max_entries`: max entries parsed from ZIP central directory
- `max_name`: max entry name/path bytes
- `max_extra`: max ZIP extra/comment bytes
- `max_uncompressed_bytes`: cap on decompressed output (zip-bomb mitigation)
- `max_nested_depth`: max path depth (components) during extraction

## Architecture

### Layer 1: IO Layer (`arc_stream.h`, `arc_stream.c`)

The IO layer provides a unified stream interface using a virtual function table (vtable) pattern. This allows the same interface to be backed by different implementations.

#### Stream Structure

```c
struct ArcStream {
    const struct ArcStreamVtable *vtable;  // Function pointers
    int64_t byte_limit;                    // Hard limit on total bytes
    int64_t bytes_read;                    // Total bytes read so far
    void *user_data;                       // Implementation-specific data
};
```

#### Stream Implementations

1. **File Descriptor Stream** (`arc_stream_from_fd`)
   - Backed by a file descriptor
   - Uses `read()` for reading
   - Supports `lseek()` for seeking
   - Tracks position internally
   - Does NOT close the file descriptor (caller owns it)

2. **Memory Stream** (`arc_stream_from_memory`)
   - Backed by a memory buffer
   - Uses `memcpy()` for reading
   - Supports seeking within buffer bounds
   - Does NOT free the buffer (caller owns it)
   - Default byte limit is buffer size if not specified

3. **Substream** (`arc_stream_substream`)
   - Bounded view of another stream
   - Creates a window into a parent stream
   - Used for reading individual archive entry data
   - Automatically seeks parent stream to correct position
   - Does NOT close parent stream (caller owns it)

#### Byte Limit Enforcement

Every stream enforces a hard byte limit to prevent zip bombs:
- Limits are checked before each read operation
- When limit is reached, reads return 0 (EOF)
- Limits are enforced at the implementation level (fd_read, mem_read, substream_read)
- Decompression filters also enforce limits on decompressed data

#### Stream Operations

- `arc_stream_read()` - Read up to n bytes (enforces byte limit)
- `arc_stream_seek()` - Seek to offset (if supported)
- `arc_stream_tell()` - Get current position (if supported)
- `arc_stream_close()` - Close and free stream

### Layer 2: Filter Layer (`arc_filter.h`, `arc_filter.c`)

The filter layer wraps underlying streams to provide decompression. Filters are themselves streams, allowing them to be chained.

#### Gzip Filter (`arc_filter_gzip`)

- Uses zlib's `inflateInit2()` with `16 + MAX_WBITS` for gzip format
- Maintains a 64KB input buffer
- Streams decompression (doesn't require seeking)
- Does NOT support seeking (returns ESPIPE)
- Tracks decompressed bytes for `tell()` operation
- Does NOT close underlying stream (caller owns it)
- **Truncated input fails:** if input ends before `Z_STREAM_END`, returns `-1` and sets `errno = EINVAL`

#### Bzip2 Filter (`arc_filter_bzip2`)

- Uses libbz2's `BZ2_bzDecompressInit()`
- Maintains a 64KB input buffer
- Streams decompression
- Does NOT support seeking (returns ESPIPE)
- Tracks decompressed bytes for `tell()` operation
- Does NOT close underlying stream (caller owns it)

#### XZ Filter (`arc_filter_xz`)

- Uses liblzma and `lzma_stream_decoder()` to stream-decompress .xz archives
- Maintains a 64KB input buffer
- Streams decompression (no seeking)
- Does NOT close the underlying stream (`openat()` reader owns it)
- **Truncated input fails:** `LZMA_BUF_ERROR` with no progress becomes `errno = EINVAL`

#### Deflate Filter (`arc_filter_deflate`)

- Uses zlib's `inflateInit2()` with `-MAX_WBITS` for raw deflate (no gzip wrapper)
- Used internally by ZIP format for deflate-compressed entries
- Input must be bounded to one compressed payload; bytes after its end marker fail validation
- Maintains a 64KB input buffer
- Streams decompression
- Does NOT support seeking (returns ESPIPE)
- Tracks decompressed bytes for `tell()` operation
- Does NOT close underlying stream (caller owns it)
- **Truncated input fails:** if input ends before `Z_STREAM_END`, returns `-1` and sets `errno = EINVAL`

### Layer 3: Format Layer

#### TAR Format (`arc_tar.h`, `arc_tar.c`)

The TAR format implementation supports:

**Supported TAR Variants:**
- **ustar format** - Standard POSIX TAR with name and path-prefix fields
- **pax extended headers** - For long paths (>100 chars) and large files (>8GB)
- **Old TAR format** - Pre-ustar format (detected by absence of magic)

**TAR Header Structure:**
- Fixed 512-byte blocks
- Octal-encoded numeric fields (mode, size, mtime, etc.)
- Checksum verification (sum of all bytes with checksum field as spaces)
- Support for prefix field (ustar) for paths up to 255 chars

**Entry Types Supported:**
- `'0'` or `'\0'` - Regular file
- `'5'` - Directory
- `'2'` - Symlink
- `'1'` - Hardlink
- `'x'` - pax extended header (per-file)
- `'g'` - pax global header
- `'L'` - GNU long filename (applied to next entry)
- `'K'` - GNU long linkname (applied to next entry)

**PAX Extended Headers:**

PAX (Portable Archive Interchange) extended headers provide POSIX-compliant support for:

**PAX per-file records (typeflag = 'x'):**
- `path` - Overrides filename, within the configured name limit
- `linkpath` - Overrides the link target, within the configured name limit
- `size` - Overrides file size (supports large files)
- `uid`, `gid`, `mtime`, `mode` - Override metadata (common in the wild)
- Applied to the next real entry (skipped in entry iteration)

**PAX global records (typeflag = 'g'):**
- Sets defaults for all following entries (until overridden)
- Same fields as per-file records

**PAX record parsing:**
- Records are decimal-length lines: `LEN key=value\n`
- Reads exactly the payload length from TAR header
- Safely skips padding to 512-byte boundaries

**GNU Long Name Extensions:**

GNU tar extensions for long names:

**Long filename (typeflag = 'L'):**
- Contains filename too long for ustar header
- Applied to the next real entry

**Long linkname (typeflag = 'K'):**
- Contains symlink target too long for ustar header
- Applied to the next real entry

**TAR Reader State:**
```c
typedef struct TarReader {
    ArcStream *stream;              // Underlying stream
    ArcEntry current_entry;        // Current entry data
    bool entry_valid;              // Whether entry data is available
    int64_t entry_data_offset;     // Stream offset of entry data
    int64_t entry_data_remaining;  // Bytes remaining in entry
    bool eof;                       // End of archive reached
} TarReader;
```

**Key Implementation Details:**
- Entry data is NOT read automatically - must call `arc_open_data()` or `arc_skip_data()`
- Entry remains valid until next `arc_next()` call or explicit `arc_skip_data()`
- Data is padded to 512-byte block boundaries
- Zero blocks indicate end of archive

#### ZIP Format (`arc_zip.h`, `arc_zip.c`)

The ZIP format implementation supports:

**ZIP Features:**
- **Central Directory parsing** - Fast listing using central directory (standard ZIP files)
- **Local-header recovery** - Reads complete local entries when the central directory is absent; input must be seekable
- **ZIP64 support** - Files >4GB, archives >4GB, >65535 entries via ZIP64 EOCD + locator + extra fields
- **Data descriptor support** - Handles ZIPs created with streaming (bit 3 set in general purpose flags)
- **Compression methods:** Store (0) and Deflate (8)
- **Directory detection** - Detected by filename ending with `/`
- **Encryption detection** - Flags encrypted entries (extraction not supported)

**ZIP64 Features:**
- Automatically detects ZIP64 archives when EOCD fields contain 0xFFFFFFFF
- Reads ZIP64 End of Central Directory Locator (signature 0x07064b50)
- Parses ZIP64 End of Central Directory Record (signature 0x06064b50)
- Handles ZIP64 Extended Information Extra Field (0x0001) in both central and local headers
- Supports 64-bit file sizes, compressed sizes, and local header offsets
- Replaces only fields marked with ZIP64 sentinels; ordinary sizes and offsets remain intact
- Works in both central-directory and local-header recovery modes

**ZIP Reader State:**
Internals are not part of the public API (opaque `ArcReader`), but conceptually the ZIP reader tracks:
- Current entry metadata + offsets
- Whether it’s using central-directory mode vs streaming mode
- Underlying stream (and optional owned underlying stream when wrapped by a filter)

**Data Descriptor Support:**
- Handles ZIPs created with streaming where sizes aren't known at header time
- Detects data descriptors via general purpose bit flag 3 (0x0008)
- Stored entries in recovery mode locate descriptors by matching CRC, sizes, and the following record boundary
- Deflated entries in recovery mode decode through the end of the raw deflate stream and count the compressed bytes consumed
- Supports descriptors with or without a signature and with 32- or 64-bit sizes
- Rejects missing or inconsistent descriptors instead of guessing zero-length contents
- Enforces decoded byte limits while recovering descriptors

**Key Implementation Details:**
- Use `arc_open_data()` to read an entry or `arc_skip_data()` to discard it
- Entry data remains available until the next `arc_next()` or explicit `arc_skip_data()`
- Central-directory mode reads entry metadata first and verifies local names, flags, methods, and sizes before opening data
- Local-header recovery reads entries sequentially; finding a descriptor may require bounded decoding before the entry is returned
- A present but malformed EOCD/ZIP64 record is an error. Truncated local headers also fail; they are not treated as an empty archive
- EOCD comments may contain signature-like bytes; the reader checks the declared comment length and directory bounds before choosing a footer
- Supports both compressed (deflate) and uncompressed (store) entries

#### Archive Reader (`arc_reader.h`, `arc_reader.c`)

The unified reader API provides format-agnostic access to archives.

**Format Detection:**
1. Detects whole-file compression (gzip/bzip2) and **sniffs the decompressed header**
2. Checks for ZIP first (PK signatures)
3. Otherwise checks TAR via **ustar magic or valid TAR checksum** (and rejects all-zero blocks)
4. Returns `{format, compression_type}` so callers can recreate a fresh filter for the real reader

**Compression Detection:**
- **Gzip:** Magic bytes `0x1f 0x8b`
- **Bzip2:** Magic bytes `'B' 'Z' 'h'`
- **XZ:** Magic bytes `0xFD 0x37 0x7A 0x58` (compressed streams handled via liblzma filter)

**Format Types:**
- `ARC_FORMAT_TAR` (0) - TAR format
- `ARC_FORMAT_ZIP` (1) - ZIP format
- `ARC_FORMAT_7Z` (3) - 7z format (limited)

**Reader Lifecycle:**
1. `arc_open_path()` / `arc_open_stream()` (or `*_ex` variants) - Opens archive
2. `arc_next()` - Iterate through entries
3. `arc_open_data()` or `arc_skip_data()` - Handle entry data
4. `arc_close()` - Clean up

**Ownership note (filtered streams):**
Filters do not close their underlying stream for composability. Readers track this via:
- `base.stream`: what the format reads (may be a filter)
- `base.owned_stream`: underlying stream to also close (e.g. the file stream under a gzip filter)

**Entry Management:**
- `arc_next()` allocates `path` and `link_target` (caller must free)
- `arc_entry_free()` frees allocated fields
- Entry structure is copied to caller, but strings are allocated

### Extraction Layer (`arc_extract.c`)

The extraction layer provides full archive extraction capabilities.

#### Extraction Functions

**`arc_extract_to_path()`**
- Extracts all entries from an archive
- Creates subdirectories as needed using `mkdirat()` via an **openat()-anchored traversal**
- Preserves permissions and timestamps (optional)
- Returns error count (0 = success, >0 = some errors)

**`arc_extract_entry()`**
- Extracts a single entry
- Must be called immediately after `arc_next()` while entry data is available
- Creates parent directories automatically
- Handles files, directories, symlinks (TAR only), and hardlinks (TAR only)

#### Extraction Implementation Details

**Directory creation and attributes:**
- Traverses each parent through `openat()` with `O_NOFOLLOW`, creating missing directories with `mkdirat()` and mode 0755.
- Applies directory permissions and timestamps after extracting members, with children before their parents.
- Uses the last archive entry's metadata when a directory path is repeated.

**File extraction:**
- Copies through a 64 KiB buffer into a new temporary file beside the destination.
- Keeps zero-filled ranges sparse and sets the final logical size with `ftruncate()`.
- Replaces the destination only after member reads and requested metadata updates succeed.

**Symlink extraction (TAR):**
- Creates a temporary symlink with `symlinkat()` and renames it into place.
- Symlink permissions are not restored. The ZIP reader does not interpret Unix symlink entries.

**Hardlink extraction (TAR):**
- Resolves targets inside the extraction root and creates links with `linkat()`.
- Full-archive extraction defers forward hardlinks until their regular targets exist.
- For single-entry extraction, the target must already exist. The ZIP reader does not interpret hardlink metadata.

**Attribute Preservation:**
- Permissions: `fchmod()` with `mode & 0777` (only user/group/other bits)
- Timestamps: `futimens()` with mtime from entry
- Ownership: Not currently preserved (would require `chown()` and root privileges)

**Security:**
- Rejects absolute paths and any `..` path components (Zip-Slip prevention)
- All extraction operations are anchored to a destination directory fd (`openat()` family)
- Uses `O_NOFOLLOW` during traversal and file creation to prevent symlink races

## API

### List entries

```c
#include "cupidarchive.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    ArcReader *reader = arc_open_path(argv[1]);
    if (!reader) { perror("archive"); return 1; }
    ArcEntry entry;
    int result;
    while ((result = arc_next(reader, &entry)) == 0) {
        printf("%s: %llu bytes\n", entry.path, (unsigned long long)entry.size);
        arc_entry_free(&entry);
    }
    arc_close(reader);
    return result < 0 ? 1 : 0;
}
```

To read a regular file's contents, call `arc_open_data(reader)` after `arc_next`. Read until EOF, check for negative reads, and close the data stream before advancing or closing the reader. Data streams borrow their reader's resources. A standalone compressed entry has unknown length represented by zero, so its declared size must not be used as an EOF test.

### Extract an archive

```c
#include "cupidarchive.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    ArcReader *reader = arc_open_path(argv[1]);
    if (!reader) { perror("archive"); return 1; }
    int result = arc_extract_to_path(reader, argv[2], true, true);
    arc_close(reader);
    return result ? 1 : 0;
}
```

`arc_extract_entry` extracts an individual returned entry. Use whole-archive extraction when forward hardlinks or deferred directory metadata are needed. Failed member writes preserve existing destinations; successful earlier members remain when a later member fails.

### Entry Structure

```c
typedef struct ArcEntry {
    char     *path;        // Normalized path (allocated, caller must free)
    uint64_t  size;        // File size in bytes
    uint32_t  mode;        // File mode/permissions
    uint64_t  mtime;       // Modification time (Unix timestamp)
    uint8_t   type;        // Entry type (ARC_ENTRY_*)
    char     *link_target; // Symlink target (if applicable, allocated, caller must free)
    uint32_t  uid;         // User ID
    uint32_t  gid;         // Group ID
} ArcEntry;
```

### Entry Types

- `ARC_ENTRY_FILE` (0) - Regular file
- `ARC_ENTRY_DIR` (1) - Directory
- `ARC_ENTRY_SYMLINK` (2) - Symbolic link
- `ARC_ENTRY_HARDLINK` (3) - Hard link
- `ARC_ENTRY_OTHER` (4) - Other (device files, etc.)

## Building

```bash
cd cupidarchive
make
```

This builds `libcupidarchive.a` (static library) in the root directory.

### Build Output

- **Library:** `libcupidarchive.a` (static archive)
- **Object files:** `obj/*.o` (compiled source files)
- **Source files:** `src/*.c` and `src/*.h`

## Integration

Link against the library:

```bash
gcc -o myapp myapp.c -Lcupidarchive -lcupidarchive -lz -lbz2
```

### Include Paths

The library expects:
- `-Icupidarchive` - For `#include "cupidarchive.h"`
- `-Icupidarchive/src` - For internal headers (automatically included)

### Dependencies

- **zlib** - For gzip decompression (`-lz`)
- **libbz2** - For bzip2 decompression (`-lbz2`)
- **liblzma** - For xz and LZMA decompression (`-llzma`)
- **Standard C library** - POSIX.1-2008 features

## Safety Features

### Zip Bomb Prevention

- **Hard byte limits:** Every stream has a `byte_limit` that cannot be exceeded
- **Limit enforcement:** Limits checked before each read operation
- **Decoded limits:** Readers enforce entry counts, name lengths, and declared uncompressed sizes; filters limit decoded bytes.
- **Substream limits:** Automatically set to entry size
- **Filter limits:** Decompression filters enforce limits on decompressed data

### Bounds Checking

- All array accesses are bounds-checked
- TAR header parsing validates field sizes
- Path normalization prevents buffer overflows
- Substream operations validate offset and length

### Error Handling

- Comprehensive error codes via `errno`
- NULL pointer checks throughout
- Forward hardlinks are resolved after their regular targets are extracted.
- Resource cleanup on errors

### Memory Safety

- Close streams and readers to release their state.
- Entry strings are allocated and must be freed by caller
- Streams clean up their internal data on close
- Sanitizer regressions check covered decoding and extraction paths for leaks and invalid accesses.

## Extraction Features

- **Full archive extraction:** `arc_extract_to_path()` extracts all entries
- **Single entry extraction:** `arc_extract_entry()` extracts one entry at a time
- **Directory creation:** Creates parent directories through `openat()` and `mkdirat()`
- **Permission preservation:** Optional preservation of file permissions; ownership is not restored
- **Timestamp preservation:** Optional preservation of modification times
- **Symlink support:** Creates symlinks correctly (TAR format only)
- **Hardlink handling:** Creates TAR hardlinks and resolves forward references during full-archive extraction

## Testing

The library includes a comprehensive test suite. To run tests:

```bash
cd cupidarchive
make test
```

This will:
- Build the library (if not already built)
- Compile all test executables
- Run all tests and report results

### Advanced Testing

Run tests with AddressSanitizer for memory error detection:

```bash
cd cupidarchive/tests
make test-asan
```

Run tests with Valgrind for detailed memory analysis:

```bash
cd cupidarchive/tests
make test-valgrind
```

See `tests/README.md` for more information about the test suite.

## Implementation Details

### Stream Virtual Function Table Pattern

The stream abstraction uses a vtable pattern for polymorphism:

```c
struct ArcStreamVtable {
    ssize_t (*read)(ArcStream *stream, void *buf, size_t n);
    int     (*seek)(ArcStream *stream, int64_t off, int whence);
    int64_t (*tell)(ArcStream *stream);
    void    (*close)(ArcStream *stream);
};
```

Each stream type (fd, memory, substream, filter) implements its own vtable.

### TAR Block Alignment

TAR format requires 512-byte block alignment:
- Entry headers are 512 bytes
- Entry data is padded to 512-byte boundaries
- End of archive is indicated by two consecutive zero blocks

### Path Normalization

Paths are normalized to:
- Remove leading `./`
- Remove duplicate slashes `//`
- Preserve absolute paths
- Handle ustar prefix + name combination

### Format Detection Logic

1. Read first 4 bytes to detect compression
2. Check compression magic bytes (gzip, bzip2, xz)
3. If compressed (gzip/bzip2), wrap with filter and read again
4. Check for ZIP format first (magic bytes `'P' 'K'` with ZIP signatures)
5. If not ZIP, read first 512 bytes to check for TAR format
6. Check for ustar magic or old TAR indicators
7. Reset stream position and return format code

### Entry Data Management

- Entry data is NOT automatically read
- `arc_next()` only reads the header
- `arc_open_data()` creates a substream for entry data
- `arc_skip_data()` seeks past entry data
- Entry remains valid until next `arc_next()` or `arc_skip_data()`

## Future Plans

- [ ] zstd compression support
- [ ] More 7z coders/filter chains and encrypted headers
- [ ] RAR format support (read-only)
- [ ] Progress callbacks for extraction
- [ ] Extraction filters (exclude patterns)
- [ ] Ownership preservation (chown support)
- [ ] Archive creation (write support)
- [ ] ZIP encryption support (password-protected archives)

## License

This library is licensed under the **GNU General Public License v3.0 (GPL-3.0)**.

This means:
- You are free to use, modify, and distribute this library
- If you modify the library, you must release your changes under GPL-3.0
- If you use this library in your project, your project must also be licensed under GPL-3.0 (or a compatible license)

See the LICENSE file in the parent directory for the full license text.

## Building and extraction checks

Run `make -j` to build all readers, including 7z and xz. Link applications with `libcupidarchive.a -lz -lbz2 -llzma`; the development headers for all three compression libraries are required. Run `make test` for stream, reader, and extraction regressions.

Extraction traverses destination directories through file descriptors and rejects symlink ancestors. Regular files are written beside their destination and renamed after successful reads and metadata updates. A truncated member, a ZIP checksum mismatch, or a reader error causes extraction to fail; existing files survive failed member writes. Successful members of an archive are retained if a later member fails. Hardlinks can refer to later regular members, and directory metadata is applied after its children.

Close each data stream returned by `arc_open_data` before closing its reader. On successful `arc_open_stream`, the reader owns the supplied stream; on failure the caller still owns it. Seekable raw streams reset their read budget after a successful seek; decompression filters remain forward only and retain their decoded byte budget.

The 7z reader maps file records to folder substreams, validates sizes and counts, and enforces the decoded limit across all folders. A solid folder is decoded once into an anonymous temporary file and reused for its consecutive files. Opening data for a solid entry validates the folder before returning a bounded file stream; defined per-file CRCs are checked as that stream is read. Empty files, directories, Unicode names, and encoded file tables are supported. LZMA decoder memory is capped at 128 MiB. The reader follows the [7z format description](https://github.com/ip7z/7zip/blob/main/DOC/7zFormat.txt).

Compressed TAR readers finish the compression stream before reporting archive completion, so missing trailers and checksum failures are reported even after the TAR end marker. ZIP and 7z inside gzip, bzip2, or xz are decoded to an anonymous temporary file for random access, within the configured decoded byte limit. Ordinary compressed files and TAR archives remain streamed.

Gzip, bzip2, and xz filters decode concatenated members as one byte stream and enforce the decoded limit across all members. Damaged later members and trailing non-member data fail instead of returning a successful prefix. The gzip/bzip2 readers reset their decoder at each member boundary, as required by the [zlib manual](https://www.zlib.net/manual.html) and [bzip2 manual](https://sourceware.org/bzip2/manual/manual.html).

Standalone compressed entries report `size = 0` for unknown length. A gzip trailer contains only one member's size modulo 2^32, and an xz footer describes its own stream; neither is a reliable total for concatenated input. Read the entry data until EOF to obtain the full contents. Advancing past or skipping a compressed entry drains the filter and reports decoding failures.

Record reads retry short underlying reads. File-path readers accept regular files and reject FIFOs without waiting for a writer. XZ streams without a content checksum are accepted, while checksums present in the stream are verified. XZ decoder memory is capped at 128 MiB.

GNU sparse files are reconstructed from their extent maps, including old GNU extension headers and PAX sparse versions 0.0, 0.1, and 1.0. Extraction retains holes, and following members use the stored-data boundaries rather than the logical sparse size. The implementation follows the [GNU sparse-format descriptions](https://www.gnu.org/software/tar/manual/html_section/Sparse-Formats.html).

TAR metadata parsing validates record lengths, numeric overflow, sparse ranges, and stored-byte totals before returning a member. PAX size overrides control both data reads and alignment. GNU long names retain literal newlines. `max_extra` bounds per-member extension metadata; `max_uncompressed_bytes` also bounds logical sparse sizes. Sparse fixture tests use GNU tar as an independent producer and Python's TAR reader to check the expected contents.

The ZIP metadata and descriptor layouts follow the [PKWARE ZIP specification](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT). Regression coverage includes every conditional ZIP64 size/offset combination, ZIP64 end records, sparse input with a local-header offset beyond 4 GiB, local-header recovery, unsigned descriptors, descriptor-like payload bytes, and malformed metadata.

Call `arc_format(reader)` to query the decoded container type. A ZIP inside gzip is reported as `ARC_FORMAT_ZIP`; a standalone compressed file is `ARC_FORMAT_COMPRESSED`. The query uses the detected data format rather than filename suffixes, and returns `ARC_FORMAT_UNKNOWN` for a null reader.
