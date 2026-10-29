// Test tar archive operations

print("Testing tar archives...\n");

// Setup test files
mkdir("test_tar_data");
write_file("test_tar_data/file1.txt", "Content of file 1");
write_file("test_tar_data/file2.txt", "Content of file 2");
write_file("test_tar_data/file3.cs", "print('hello');");

// Test 1: Create tar archive
let files = ["test_tar_data/file1.txt", "test_tar_data/file2.txt", "test_tar_data/file3.cs"];
let result1 = tar_create("test_archive.tar", files);
assert(result1 == true, "Should successfully create tar archive");
assert(exists("test_archive.tar"), "Tar archive should exist");
print("✓ Test 1: tar_create created archive");

// Test 2: List tar archive contents
let contents = tar_list("test_archive.tar");
assert(typeof(contents) == "list", "Should return list");
assert(len(contents) == 3, "Should contain 3 files");
print("✓ Test 2: tar_list shows", len(contents), "files");

// Test 3: Extract tar archive
mkdir("test_tar_extract");
let result2 = tar_extract("test_archive.tar", "test_tar_extract");
assert(result2 == true, "Should successfully extract tar");
assert(exists("test_tar_extract/file1.txt"), "Extracted file1 should exist");
assert(exists("test_tar_extract/file2.txt"), "Extracted file2 should exist");
print("✓ Test 3: tar_extract restored files");

// Test 4: Verify extracted content
let content1 = read_file("test_tar_extract/file1.txt");
assert(content1 == "Content of file 1", "Extracted content should match");
print("✓ Test 4: Extracted content matches original");

// Test 5: Create compressed tar (tar.gz)
let result3 = tar_create("test_archive.tar.gz", files, "gzip");
assert(result3 == true, "Should create compressed tar");
assert(exists("test_archive.tar.gz"), "Compressed tar should exist");
print("✓ Test 5: tar_create with gzip compression");

// Test 6: List compressed tar
let contents_gz = tar_list("test_archive.tar.gz");
assert(typeof(contents_gz) == "list", "Should list compressed tar");
assert(len(contents_gz) == 3, "Should contain 3 files");
print("✓ Test 6: tar_list works on compressed archive");

// Test 7: Extract compressed tar
mkdir("test_tar_extract_gz");
let result4 = tar_extract("test_archive.tar.gz", "test_tar_extract_gz");
assert(result4 == true, "Should extract compressed tar");
assert(exists("test_tar_extract_gz/file1.txt"), "File from compressed tar should exist");
print("✓ Test 7: tar_extract works on compressed archive");

// Test 8: Create archive with empty list
let result5 = tar_create("empty_archive.tar", []);
assert(result5 == true, "Should create archive with no files");
print("✓ Test 8: Create tar with empty file list");

// Test 9: List non-existent archive
let contents_bad = tar_list("nonexistent.tar");
assert(contents_bad == nil, "Should return nil for non-existent archive");
print("✓ Test 9: List non-existent archive returns nil");

// Test 10: Extract to non-existent directory (should create it)
let result6 = tar_extract("test_archive.tar", "new_extract_dir");
assert(result6 == true, "Should create directory and extract");
assert(is_dir("new_extract_dir"), "Extract should create directory");
print("✓ Test 10: Extract creates destination directory");

// Cleanup
rm("test_tar_data/file1.txt");
rm("test_tar_data/file2.txt");
rm("test_tar_data/file3.cs");
rm("test_tar_data");
rm("test_archive.tar");
rm("test_archive.tar.gz");
rm("empty_archive.tar");
rm("test_tar_extract/file1.txt");
rm("test_tar_extract/file2.txt");
rm("test_tar_extract/file3.cs");
rm("test_tar_extract");
rm("test_tar_extract_gz/file1.txt");
rm("test_tar_extract_gz/file2.txt");
rm("test_tar_extract_gz/file3.cs");
rm("test_tar_extract_gz");
rm("new_extract_dir/file1.txt");
rm("new_extract_dir/file2.txt");
rm("new_extract_dir/file3.cs");
rm("new_extract_dir");

print("\n✅ All tar tests passed!");
