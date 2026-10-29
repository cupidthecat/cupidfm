// Archive Operations Demo
// Demonstrates compression and archiving with gzip, tar, and tar.gz

print("=== Archive Operations Demo ===\n");

// Setup: Create some test files
print("Setting up test files...");
mkdir("demo_data");
write_file("demo_data/readme.txt", "This is a readme file.\nIt contains documentation.");
write_file("demo_data/data.json", '{"name": "demo", "version": "1.0"}');
write_file("demo_data/script.cs", 'print("Hello from archived script!");');
write_file("demo_data/large_file.txt", str_repeat("This is repeated content. ", 200));

print("✓ Created test files in demo_data/\n");

// ===================
// GZIP Compression
// ===================
print("1. GZIP Compression:");

// Compress a file
let gzip_result = gzip_compress("demo_data/large_file.txt", "demo_data/large_file.txt.gz");
print("   Compressed large_file.txt:", gzip_result);

// Decompress it back
let gunzip_result = gzip_decompress("demo_data/large_file.txt.gz", "demo_data/large_file_restored.txt");
print("   Decompressed to large_file_restored.txt:", gunzip_result);

// Verify content
let original = read_file("demo_data/large_file.txt");
let restored = read_file("demo_data/large_file_restored.txt");
print("   Content matches:", original == restored);
print("   Original size:", len(original), "bytes");

// ===================
// TAR Archives
// ===================
print("\n2. TAR Archive Creation:");

// Create a tar archive
let files_to_archive = [
    "demo_data/readme.txt",
    "demo_data/data.json",
    "demo_data/script.cs"
];

let tar_result = tar_create("backup.tar", files_to_archive);
print("   Created backup.tar:", tar_result);

// List archive contents
let tar_contents = tar_list("backup.tar");
print("   Archive contains", len(tar_contents), "files:");
for file in tar_contents {
    print("     -", file);
}

// Extract the archive
mkdir("extracted");
let extract_result = tar_extract("backup.tar", "extracted");
print("   Extracted to 'extracted/':", extract_result);

// Verify extraction
print("   Extracted files:");
let extracted_files = list_dir("extracted");
for file in extracted_files {
    print("     -", file);
}

// ===================
// Compressed TAR (tar.gz)
// ===================
print("\n3. Compressed TAR Archive (tar.gz):");

// Create compressed archive
let targz_result = tar_create("backup.tar.gz", files_to_archive, "gzip");
print("   Created backup.tar.gz:", targz_result);

// List compressed archive
let targz_contents = tar_list("backup.tar.gz");
print("   Compressed archive contains", len(targz_contents), "files:");
for file in targz_contents {
    print("     -", file);
}

// Extract compressed archive
mkdir("extracted_gz");
let extract_gz_result = tar_extract("backup.tar.gz", "extracted_gz");
print("   Extracted compressed archive:", extract_gz_result);

// Verify content from compressed archive
if exists("extracted_gz/data.json") {
    let json_content = read_file("extracted_gz/data.json");
    print("   data.json from archive:", json_content);
}

// ===================
// Real-world Example: Backup System
// ===================
print("\n4. Real-world example - Backup System:");

// Create backup with timestamp
let timestamp = to_str(unix_s());
let backup_name = "backup_" + timestamp + ".tar.gz";

// Files to backup
let important_files = glob("*.cs", "demo_data");
print("   Backing up", len(important_files), "script files...");

let backup_result = tar_create(backup_name, important_files, "gzip");
print("   Backup created:", backup_name);
print("   Success:", backup_result);

// Verify backup
let backup_list = tar_list(backup_name);
print("   Backup contains:", len(backup_list), "files");

// ===================
// Example: Multiple Archives
// ===================
print("\n5. Creating multiple themed archives:");

// Archive by file type
let json_files = glob("*.json", "demo_data");
if len(json_files) > 0 {
    tar_create("data_archive.tar", json_files);
    print("   Created data_archive.tar with", len(json_files), "JSON files");
}

let txt_files = glob("*.txt", "demo_data");
if len(txt_files) > 0 {
    tar_create("docs_archive.tar.gz", txt_files, "gzip");
    print("   Created docs_archive.tar.gz with", len(txt_files), "text files");
}

// ===================
// Example: Round-trip Test
// ===================
print("\n6. Round-trip integrity test:");

// Create test content
let test_data = "Important data: " + to_str(unix_ms());
write_file("test_round_trip.txt", test_data);

// Compress
gzip_compress("test_round_trip.txt", "test_round_trip.txt.gz");

// Archive the compressed file
tar_create("test_archive.tar", ["test_round_trip.txt.gz"]);

// Extract archive
mkdir("round_trip_test");
tar_extract("test_archive.tar", "round_trip_test");

// Decompress
gzip_decompress("round_trip_test/test_round_trip.txt.gz", "round_trip_test/final.txt");

// Verify
let final_data = read_file("round_trip_test/final.txt");
print("   Original data:", test_data);
print("   Final data:", final_data);
print("   Integrity check:", test_data == final_data ? "✓ PASSED" : "✗ FAILED");

// ===================
// Cleanup
// ===================
print("\n7. Cleaning up demo files...");

// Remove test files
rm("demo_data/readme.txt");
rm("demo_data/data.json");
rm("demo_data/script.cs");
rm("demo_data/large_file.txt");
rm("demo_data/large_file.txt.gz");
rm("demo_data/large_file_restored.txt");
rm("demo_data");

// Remove archives
rm("backup.tar");
rm("backup.tar.gz");
rm(backup_name);
rm("data_archive.tar");
rm("docs_archive.tar.gz");
rm("test_archive.tar");
rm("test_round_trip.txt");
rm("test_round_trip.txt.gz");

// Remove extracted directories
let cleanup_dirs = ["extracted", "extracted_gz", "round_trip_test"];
for dir in cleanup_dirs {
    if is_dir(dir) {
        let files = list_dir(dir);
        for file in files {
            rm(dir + "/" + file);
        }
        rm(dir);
    }
}

print("✓ Cleanup complete");

print("\n=== Archive Operations Demo Complete ===");
print("\nKey features:");
print("  • gzip_compress/decompress - Single file compression");
print("  • tar_create - Bundle multiple files");
print("  • tar_list - View archive contents");
print("  • tar_extract - Restore files from archive");
print("  • tar.gz - Compressed archives for efficient storage");
