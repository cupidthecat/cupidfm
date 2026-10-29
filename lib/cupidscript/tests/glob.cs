// Test glob pattern matching

print("Testing glob patterns...\n");

// Create test directory structure
mkdir("test_glob_data");
write_file("test_glob_data/file1.txt", "content1");
write_file("test_glob_data/file2.txt", "content2");
write_file("test_glob_data/file3.cs", "content3");
write_file("test_glob_data/test_file.txt", "content4");
mkdir("test_glob_data/subdir");
write_file("test_glob_data/subdir/nested.txt", "nested content");
write_file("test_glob_data/subdir/nested.cs", "nested script");

// Test 1: Simple wildcard - *.txt
let txt_files = glob("*.txt", "test_glob_data");
assert(len(txt_files) == 3, "Should find 3 .txt files");
print("✓ Test 1: Simple wildcard *.txt found", len(txt_files), "files");

// Test 2: Wildcard with prefix - test_*
let test_files = glob("test_*", "test_glob_data");
assert(len(test_files) >= 1, "Should find test_* files");
print("✓ Test 2: Pattern test_* found", len(test_files), "files");

// Test 3: Pattern - *.cs
let cs_files = glob("*.cs", "test_glob_data");
assert(len(cs_files) >= 1, "Should find .cs files");
print("✓ Test 3: Pattern *.cs found", len(cs_files), "files");

// Test 4: Recursive pattern - **/*.txt
let all_txt = glob("**/*.txt", "test_glob_data");
assert(len(all_txt) >= 4, "Should find all .txt files recursively");
print("✓ Test 4: Recursive pattern **/*.txt found", len(all_txt), "files");

// Test 5: Recursive pattern - **/*.cs
let all_cs = glob("**/*.cs", "test_glob_data");
assert(len(all_cs) >= 2, "Should find all .cs files recursively");
print("✓ Test 5: Recursive pattern **/*.cs found", len(all_cs), "files");

// Test 6: No matches
let no_match = glob("*.xyz", "test_glob_data");
assert(len(no_match) == 0, "Should find no .xyz files");
print("✓ Test 6: No matches for *.xyz");

// Test 7: Single wildcard ?
write_file("test_glob_data/a.txt", "a");
write_file("test_glob_data/b.txt", "b");
let single_char = glob("?.txt", "test_glob_data");
assert(len(single_char) >= 2, "Should find single char + .txt files");
print("✓ Test 7: Single char wildcard ? found", len(single_char), "files");

// Test 8: From current directory
let current_glob = glob("*.cs");
assert(len(current_glob) >= 0, "Should return list from current dir");
print("✓ Test 8: Glob from current directory");

// Test 9: Non-existent directory
let bad_glob = glob("*.txt", "nonexistent_dir_12345");
assert(len(bad_glob) == 0, "Should return empty list for non-existent dir");
print("✓ Test 9: Non-existent directory returns empty list");

// Test 10: All files with *
let all_files = glob("*", "test_glob_data");
assert(len(all_files) >= 5, "Should find all files in directory");
print("✓ Test 10: Pattern * found", len(all_files), "files");

// Cleanup
rm("test_glob_data/file1.txt");
rm("test_glob_data/file2.txt");
rm("test_glob_data/file3.cs");
rm("test_glob_data/test_file.txt");
rm("test_glob_data/a.txt");
rm("test_glob_data/b.txt");
rm("test_glob_data/subdir/nested.txt");
rm("test_glob_data/subdir/nested.cs");
rm("test_glob_data/subdir");
rm("test_glob_data");

print("\n✅ All glob tests passed!");
