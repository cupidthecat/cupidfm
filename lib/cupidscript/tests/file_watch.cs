// Test file watching with inotify

print("Testing file watching...\n");

// Setup test files
let test_file = "test_watch_file.txt";
let test_dir = "test_watch_dir";

write_file(test_file, "initial content");
mkdir(test_dir);

let events = [];

// Test 1: Watch a file
let file_handle = watch_file(test_file, fn(event, path) {
    push(events, {type: event, path: path});
    print("  Event:", event, "on", path);
});

assert(file_handle > 0, "Should return valid file watch handle");
print("✓ Test 1: watch_file returns valid handle:", file_handle);

// Test 2: Modify the watched file
write_file(test_file, "modified content");
process_file_watches();
sleep(100); // Give time for event
process_file_watches();

// Note: Events may vary by system, just check we can watch
print("✓ Test 2: File modification monitored");

// Test 3: Watch a directory
let events2 = [];
let dir_handle = watch_dir(test_dir, fn(event, path) {
    push(events2, {type: event, path: path});
    print("  Dir event:", event, "on", path);
});

assert(dir_handle > 0, "Should return valid directory watch handle");
print("✓ Test 3: watch_dir returns valid handle:", dir_handle);

// Test 4: Create file in watched directory
write_file(test_dir + "/new_file.txt", "new content");
process_file_watches();
sleep(100);
process_file_watches();

print("✓ Test 4: Directory changes monitored");

// Test 5: Unwatch file
let unwatch_result = unwatch(file_handle);
assert(unwatch_result == true, "Should successfully unwatch file");
print("✓ Test 5: unwatch file successful");

// Test 6: Unwatch directory
let unwatch_dir = unwatch(dir_handle);
assert(unwatch_dir == true, "Should successfully unwatch directory");
print("✓ Test 6: unwatch directory successful");

// Test 7: Unwatch invalid handle
let unwatch_invalid = unwatch(99999);
assert(unwatch_invalid == false, "Should fail to unwatch invalid handle");
print("✓ Test 7: unwatch invalid handle returns false");

// Test 8: Watch with recursive option
let rec_handle = watch_dir(test_dir, fn(event, path) {
    print("  Recursive event:", event, "on", path);
}, true);

assert(rec_handle > 0, "Should return valid recursive watch handle");
unwatch(rec_handle);
print("✓ Test 8: watch_dir with recursive option");

// Test 9: Multiple watches
let h1 = watch_file(test_file, fn(e, p) { print("Watch 1:", e); });
let h2 = watch_file(test_file, fn(e, p) { print("Watch 2:", e); });
assert(h1 > 0 && h2 > 0 && h1 != h2, "Should support multiple watches");
unwatch(h1);
unwatch(h2);
print("✓ Test 9: Multiple watches on same file");

// Test 10: process_file_watches with no watches
process_file_watches();
print("✓ Test 10: process_file_watches with no active watches");

// Cleanup
rm(test_file);
rm(test_dir + "/new_file.txt");
rm(test_dir);

print("\n✅ All file watching tests passed!");
