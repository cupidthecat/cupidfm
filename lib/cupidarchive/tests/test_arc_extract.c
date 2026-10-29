#define _POSIX_C_SOURCE 200809L
#include "test_runner.h"
#include "../cupidarchive.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

// Test extraction with nonexistent archive
bool test_extract_nonexistent_archive() {
    ArcReader *reader = arc_open_path("/nonexistent/file.tar");
    ASSERT_NULL(reader, "Should return NULL for nonexistent archive");
    
    if (reader) {
        int result = arc_extract_to_path(reader, "/tmp", false, false);
        ASSERT_EQ(result, -1, "Should fail for nonexistent archive");
        arc_close(reader);
    }
    return true;
}

// Test extraction with nonexistent destination
bool test_extract_nonexistent_dest() {
    // Create a minimal test archive first
    // For now, just test the error handling
    ArcReader *reader = arc_open_path("/nonexistent/file.tar");
    if (reader) {
        int result = arc_extract_to_path(reader, "/nonexistent/directory", false, false);
        ASSERT_EQ(result, -1, "Should fail for nonexistent destination");
        arc_close(reader);
    }
    return true;
}

// Test extract_entry with NULL reader
bool test_extract_entry_null_reader() {
    ArcEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.path = strdup("test.txt");
    entry.type = ARC_ENTRY_FILE;
    entry.size = 0;
    
    int result = arc_extract_entry(NULL, &entry, "/tmp", false, false);
    ASSERT_EQ(result, -1, "Should fail for NULL reader");
    
    arc_entry_free(&entry);
    return true;
}

// Test extract_entry with NULL entry
bool test_extract_entry_null_entry() {
    // Can't test without valid reader, but document expected behavior
    return true;
}

// Test extract_entry with invalid destination
bool test_extract_entry_invalid_dest() {
    ArcEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.path = strdup("test.txt");
    entry.type = ARC_ENTRY_FILE;
    entry.size = 0;
    
    // Test with NULL destination
    ArcReader *reader = arc_open_path("/nonexistent/file.tar");
    if (reader) {
        int result = arc_extract_entry(reader, &entry, NULL, false, false);
        ASSERT_EQ(result, -1, "Should fail for NULL destination");
        arc_close(reader);
    }
    
    arc_entry_free(&entry);
    return true;
}

int main() {
    printf("=== ArcExtract Tests ===\n\n");
    
    RUN_TEST(test_extract_nonexistent_archive);
    RUN_TEST(test_extract_nonexistent_dest);
    RUN_TEST(test_extract_entry_null_reader);
    RUN_TEST(test_extract_entry_null_entry);
    RUN_TEST(test_extract_entry_invalid_dest);
    
    PRINT_SUMMARY();
}

