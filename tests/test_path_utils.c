#include "test_runner.h"
#include "../src/utils.h"
#include <string.h>

#define MAX_PATH_LENGTH 1024

// Test path_join with normal paths
bool test_path_join_normal() {
    char result[MAX_PATH_LENGTH];
    
    path_join(result, "/home/user", "documents");
    ASSERT_STR_EQ(result, "/home/user/documents", "Should join paths correctly");
    
    path_join(result, "/home/user/", "documents");
    ASSERT_STR_EQ(result, "/home/user/documents", "Should handle trailing slash");
    
    return true;
}

// Test path_join with empty base
bool test_path_join_empty_base() {
    char result[MAX_PATH_LENGTH];
    
    path_join(result, "", "documents");
    ASSERT_STR_EQ(result, "documents", "Should handle empty base");
    
    return true;
}

// Test path_join with empty extra
bool test_path_join_empty_extra() {
    char result[MAX_PATH_LENGTH];
    
    path_join(result, "/home/user", "");
    ASSERT_STR_EQ(result, "/home/user", "Should handle empty extra");
    
    return true;
}

// Test path_join null termination (critical fix #9)
bool test_path_join_null_termination() {
    char result[MAX_PATH_LENGTH];
    
    // Test with maximum length path
    char long_base[MAX_PATH_LENGTH];
    char long_extra[100];
    memset(long_base, 'a', MAX_PATH_LENGTH - 100);
    long_base[MAX_PATH_LENGTH - 100] = '\0';
    memset(long_extra, 'b', 99);
    long_extra[99] = '\0';
    
    path_join(result, long_base, long_extra);
    
    // Verify null termination
    ASSERT_EQ(result[MAX_PATH_LENGTH - 1], '\0', 
              "Result should be null-terminated");
    
    return true;
}

// Test path_join buffer overflow protection
bool test_path_join_buffer_overflow() {
    char result[MAX_PATH_LENGTH];
    
    // Create paths that would overflow if not protected
    char very_long_base[MAX_PATH_LENGTH * 2];
    char very_long_extra[MAX_PATH_LENGTH * 2];
    memset(very_long_base, 'a', MAX_PATH_LENGTH * 2 - 1);
    very_long_base[MAX_PATH_LENGTH * 2 - 1] = '\0';
    memset(very_long_extra, 'b', MAX_PATH_LENGTH * 2 - 1);
    very_long_extra[MAX_PATH_LENGTH * 2 - 1] = '\0';
    
    path_join(result, very_long_base, very_long_extra);
    
    // Result should be truncated but null-terminated
    ASSERT_EQ(result[MAX_PATH_LENGTH - 1], '\0', 
              "Result should be null-terminated even when truncated");
    ASSERT_EQ(strlen(result), MAX_PATH_LENGTH - 1, 
              "Result should be truncated to MAX_PATH_LENGTH - 1");
    
    return true;
}

// Test path_join with root path
bool test_path_join_root() {
    char result[MAX_PATH_LENGTH];
    
    path_join(result, "/", "home");
    ASSERT_STR_EQ(result, "/home", "Should handle root path");
    
    return true;
}

// Test path_join edge cases
bool test_path_join_edge_cases() {
    char result[MAX_PATH_LENGTH];
    
    // Both empty
    path_join(result, "", "");
    ASSERT_STR_EQ(result, "", "Should handle both empty");
    
    // Base is just "/"
    path_join(result, "/", "");
    ASSERT_STR_EQ(result, "/", "Should handle root only");
    
    return true;
}

int main() {
    printf("=== Path Utils Tests ===\n\n");
    
    RUN_TEST(test_path_join_normal);
    RUN_TEST(test_path_join_empty_base);
    RUN_TEST(test_path_join_empty_extra);
    RUN_TEST(test_path_join_null_termination);
    RUN_TEST(test_path_join_buffer_overflow);
    RUN_TEST(test_path_join_root);
    RUN_TEST(test_path_join_edge_cases);
    
    PRINT_SUMMARY();
}

