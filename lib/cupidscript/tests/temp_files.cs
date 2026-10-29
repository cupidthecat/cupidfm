// Test temporary file handling

print("Testing temp files...\n");

// Test 1: Create temp file without arguments
let temp1 = temp_file();
assert(typeof(temp1) == "string", "Should return string path");
assert(exists(temp1), "Temp file should exist");
print("✓ Test 1: temp_file() created:", temp1);

// Test 2: Create temp file with prefix
let temp2 = temp_file("myapp_");
assert(typeof(temp2) == "string", "Should return string path");
assert(exists(temp2), "Temp file with prefix should exist");
assert(str_contains(temp2, "myapp_"), "Should contain prefix");
print("✓ Test 2: temp_file with prefix created:", temp2);

// Test 3: Create temp file with prefix and suffix
let temp3 = temp_file("test_", ".txt");
assert(typeof(temp3) == "string", "Should return string path");
assert(exists(temp3), "Temp file with suffix should exist");
assert(str_endswith(temp3, ".txt"), "Should have .txt suffix");
print("✓ Test 3: temp_file with suffix created:", temp3);

// Test 4: Write to temp file
write_file(temp1, "temporary data");
let content = read_file(temp1);
assert(content == "temporary data", "Should be able to write to temp file");
print("✓ Test 4: Can write to temp file");

// Test 5: Create temp directory
let tempdir = temp_dir();
assert(typeof(tempdir) == "string", "Should return string path");
assert(is_dir(tempdir), "Temp dir should be a directory");
print("✓ Test 5: temp_dir() created:", tempdir);

// Test 6: Create temp directory with prefix
let tempdir2 = temp_dir("work_");
assert(typeof(tempdir2) == "string", "Should return string path");
assert(is_dir(tempdir2), "Temp dir with prefix should be a directory");
assert(str_contains(tempdir2, "work_"), "Should contain prefix");
print("✓ Test 6: temp_dir with prefix created:", tempdir2);

// Test 7: Use temp directory
write_file(tempdir + "/test.txt", "test content");
assert(exists(tempdir + "/test.txt"), "Should be able to create files in temp dir");
print("✓ Test 7: Can create files in temp directory");

// Test 8: Multiple temp files are unique
let temp_a = temp_file();
let temp_b = temp_file();
assert(temp_a != temp_b, "Temp files should have unique paths");
print("✓ Test 8: Multiple temp files have unique paths");

// Test 9: Temp files in system temp directory
assert(str_contains(temp1, "tmp") || str_contains(temp1, "TEMP"), 
       "Should be in system temp directory");
print("✓ Test 9: Temp files created in system temp directory");

// Test 10: Temp directory operations
let files_before = list_dir(tempdir);
write_file(tempdir + "/file1.txt", "data1");
write_file(tempdir + "/file2.txt", "data2");
let files_after = list_dir(tempdir);
assert(len(files_after) >= 2, "Should be able to add files to temp dir");
print("✓ Test 10: Temp directory supports operations");

// Note: Auto-cleanup happens on process exit, so we manually clean some for testing
rm(temp1);
rm(temp2);
rm(temp3);
rm(tempdir + "/test.txt");
rm(tempdir + "/file1.txt");
rm(tempdir + "/file2.txt");
rm(tempdir);
rm(temp_a);
rm(temp_b);
rm(tempdir2);

print("\n✅ All temp file tests passed!");
