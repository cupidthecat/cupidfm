# CupidArchive

A lightweight, safe archive reading library for previewing and extracting archive contents without external dependencies (beyond compression libraries).

## Overview

CupidArchive provides a clean 3-layer architecture for reading archive files:

1. **IO Layer** - Safe stream abstraction with byte limits to prevent zip bombs
2. **Filter Layer** - Decompression wrappers (gzip, bzip2, deflate, xz)
3. **Format Layer** - Archive format parsers (TAR, ZIP)

The library is designed with safety as a primary concern: every stream has a hard byte limit to prevent zip bomb attacks, and all operations are bounds-checked.

## Current Support

- **Formats:** TAR (ustar + pax extensions), ZIP (central directory + streaming mode, ZIP64 support)
- **Compression:** gzip (zlib), bzip2 (libbz2), deflate (zlib, for ZIP), xz/lzma (detection only, not implemented)
- **Entry Types:** Regular files, directories, symlinks, hardlinks (TAR only), files and directories (ZIP)
- **Operations:** Reading, previewing, and **extraction**

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

#### Bzip2 Filter (`arc_filter_bzip2`)

- Uses libbz2's `BZ2_bzDecompressInit()`
- Maintains a 64KB input buffer
- Streams decompression
- Does NOT support seeking (returns ESPIPE)
- Tracks decompressed bytes for `tell()` operation
- Does NOT close underlying stream (caller owns it)

#### XZ Filter (`arc_filter_xz`)

- Currently a placeholder (returns NULL with ENOSYS)
- Planned for future implementation with liblzma
- Format detection recognizes XZ magic bytes but cannot decompress

#### Deflate Filter (`arc_filter_deflate`)

- Uses zlib's `inflateInit2()` with `-MAX_WBITS` for raw deflate (no gzip wrapper)
- Used internally by ZIP format for deflate-compressed entries
- Maintains a 64KB input buffer
- Streams decompression
- Does NOT support seeking (returns ESPIPE)
- Tracks decompressed bytes for `tell()` operation
- Does NOT close underlying stream (caller owns it)

### Layer 3: Format Layer

#### TAR Format (`arc_tar.h`, `arc_tar.c`)

The TAR format implementation supports:

**Supported TAR Variants:**
- **ustar format** - Standard POSIX TAR with 100-byte filename limit
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

**Pax Extended Headers:**
- Parsed as key=value pairs
- Supports `path` attribute (long paths)
- Supports `size` attribute (large files)
- Automatically skipped and processed before actual entry header

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
- **Streaming mode** - Falls back to local header parsing when central directory is missing
- **ZIP64 support** - Files >4GB, archives >4GB, >65535 entries
- **Compression methods:** Store (0) and Deflate (8)
- **Directory detection** - Detected by filename ending with `/`
- **Encryption detection** - Flags encrypted entries (extraction not supported)

**ZIP64 Features:**
- Automatically detects ZIP64 archives via EOCD64 locator
- Parses ZIP64 Extended Information Extra Field (0x0001)
- Uses 64-bit sizes and offsets when standard fields are 0xFFFFFFFF

**ZIP Reader State:**
```c
typedef struct ZipReader {
    ArcStream *stream;              // Underlying stream
    ArcEntry current_entry;         // Current entry data
    bool entry_valid;               // Whether entry data is available
    int64_t entry_data_offset;      // Stream offset of entry data
    int64_t entry_data_remaining;   // Bytes remaining in entry
    bool eof;                        // End of archive reached
    bool streaming_mode;            // Using streaming (local header) mode
    // ... internal state for central directory or streaming
} ZipReader;
```

**Key Implementation Details:**
- Entry data is NOT read automatically - must call `arc_open_data()` or `arc_skip_data()`
- Entry remains valid until next `arc_next()` call or explicit `arc_skip_data()`
- Central directory mode: reads all entries from central directory first
- Streaming mode: reads entries sequentially from local file headers
- Supports both compressed (deflate) and uncompressed (store) entries

#### Archive Reader (`arc_reader.h`, `arc_reader.c`)

The unified reader API provides format-agnostic access to archives.

**Format Detection:**
1. Reads first 4 bytes to detect compression
2. Wraps stream with appropriate decompression filter if needed (gzip, bzip2)
3. Checks for ZIP format first (magic bytes `'P' 'K'`)
4. If not ZIP, reads first 512 bytes to check for TAR format
5. Checks for ustar magic or old TAR format indicators

**Compression Detection:**
- **Gzip:** Magic bytes `0x1f 0x8b`
- **Bzip2:** Magic bytes `'B' 'Z' 'h'`
- **XZ:** Magic bytes `0xFD 0x37 0x7A 0x58` (detected but not supported - returns error)

**Format Types:**
- `ARC_FORMAT_TAR` (0) - TAR format
- `ARC_FORMAT_ZIP` (1) - ZIP format

**Reader Lifecycle:**
1. `arc_open_path()` or `arc_open_stream()` - Opens archive
2. `arc_next()` - Iterate through entries
3. `arc_open_data()` or `arc_skip_data()` - Handle entry data
4. `arc_close()` - Clean up

**Entry Management:**
- `arc_next()` allocates `path` and `link_target` (caller must free)
- `arc_entry_free()` frees allocated fields
- Entry structure is copied to caller, but strings are allocated

### Extraction Layer (`arc_extract.c`)

The extraction layer provides full archive extraction capabilities.

#### Extraction Functions

**`arc_extract_to_path()`**
- Extracts all entries from an archive
- Creates subdirectories as needed (`mkdir_p()`)
- Preserves permissions and timestamps (optional)
- Returns error count (0 = success, >0 = some errors)

**`arc_extract_entry()`**
- Extracts a single entry
- Must be called immediately after `arc_next()` while entry data is available
- Creates parent directories automatically
- Handles files, directories, symlinks (TAR only), and hardlinks (TAR only)

#### Extraction Implementation Details

**Directory Creation:**
- Uses `mkdir_p()` to create parent directories recursively
- Default mode: 0755
- Handles existing directories gracefully (EEXIST)

**File Extraction:**
- Uses 64KB buffer for copying
- Creates files with `O_WRONLY | O_CREAT | O_TRUNC`
- Preserves permissions if requested
- Sets timestamps using `utime()` if requested

**Symlink Extraction (TAR only):**
- Removes existing file/symlink first (`unlink()`)
- Creates symlink with `symlink()`
- Does NOT preserve permissions (symlinks don't have separate permissions)
- ZIP format does not support symlinks

**Hardlink Extraction (TAR only):**
- Currently extracts as regular file (hardlink creation requires inode tracking)
- Future enhancement: track inode mappings and create links in second pass
- ZIP format does not support hardlinks

**Attribute Preservation:**
- Permissions: `chmod()` with `mode & 0777` (only user/group/other bits)
- Timestamps: `utime()` with mtime from entry
- Ownership: Not currently preserved (would require `chown()` and root privileges)

## API

### Basic Usage

```c
#include "cupidarchive/arc_reader.h"

ArcReader *reader = arc_open_path("archive.tar.gz");
if (!reader) {
    // Handle error
    return;
}

ArcEntry entry;
while (arc_next(reader, &entry) == 0) {
    printf("Entry: %s (size: %lu)\n", entry.path, entry.size);
    
    // Optionally read entry data
    ArcStream *data = arc_open_data(reader);
    if (data) {
        char buffer[4096];
        ssize_t n = arc_stream_read(data, buffer, sizeof(buffer));
        // ... process data ...
        arc_stream_close(data);
    }
    
    arc_entry_free(&entry);
}

arc_close(reader);
```

### Extraction Usage

```c
#include "cupidarchive/arc_reader.h"

// Extract entire archive
ArcReader *reader = arc_open_path("archive.tar.gz");
if (!reader) {
    // Handle error
    return;
}

int result = arc_extract_to_path(reader, "/tmp/extracted", true, true);
if (result < 0) {
    // Handle error
}

arc_close(reader);

// Or extract entries one by one
ArcReader *reader2 = arc_open_path("archive.tar");
ArcEntry entry;
while (arc_next(reader2, &entry) == 0) {
    int result = arc_extract_entry(reader2, &entry, "/tmp/extracted", true, true);
    if (result < 0) {
        // Handle error for this entry
    }
    arc_entry_free(&entry);
}
arc_close(reader2);
```

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
- **Standard C library** - POSIX.1-2008 features

## Safety Features

### Zip Bomb Prevention

- **Hard byte limits:** Every stream has a `byte_limit` that cannot be exceeded
- **Limit enforcement:** Limits checked before each read operation
- **Automatic limits:** File streams get 10x file size limit (for compressed archives)
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
- Graceful degradation (e.g., hardlinks fall back to file copy)
- Resource cleanup on errors

### Memory Safety

- All allocated memory is properly freed
- Entry strings are allocated and must be freed by caller
- Streams clean up their internal data on close
- No memory leaks in normal operation

## Extraction Features

- **Full archive extraction:** `arc_extract_to_path()` extracts all entries
- **Single entry extraction:** `arc_extract_entry()` extracts one entry at a time
- **Directory creation:** Automatically creates parent directories as needed (`mkdir_p()`)
- **Permission preservation:** Optional preservation of file permissions and ownership
- **Timestamp preservation:** Optional preservation of modification times
- **Symlink support:** Creates symlinks correctly (TAR format only)
- **Hardlink handling:** Attempts to create hardlinks, falls back to copying (TAR format only, future: proper inode tracking)

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

- [ ] xz/lzma compression support (liblzma integration)
- [ ] zstd compression support
- [ ] 7z format support
- [ ] RAR format support (read-only)
- [ ] Progress callbacks for extraction
- [ ] Extraction filters (exclude patterns)
- [ ] Proper hardlink handling (inode tracking)
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
