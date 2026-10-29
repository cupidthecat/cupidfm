# File Handling

CupidScript provides comprehensive file handling capabilities including basic I/O operations and pattern matching.

> **Note:** Advanced file handling features (glob, file watching, temp files, archives) are platform-specific and may not be available on all systems.

## Table of Contents

- [Basic File Operations](#basic-file-operations)
- [Advanced Features (Platform-Specific)](#advanced-features-platform-specific)
- [Platform Support](#platform-support)

---

## Basic File Operations

These functions are available on all platforms:

### Reading Files

```cs
// Read text file
let content = read_file("config.txt");
if (content == nil) {
    print("Failed to read file");
}

// Read binary file
let data = read_file_bytes("image.png");
```

### Writing Files

```cs
// Write text
let success = write_file("output.txt", "Hello, world!");

// Write binary data
let bytes_data = bytes([0x48, 0x65, 0x6C, 0x6C, 0x6F]); // "Hello"
write_file_bytes("binary.dat", bytes_data);
```

### File Information

```cs
// Check existence
if (exists("config.yaml")) {
    print("Config found");
}

// Check type
if (is_file("data.txt")) {
    print("It's a file");
}

if (is_dir("folder")) {
    print("It's a directory");
}
```

### Directory Operations

```cs
// List directory contents
let entries = list_dir(".");
if (entries != nil) {
    for entry in entries {
        print(entry);
    }
}

// Create directory
mkdir("new_folder");

// Remove file or empty directory
rm("old_file.txt");

// Rename/move
rename("old_name.txt", "new_name.txt");
```

### Working Directory

```cs
// Get current directory
let dir = cwd();
print("Working in:", dir);

// Change directory
chdir("/path/to/project");
```

---

## Advanced Features (Platform-Specific)

The following features may not be available on all platforms. Check the [Platform Support](#platform-support) table below.

### Glob Patterns

```cs
// Find all text files
let txt_files = glob("*.txt");

// Find recursively
let cs_files = glob("**/*.cs", "src");
```

### File Watching

```cs
// Watch for changes (platform-dependent)
let handle = watch_file("config.yaml", fn(event, path) {
    print("Config changed:", event);
});

// Stop watching
unwatch(handle);
```

### Temporary Files

```cs
// Create temp file (platform-dependent)
let temp = temp_file("myapp_", ".txt");
write_file(temp, "temporary data");
// Automatically cleaned up on exit
```

### Archive Operations

```cs
// Compress with gzip (platform-dependent)
gzip_compress("large.txt", "large.txt.gz");

// Create tar archive (platform-dependent)
tar_create("backup.tar", ["file1.txt", "file2.txt"]);
```

---

## Platform Support

| Feature | Windows | Linux | macOS | Notes |
|---------|---------|-------|-------|-------|
| read_file | ✅ | ✅ | ✅ | Universal |
| write_file | ✅ | ✅ | ✅ | Universal |
| exists | ✅ | ✅ | ✅ | Universal |
| is_file/is_dir | ✅ | ✅ | ✅ | Universal |
| list_dir | ✅ | ✅ | ✅ | Universal |
| mkdir | ✅ | ✅ | ✅ | Universal |
| rm | ✅ | ✅ | ✅ | Universal |
| rename | ✅ | ✅ | ✅ | Universal |
| cwd/chdir | ✅ | ✅ | ✅ | Universal |
| glob | ⚠️ | ✅ | ✅ | May require platform-specific implementation |
| watch_file | ❌ | ✅ | ⚠️ | Linux: inotify; macOS: may use kqueue if implemented |
| temp_file | ⚠️ | ✅ | ✅ | Implementation may vary |
| gzip/tar | ⚠️ | ✅ | ✅ | Requires external libraries |

**Legend:**
- ✅ Full support
- ⚠️ Partial support or platform-specific implementation
- ❌ Not supported

**Notes:**

- Basic file operations work universally across all platforms
- Advanced features (glob, watch, archives) depend on platform-specific APIs and libraries
- On unsupported platforms, advanced functions may return `nil` or `false`
- Always check return values when using advanced features

---

## Common Patterns

### Safe File Reading

```cs
let content = read_file("config.yaml");
if (content == nil) {
    print("Using default configuration");
    content = "default: true";
}
let config = yaml_parse(content);
```

### Atomic File Write

```cs
fn safe_write(path, data) {
    let temp = path + ".tmp";
    if (!write_file(temp, data)) {
        return false;
    }
    return rename(temp, path);
}
```

### Directory Traversal

```cs
fn list_files_recursive(dir, pattern) {
    let result = [];
    let entries = list_dir(dir);
    if (entries == nil) return result;
    
    for entry in entries {
        let path = path_join(dir, entry);
        if (is_dir(path)) {
            let sub = list_files_recursive(path, pattern);
            extend(result, sub);
        } else if (str_endswith(entry, pattern)) {
            push(result, path);
        }
    }
    return result;
}

let cs_files = list_files_recursive("src", ".cs");
```

---

## See Also

- [Standard Library](Standard-Library.md) - Complete function reference
- [Data Formats](Data-Formats.md) - JSON, YAML, CSV, XML handling
- [Strings and Strbuf](Strings-and-Strbuf.md) - String processing
