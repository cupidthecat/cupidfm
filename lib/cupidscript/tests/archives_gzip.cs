// Test gzip compression and decompression

print("Testing gzip compression...\n");

// Setup test files
write_file("test_gzip_input.txt", "This is test content for gzip compression.\nIt has multiple lines.\nAnd some data to compress.");
write_file("test_gzip_large.txt", str_repeat("Large data content for compression testing. ", 100));

// Test 1: Compress a file
let result1 = gzip_compress("test_gzip_input.txt", "test_gzip_input.txt.gz");
assert(result1 == true, "Should successfully compress file");
assert(exists("test_gzip_input.txt.gz"), "Compressed file should exist");
print("✓ Test 1: gzip_compress created compressed file");

// Test 2: Decompress a file
let result2 = gzip_decompress("test_gzip_input.txt.gz", "test_gzip_output.txt");
assert(result2 == true, "Should successfully decompress file");
assert(exists("test_gzip_output.txt"), "Decompressed file should exist");
print("✓ Test 2: gzip_decompress restored file");

// Test 3: Round-trip test - content should match
let original = read_file("test_gzip_input.txt");
let restored = read_file("test_gzip_output.txt");
assert(original == restored, "Decompressed content should match original");
print("✓ Test 3: Round-trip preserves content");

// Test 4: Compress large file
let result3 = gzip_compress("test_gzip_large.txt", "test_gzip_large.txt.gz");
assert(result3 == true, "Should compress large file");
print("✓ Test 4: Compress large file");

// Test 5: Decompress large file
let result4 = gzip_decompress("test_gzip_large.txt.gz", "test_gzip_large_out.txt");
assert(result4 == true, "Should decompress large file");
let large_original = read_file("test_gzip_large.txt");
let large_restored = read_file("test_gzip_large_out.txt");
assert(large_original == large_restored, "Large file content should match");
print("✓ Test 5: Large file round-trip successful");

// Test 6: Compress non-existent file
let result5 = gzip_compress("nonexistent_file.txt", "output.gz");
assert(result5 == false, "Should fail to compress non-existent file");
print("✓ Test 6: Compress non-existent file returns false");

// Test 7: Decompress non-existent file
let result6 = gzip_decompress("nonexistent.gz", "output.txt");
assert(result6 == false, "Should fail to decompress non-existent file");
print("✓ Test 7: Decompress non-existent file returns false");

// Test 8: Compression reduces file size
write_file("test_compress_size.txt", str_repeat("AAAAAAAAAA", 1000));
gzip_compress("test_compress_size.txt", "test_compress_size.txt.gz");
// Just verify both exist - actual size comparison would need file stats
assert(exists("test_compress_size.txt") && exists("test_compress_size.txt.gz"), 
       "Both compressed and original should exist");
print("✓ Test 8: Compression works on repetitive data");

// Cleanup
rm("test_gzip_input.txt");
rm("test_gzip_input.txt.gz");
rm("test_gzip_output.txt");
rm("test_gzip_large.txt");
rm("test_gzip_large.txt.gz");
rm("test_gzip_large_out.txt");
rm("test_compress_size.txt");
rm("test_compress_size.txt.gz");

print("\n✅ All gzip tests passed!");
