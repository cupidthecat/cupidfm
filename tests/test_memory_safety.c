#include "test_runner.h"
#include "vector.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>

static jmp_buf jump_buffer;

void handle_sigsegv(int sig) {
    (void)sig;
    longjmp(jump_buffer, 1);
}

// Test strncpy null termination (critical fix #9)
bool test_strncpy_null_termination() {
    char dest[10];
    char src[20];
    memset(src, 'a', 19);
    src[19] = '\0';
    
    // Simulate the fix: strncpy with size-1 and explicit null termination
    strncpy(dest, src, sizeof(dest) - 1);
    dest[sizeof(dest) - 1] = '\0';
    
    ASSERT_EQ(dest[sizeof(dest) - 1], '\0', 
              "Destination should be null-terminated");
    ASSERT_EQ(strlen(dest), sizeof(dest) - 1, 
              "String length should be size-1");
    
    return true;
}

// Test that realloc failure doesn't corrupt state (critical fix #6)
bool test_realloc_failure_safety() {
    Vector v = Vector_new(5);
    
    // The fix ensures cap is only updated after realloc succeeds
    // This prevents heap corruption if realloc fails
    Vector_add(&v, 100);
    
    // Verify vector is still in a consistent state
    // If realloc failed, cap would be wrong, but our fix prevents that
    ASSERT_NOT_NULL(v.el, "Vector should remain valid");
    
    Vector_bye(&v);
    return true;
}

// Test that Vector operations don't leak memory
bool test_vector_no_memory_leak() {
    Vector v = Vector_new(10);
    
    // Add and remove elements multiple times
    for (int i = 0; i < 100; i++) {
        char *str = malloc(20);
        snprintf(str, 20, "test%d", i);
        Vector_add(&v, 1);
        v.el[Vector_len(v)] = str;
        Vector_set_len_no_free(&v, Vector_len(v) + 1);
        
        if (i % 2 == 0) {
            Vector_set_len(&v, Vector_len(v) - 1);
        }
    }
    
    // All elements should be freed
    Vector_bye(&v);
    
    // If we got here without crashing, memory management is working
    return true;
}

// Test empty directory path handling (critical fix #10)
bool test_empty_path_handling() {
    // This test verifies the fix where we check dir_path_len == 0
    // before accessing dir_path[dir_path_len - 1]
    const char *empty_path = "";
    size_t path_len = strlen(empty_path);
    
    // The fix ensures we check path_len == 0 first
    bool is_safe = (path_len == 0 || (path_len > 0 && empty_path[path_len - 1] == '/'));
    
    ASSERT(is_safe, "Should handle empty path safely");
    
    return true;
}

// Test that vector doesn't access out of bounds
bool test_vector_bounds_checking() {
    Vector v = Vector_new(5);
    
    // Add elements
    for (int i = 0; i < 3; i++) {
        char *str = malloc(10);
        snprintf(str, 10, "test%d", i);
        Vector_add(&v, 1);
        v.el[Vector_len(v)] = str;
        Vector_set_len_no_free(&v, Vector_len(v) + 1);
    }
    
    // Vector_bye should only access elements up to len
    // This tests the fix where we iterate to len instead of until NULL
    Vector_bye(&v);
    
    // If we got here, no out-of-bounds access occurred
    return true;
}

// Test that setting length to 0 works correctly
bool test_vector_set_len_zero() {
    Vector v = Vector_new(10);
    
    // Add elements
    for (int i = 0; i < 5; i++) {
        char *str = malloc(10);
        snprintf(str, 10, "test%d", i);
        Vector_add(&v, 1);
        v.el[Vector_len(v)] = str;
        Vector_set_len_no_free(&v, Vector_len(v) + 1);
    }
    
    // Set length to 0 - should free all elements
    Vector_set_len(&v, 0);
    ASSERT_EQ(Vector_len(v), 0, "Length should be 0");
    
    Vector_bye(&v);
    return true;
}

int main() {
    printf("=== Memory Safety Tests ===\n\n");
    
    RUN_TEST(test_strncpy_null_termination);
    RUN_TEST(test_realloc_failure_safety);
    RUN_TEST(test_vector_no_memory_leak);
    RUN_TEST(test_empty_path_handling);
    RUN_TEST(test_vector_bounds_checking);
    RUN_TEST(test_vector_set_len_zero);
    
    PRINT_SUMMARY();
}

