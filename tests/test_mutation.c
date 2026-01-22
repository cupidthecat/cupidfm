#include "mutation_test.h"
#include <assert.h>

#define MAX_PATH_LENGTH 1024

// Original (correct) path_join implementation
void path_join_correct(char *result, const char *base, const char *extra) {
    size_t base_len = strlen(base);
    size_t extra_len = strlen(extra);

    if (base_len == 0) {
        strncpy(result, extra, MAX_PATH_LENGTH);
    } else if (extra_len == 0) {
        strncpy(result, base, MAX_PATH_LENGTH);
    } else {
        if (base[base_len - 1] == '/') {
            snprintf(result, MAX_PATH_LENGTH, "%s%s", base, extra);
        } else {
            snprintf(result, MAX_PATH_LENGTH, "%s/%s", base, extra);
        }
    }

    result[MAX_PATH_LENGTH - 1] = '\0';
}

// Mutation 1: Missing null termination
void path_join_mut1(char *result, const char *base, const char *extra) {
    size_t base_len = strlen(base);
    size_t extra_len = strlen(extra);

    if (base_len == 0) {
        strncpy(result, extra, MAX_PATH_LENGTH);
    } else if (extra_len == 0) {
        strncpy(result, base, MAX_PATH_LENGTH);
    } else {
        if (base[base_len - 1] == '/') {
            snprintf(result, MAX_PATH_LENGTH, "%s%s", base, extra);
        } else {
            snprintf(result, MAX_PATH_LENGTH, "%s/%s", base, extra);
        }
    }
    // MUTATION: Missing null termination
    // result[MAX_PATH_LENGTH - 1] = '\0';
}

// Mutation 2: Wrong separator
void path_join_mut2(char *result, const char *base, const char *extra) {
    size_t base_len = strlen(base);
    size_t extra_len = strlen(extra);

    if (base_len == 0) {
        strncpy(result, extra, MAX_PATH_LENGTH);
    } else if (extra_len == 0) {
        strncpy(result, base, MAX_PATH_LENGTH);
    } else {
        if (base[base_len - 1] == '/') {
            snprintf(result, MAX_PATH_LENGTH, "%s%s", base, extra);
        } else {
            // MUTATION: Wrong separator
            snprintf(result, MAX_PATH_LENGTH, "%s\\%s", base, extra);
        }
    }

    result[MAX_PATH_LENGTH - 1] = '\0';
}

// Mutation 3: Always add separator
void path_join_mut3(char *result, const char *base, const char *extra) {
    size_t base_len = strlen(base);
    size_t extra_len = strlen(extra);

    if (base_len == 0) {
        strncpy(result, extra, MAX_PATH_LENGTH);
    } else if (extra_len == 0) {
        strncpy(result, base, MAX_PATH_LENGTH);
    } else {
        // MUTATION: Always add separator, even if base ends with /
        snprintf(result, MAX_PATH_LENGTH, "%s/%s", base, extra);
    }

    result[MAX_PATH_LENGTH - 1] = '\0';
}

// Mutation 4: Wrong empty base handling
void path_join_mut4(char *result, const char *base, const char *extra) {
    size_t base_len = strlen(base);
    size_t extra_len = strlen(extra);

    // MUTATION: Wrong empty base handling
    if (base_len == 0) {
        strncpy(result, "/", MAX_PATH_LENGTH);
        strncat(result, extra, MAX_PATH_LENGTH - strlen(result) - 1);
    } else if (extra_len == 0) {
        strncpy(result, base, MAX_PATH_LENGTH);
    } else {
        if (base[base_len - 1] == '/') {
            snprintf(result, MAX_PATH_LENGTH, "%s%s", base, extra);
        } else {
            snprintf(result, MAX_PATH_LENGTH, "%s/%s", base, extra);
        }
    }

    result[MAX_PATH_LENGTH - 1] = '\0';
}

// Mutation 5: Buffer overflow (no size check)
void path_join_mut5(char *result, const char *base, const char *extra) {
    size_t base_len = strlen(base);
    size_t extra_len = strlen(extra);

    if (base_len == 0) {
        // MUTATION: No size limit
        strcpy(result, extra);
    } else if (extra_len == 0) {
        // MUTATION: No size limit
        strcpy(result, base);
    } else {
        if (base[base_len - 1] == '/') {
            // MUTATION: No size limit
            sprintf(result, "%s%s", base, extra);
        } else {
            // MUTATION: No size limit
            sprintf(result, "%s/%s", base, extra);
        }
    }

    result[MAX_PATH_LENGTH - 1] = '\0';
}

// Mutation 6: Wrong empty extra handling
void path_join_mut6(char *result, const char *base, const char *extra) {
    size_t base_len = strlen(base);
    size_t extra_len = strlen(extra);

    if (base_len == 0) {
        strncpy(result, extra, MAX_PATH_LENGTH);
    } else if (extra_len == 0) {
        // MUTATION: Wrong empty extra handling
        strncpy(result, base, MAX_PATH_LENGTH);
        strncat(result, "/", MAX_PATH_LENGTH - strlen(result) - 1);
    } else {
        if (base[base_len - 1] == '/') {
            snprintf(result, MAX_PATH_LENGTH, "%s%s", base, extra);
        } else {
            snprintf(result, MAX_PATH_LENGTH, "%s/%s", base, extra);
        }
    }

    result[MAX_PATH_LENGTH - 1] = '\0';
}

// Mutation 7: Off-by-one in length check
void path_join_mut7(char *result, const char *base, const char *extra) {
    size_t base_len = strlen(base);
    size_t extra_len = strlen(extra);

    if (base_len == 0) {
        strncpy(result, extra, MAX_PATH_LENGTH);
    } else if (extra_len == 0) {
        strncpy(result, base, MAX_PATH_LENGTH);
    } else {
        if (base[base_len - 1] == '/') {
            snprintf(result, MAX_PATH_LENGTH, "%s%s", base, extra);
        } else {
            snprintf(result, MAX_PATH_LENGTH, "%s/%s", base, extra);
        }
    }

    // MUTATION: Off-by-one
    result[MAX_PATH_LENGTH] = '\0';
}

// Mutation 8: Reversed base and extra
void path_join_mut8(char *result, const char *base, const char *extra) {
    size_t base_len = strlen(base);
    size_t extra_len = strlen(extra);

    // MUTATION: Reversed base and extra
    if (extra_len == 0) {
        strncpy(result, base, MAX_PATH_LENGTH);
    } else if (base_len == 0) {
        strncpy(result, extra, MAX_PATH_LENGTH);
    } else {
        if (extra[extra_len - 1] == '/') {
            snprintf(result, MAX_PATH_LENGTH, "%s%s", extra, base);
        } else {
            snprintf(result, MAX_PATH_LENGTH, "%s/%s", extra, base);
        }
    }

    result[MAX_PATH_LENGTH - 1] = '\0';
}

// Test helper: Check if result is null-terminated
bool check_null_terminated(const char *result) {
    return result[MAX_PATH_LENGTH - 1] == '\0';
}

// Test helper: Check if result length is bounded
bool check_length_bounded(const char *result) {
    return strlen(result) < MAX_PATH_LENGTH;
}

// Test helper: Check normal path join
bool test_normal_path_join(void (*path_join_func)(char *, const char *, const char *)) {
    char result[MAX_PATH_LENGTH];
    path_join_func(result, "/home/user", "documents");
    return strcmp(result, "/home/user/documents") == 0;
}

// Test helper: Check empty base
bool test_empty_base(void (*path_join_func)(char *, const char *, const char *)) {
    char result[MAX_PATH_LENGTH];
    path_join_func(result, "", "test");
    return strcmp(result, "test") == 0;
}

// Test helper: Check empty extra
bool test_empty_extra(void (*path_join_func)(char *, const char *, const char *)) {
    char result[MAX_PATH_LENGTH];
    path_join_func(result, "/home", "");
    return strcmp(result, "/home") == 0;
}

// Test helper: Check base ending with slash
bool test_base_ends_slash(void (*path_join_func)(char *, const char *, const char *)) {
    char result[MAX_PATH_LENGTH];
    path_join_func(result, "/home/", "user");
    return strcmp(result, "/home/user") == 0;
}

int main(int argc, char *argv[]) {
    bool verbose = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        }
    }
    
    printf("=== Mutation Testing ===\n");
    printf("Testing if our tests catch intentional bugs (mutations)\n\n");
    
    mut_init(verbose);
    
    // Test Mutation 1: Missing null termination
    TEST_MUTATION_SHOULD_FAIL("Missing null termination",
        char result[MAX_PATH_LENGTH];
        memset(result, 'X', MAX_PATH_LENGTH); // Fill with non-null
        path_join_mut1(result, "/home", "user");
        // Check that result[MAX_PATH_LENGTH-1] is null (mutation doesn't set it)
        // Note: snprintf always null-terminates, so this mutation might not actually fail
        // Return FALSE when mutation detected (not null-terminated)
        bool test_passed = (result[MAX_PATH_LENGTH - 1] == '\0');
    , test_passed);
    
    // Test Mutation 2: Wrong separator
    TEST_MUTATION_SHOULD_FAIL("Wrong separator (\\ instead of /) - basic",
        char result[MAX_PATH_LENGTH];
        path_join_mut2(result, "/home", "user");
        // Should be "/home/user" but mutation produces "/home\\user"
        // Return FALSE when mutation detected (wrong result)
        bool test_passed = (strcmp(result, "/home/user") == 0);
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Wrong separator - check for backslash",
        char result[MAX_PATH_LENGTH];
        path_join_mut2(result, "dir1", "dir2");
        // Check if result contains backslash (mutation uses \ instead of /)
        // Return FALSE when mutation detected (has backslash)
        bool test_passed = (strchr(result, '\\') == NULL);
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Wrong separator - multiple paths",
        char result[MAX_PATH_LENGTH];
        path_join_mut2(result, "a", "b");
        // Return FALSE when mutation detected (wrong result)
        bool test_passed = (strcmp(result, "a/b") == 0);
    , test_passed);
    
    // Test Mutation 3: Always add separator
    TEST_MUTATION_SHOULD_FAIL("Always add separator - base ends with /",
        char result[MAX_PATH_LENGTH];
        path_join_mut3(result, "/home/", "user");
        // Should be "/home/user" but mutation produces "/home//user"
        bool test_passed = (strcmp(result, "/home/user") == 0);
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Always add separator - check for double slash",
        char result[MAX_PATH_LENGTH];
        path_join_mut3(result, "/dir/", "file");
        // Check if result contains "//" (mutation always adds /)
        // Return FALSE when mutation detected (has double slash)
        bool test_passed = (strstr(result, "//") == NULL);
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Always add separator - root path",
        char result[MAX_PATH_LENGTH];
        path_join_mut3(result, "/", "home");
        // Should be "/home" but mutation produces "//home"
        bool test_passed = (strcmp(result, "/home") == 0);
    , test_passed);
    
    // Test Mutation 4: Wrong empty base handling
    TEST_MUTATION_SHOULD_FAIL("Wrong empty base handling - basic",
        char result[MAX_PATH_LENGTH];
        path_join_mut4(result, "", "test");
        // Should be "test" but mutation produces "/test"
        bool test_passed = (strcmp(result, "test") == 0);
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Wrong empty base handling - check for leading slash",
        char result[MAX_PATH_LENGTH];
        path_join_mut4(result, "", "file.txt");
        // Result should NOT start with / when base is empty
        // Return FALSE when mutation detected (starts with /)
        bool test_passed = (result[0] != '/');
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Wrong empty base handling - path with subdirs",
        char result[MAX_PATH_LENGTH];
        path_join_mut4(result, "", "dir/subdir/file");
        bool test_passed = (strcmp(result, "dir/subdir/file") == 0);
    , test_passed);
    
    // Test Mutation 5: Buffer overflow (no size check) - test with long strings
    TEST_MUTATION_SHOULD_FAIL("Buffer overflow (no size check)",
        char result[MAX_PATH_LENGTH];
        char long_base[2000];
        char long_extra[2000];
        memset(long_base, 'a', sizeof(long_base) - 1);
        long_base[sizeof(long_base) - 1] = '\0';
        memset(long_extra, 'b', sizeof(long_extra) - 1);
        long_extra[sizeof(long_extra) - 1] = '\0';
        path_join_mut5(result, long_base, long_extra);
        // Check if result was properly bounded (mutation doesn't check, so might overflow)
        // Return FALSE when mutation detected (not properly bounded)
        bool test_passed = (strlen(result) < MAX_PATH_LENGTH && result[MAX_PATH_LENGTH - 1] == '\0');
    , test_passed);
    
    // Test Mutation 6: Wrong empty extra handling
    TEST_MUTATION_SHOULD_FAIL("Wrong empty extra handling - basic",
        char result[MAX_PATH_LENGTH];
        path_join_mut6(result, "/home", "");
        // Should be "/home" but mutation produces "/home/"
        bool test_passed = (strcmp(result, "/home") == 0);
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Wrong empty extra handling - check for trailing slash",
        char result[MAX_PATH_LENGTH];
        path_join_mut6(result, "/home", "");
        // Result should NOT end with / when extra is empty
        // Return FALSE when mutation detected (ends with /)
        size_t len = strlen(result);
        bool test_passed = (len == 0 || result[len - 1] != '/');
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Wrong empty extra handling - relative path",
        char result[MAX_PATH_LENGTH];
        path_join_mut6(result, "dir", "");
        bool test_passed = (strcmp(result, "dir") == 0);
    , test_passed);
    
    // Test Mutation 7: Off-by-one in length check
    TEST_MUTATION_SHOULD_FAIL("Off-by-one in length check - boundary",
        char result[MAX_PATH_LENGTH + 1];
        memset(result, 'X', MAX_PATH_LENGTH + 1); // Fill with non-null
        path_join_mut7(result, "/home", "user");
        // Mutation writes to result[MAX_PATH_LENGTH] instead of result[MAX_PATH_LENGTH-1]
        // So result[MAX_PATH_LENGTH-1] might not be null
        // Return FALSE when mutation detected (not null-terminated at correct position)
        bool test_passed = (result[MAX_PATH_LENGTH - 1] == '\0');
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Off-by-one - check exact boundary",
        char result[MAX_PATH_LENGTH + 1];
        memset(result, 'X', MAX_PATH_LENGTH + 1);
        char long_path[600];
        memset(long_path, 'a', sizeof(long_path) - 1);
        long_path[sizeof(long_path) - 1] = '\0';
        path_join_mut7(result, long_path, "b");
        // Check that boundary is properly null-terminated
        bool test_passed = (result[MAX_PATH_LENGTH - 1] == '\0');
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Off-by-one - length validation",
        char result[MAX_PATH_LENGTH + 1];
        memset(result, 'X', MAX_PATH_LENGTH + 1);
        path_join_mut7(result, "a", "b");
        // Verify result length is within bounds
        bool test_passed = (strlen(result) < MAX_PATH_LENGTH);
    , test_passed);
    
    // Test Mutation 8: Reversed base and extra
    TEST_MUTATION_SHOULD_FAIL("Reversed base and extra - basic",
        char result[MAX_PATH_LENGTH];
        path_join_mut8(result, "/home", "user");
        // Should be "/home/user" but mutation produces "user/home"
        bool test_passed = (strcmp(result, "/home/user") == 0);
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Reversed base and extra - check order",
        char result[MAX_PATH_LENGTH];
        path_join_mut8(result, "base", "extra");
        // Result should start with "base", not "extra"
        bool test_passed = (strncmp(result, "base", 4) == 0);
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Reversed base and extra - with slashes",
        char result[MAX_PATH_LENGTH];
        path_join_mut8(result, "/dir1", "dir2");
        // Should be "/dir1/dir2" but mutation produces "dir2/dir1"
        bool test_passed = (strcmp(result, "/dir1/dir2") == 0);
    , test_passed);
    
    TEST_MUTATION_SHOULD_FAIL("Reversed base and extra - absolute path",
        char result[MAX_PATH_LENGTH];
        path_join_mut8(result, "/", "home");
        // Should be "/home" but mutation might produce "home/"
        bool test_passed = (strcmp(result, "/home") == 0);
    , test_passed);
    
    mut_print_summary();
    
    return mut_state.mutations_survived > 0 ? 1 : 0;
}

