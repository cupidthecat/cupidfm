// File Watching Demo
// Demonstrates monitoring files and directories for changes

print("=== File Watching Demo ===\n");

// Create a test file to watch
let watch_file_path = "watched_file.txt";
write_file(watch_file_path, "Initial content");

print("1. Setting up file watch on:", watch_file_path);

// Watch for file changes
let changes = [];
let file_handle = watch_file(watch_file_path, fn(event, path) {
    print("   📝 File event:", event, "on", path);
    push(changes, {event: event, path: path, time: unix_ms()});
});

print("   Watch handle:", file_handle);
print("   Modifying file...");

// Make some changes
sleep(100);
write_file(watch_file_path, "Modified content 1");
process_file_watches();
sleep(100);

write_file(watch_file_path, "Modified content 2");
process_file_watches();
sleep(100);

print("   File changes detected:", len(changes));

// Example 2: Watch a directory
print("\n2. Setting up directory watch:");
let watch_dir_path = "watched_directory";
mkdir(watch_dir_path);

let dir_changes = [];
let dir_handle = watch_dir(watch_dir_path, fn(event, path) {
    print("   📁 Directory event:", event, "on", path);
    push(dir_changes, {event: event, path: path});
});

print("   Directory watch handle:", dir_handle);
print("   Creating files in directory...");

sleep(100);
write_file(watch_dir_path + "/file1.txt", "File 1");
process_file_watches();
sleep(100);

write_file(watch_dir_path + "/file2.txt", "File 2");
process_file_watches();
sleep(100);

write_file(watch_dir_path + "/file1.txt", "File 1 modified");
process_file_watches();
sleep(100);

print("   Directory changes detected:", len(dir_changes));

// Example 3: Recursive directory watching
print("\n3. Recursive directory watching:");
mkdir(watch_dir_path + "/subdir");
let rec_handle = watch_dir(watch_dir_path, fn(event, path) {
    print("   🌲 Recursive event:", event, "on", path);
}, true);

sleep(100);
write_file(watch_dir_path + "/subdir/nested.txt", "Nested file");
process_file_watches();
sleep(100);

// Example 4: Real-world use case - auto-reload config
print("\n4. Real-world example - Config file monitoring:");
let config_file = "app_config.yaml";
write_file(config_file, "version: 1\nmode: production");

let config_watch = watch_file(config_file, fn(event, path) {
    print("   ⚙️  Config changed! Reloading...");
    let new_config = read_file(path);
    print("   New config:", new_config);
});

sleep(100);
write_file(config_file, "version: 2\nmode: development\ndebug: true");
process_file_watches();
sleep(100);

print("\n5. Cleaning up watches:");
print("   Unwatching file:", unwatch(file_handle));
print("   Unwatching directory:", unwatch(dir_handle));
print("   Unwatching recursive:", unwatch(rec_handle));
print("   Unwatching config:", unwatch(config_watch));

// Cleanup
rm(watch_file_path);
rm(watch_dir_path + "/file1.txt");
rm(watch_dir_path + "/file2.txt");
rm(watch_dir_path + "/subdir/nested.txt");
rm(watch_dir_path + "/subdir");
rm(watch_dir_path);
rm(config_file);

print("\n=== File Watching Demo Complete ===");
print("\nNote: File watching uses Linux inotify for efficient monitoring.");
print("Events are processed when you call process_file_watches().");
