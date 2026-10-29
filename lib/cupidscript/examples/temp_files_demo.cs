// Temporary Files Demo
// Demonstrates creating and using temporary files and directories

print("=== Temporary Files Demo ===\n");

// Example 1: Basic temp file
print("1. Creating basic temporary file:");
let temp1 = temp_file();
print("   Path:", temp1);
print("   Exists:", exists(temp1));
print("   Is file:", is_file(temp1));

// Write to temp file
write_file(temp1, "This is temporary data that will be cleaned up on exit.");
let content = read_file(temp1);
print("   Content:", content);

// Example 2: Temp file with custom prefix and suffix
print("\n2. Creating temp file with custom naming:");
let temp_log = temp_file("myapp_", ".log");
print("   Log file:", temp_log);
write_file(temp_log, "Log entry 1\nLog entry 2\nLog entry 3");
print("   Log contains:", len(split_lines(read_file(temp_log))), "lines");

// Example 3: Temp directory
print("\n3. Creating temporary directory:");
let temp_dir = temp_dir("workspace_");
print("   Directory:", temp_dir);
print("   Is directory:", is_dir(temp_dir));

// Use temp directory for work
write_file(temp_dir + "/data.json", '{"key": "value"}');
write_file(temp_dir + "/result.txt", "Processing results");
write_file(temp_dir + "/cache.bin", "Binary cache data");

let files = list_dir(temp_dir);
print("   Files created:", len(files));
for file in files {
    print("     -", file);
}

// Example 4: Multiple temp files
print("\n4. Creating multiple temp files:");
let temp_files = [];
for i in [1, 2, 3, 4, 5] {
    let tf = temp_file("batch_", ".tmp");
    push(temp_files, tf);
    write_file(tf, "Batch item " + to_str(i));
}
print("   Created", len(temp_files), "temporary files");
print("   All unique:", len(temp_files) == len(set(temp_files)));

// Example 5: Real-world use case - Download cache
print("\n5. Real-world example - Download cache:");
let cache_dir = temp_dir("download_cache_");
print("   Cache directory:", cache_dir);

// Simulate downloading and caching files
let urls = ["file1.dat", "file2.dat", "file3.dat"];
for url in urls {
    let cache_file = cache_dir + "/" + url;
    write_file(cache_file, "Downloaded content from: " + url);
    print("   Cached:", url);
}

let cached_files = list_dir(cache_dir);
print("   Total cached files:", len(cached_files));

// Example 6: Processing pipeline with temp files
print("\n6. Processing pipeline example:");
let input_temp = temp_file("input_", ".txt");
let output_temp = temp_file("output_", ".txt");

write_file(input_temp, "raw data needing processing");
let raw = read_file(input_temp);
let processed = str_upper(raw);
write_file(output_temp, processed);

print("   Input:", input_temp);
print("   Output:", output_temp);
print("   Result:", read_file(output_temp));

// Example 7: Temp file for atomic writes
print("\n7. Atomic file write pattern:");
let target_file = "important_config.json";
let temp_write = temp_file("config_", ".tmp");

// Write to temp file first
write_file(temp_write, '{"version": "2.0", "setting": "value"}');

// Verify it's valid (in real case, you'd parse/validate)
let temp_content = read_file(temp_write);
if len(temp_content) > 0 {
    // Move to final location (rename is atomic)
    rename(temp_write, target_file);
    print("   Config written atomically");
} else {
    print("   Validation failed, temp file discarded");
}

// Example 8: Temp directory for build artifacts
print("\n8. Build artifacts example:");
let build_dir = temp_dir("build_");
mkdir(build_dir + "/obj");
mkdir(build_dir + "/bin");

write_file(build_dir + "/obj/main.o", "object file");
write_file(build_dir + "/bin/program", "executable");

print("   Build directory:", build_dir);
print("   Structure:");
let subdirs = list_dir(build_dir);
for subdir in subdirs {
    print("     -", subdir);
    let subpath = build_dir + "/" + subdir;
    if is_dir(subpath) {
        let contents = list_dir(subpath);
        for item in contents {
            print("       -", item);
        }
    }
}

print("\n=== Temporary Files Demo Complete ===");
print("\nNote: Temporary files are automatically cleaned up when the program exits.");
print("They are created in the system temp directory (/tmp on Linux).");

// Manual cleanup for demo (automatic cleanup happens on exit)
rm(target_file);
