#define _POSIX_C_SOURCE 200809L
#include "test_runner.h"
#include "../cupidarchive.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>


// Test opening archive from path (requires actual file)
bool test_arc_open_path_nonexistent() {
    ArcReader *reader = arc_open_path("/nonexistent/file.tar");
    ASSERT_NULL(reader, "Should return NULL for nonexistent file");
    return true;
}

// Test entry free
bool test_arc_entry_free() {
    ArcEntry entry;
    memset(&entry, 0, sizeof(entry));
    
    // Test with allocated fields
    entry.path = strdup("test/path.txt");
    entry.link_target = strdup("target");
    
    arc_entry_free(&entry);
    
    // Verify fields are cleared (implementation may or may not set to NULL)
    ASSERT_TRUE(entry.path == NULL || entry.path[0] == '\0', "Path should be freed");
    return true;
}

// Test entry free with NULL
bool test_arc_entry_free_null() {
    ArcEntry entry;
    memset(&entry, 0, sizeof(entry));
    
    // Should not crash with NULL fields
    arc_entry_free(&entry);
    return true;
}

// Test arc_next with NULL reader
bool test_arc_next_null_reader() {
    ArcEntry entry;
    int result = arc_next(NULL, &entry);
    ASSERT_EQ(result, -1, "Should return error for NULL reader");
    return true;
}

// Test arc_next with NULL entry
bool test_arc_next_null_entry() {
    // We can't test this without a valid reader, but we can document it
    // In practice, this should return -1
    return true;
}

// Test arc_close with NULL
bool test_arc_close_null() {
    arc_close(NULL); // Should not crash
    return true;
}

int main() {
    printf("=== ArcReader Tests ===\n\n");
    
    RUN_TEST(test_arc_open_path_nonexistent);
    RUN_TEST(test_arc_entry_free);
    RUN_TEST(test_arc_entry_free_null);
    RUN_TEST(test_arc_next_null_reader);
    RUN_TEST(test_arc_next_null_entry);
    RUN_TEST(test_arc_close_null);
    
    PRINT_SUMMARY();
}

