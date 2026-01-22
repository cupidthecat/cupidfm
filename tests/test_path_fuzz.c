#include "test_runner.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define MAX_PATH_LENGTH 1024
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

// Standalone path_join implementation for testing (same as test_path_join.c)
void path_join(char *result, const char *base, const char *extra) {
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

// Simple PRNG for fuzzing (linear congruential generator)
static uint32_t fuzz_seed = 1;

static uint32_t fuzz_rand(void) {
    fuzz_seed = fuzz_seed * 1103515245 + 12345;
    return (fuzz_seed / 65536) % 32768;
}

static void fuzz_srand(uint32_t seed) {
    fuzz_seed = seed;
}

// Generate random string of given length (currently unused but kept for future fuzzing)
__attribute__((unused))
static void generate_random_string(char *buf, size_t len, bool printable_only) {
    for (size_t i = 0; i < len - 1; i++) {
        if (printable_only) {
            buf[i] = 32 + (fuzz_rand() % 95); // Printable ASCII
        } else {
            buf[i] = fuzz_rand() % 256; // Any byte
        }
    }
    buf[len - 1] = '\0';
}

// Generate random path-like string
static void generate_random_path(char *buf, size_t len) {
    size_t pos = 0;
    bool add_slash = (fuzz_rand() % 3 == 0); // 33% chance of starting with /
    
    if (add_slash && pos < len - 1) {
        buf[pos++] = '/';
    }
    
    // Generate path segments
    int segments = 1 + (fuzz_rand() % 5); // 1-5 segments
    for (int s = 0; s < segments && pos < len - 10; s++) {
        if (s > 0 && pos < len - 1) {
            buf[pos++] = '/';
        }
        
        // Generate segment name (3-20 chars)
        int seg_len = 3 + (fuzz_rand() % 18);
        for (int i = 0; i < seg_len && pos < len - 1; i++) {
            char c;
            int r = fuzz_rand() % 100;
            if (r < 70) {
                c = 'a' + (fuzz_rand() % 26); // lowercase
            } else if (r < 85) {
                c = 'A' + (fuzz_rand() % 26); // uppercase
            } else if (r < 95) {
                c = '0' + (fuzz_rand() % 10); // digits
            } else {
                c = '_'; // underscore
            }
            buf[pos++] = c;
        }
    }
    
    buf[pos] = '\0';
}

// Test path_join with random inputs
bool test_path_join_fuzz_random() {
    char base[MAX_PATH_LENGTH * 2];
    char extra[MAX_PATH_LENGTH * 2];
    char result[MAX_PATH_LENGTH];
    
    // Run 1000 random tests
    for (int i = 0; i < 1000; i++) {
        fuzz_srand(time(NULL) + i);
        
        // Generate random base and extra
        size_t base_len = 1 + (fuzz_rand() % (MAX_PATH_LENGTH * 2 - 1));
        size_t extra_len = 1 + (fuzz_rand() % (MAX_PATH_LENGTH * 2 - 1));
        
        generate_random_path(base, base_len);
        generate_random_path(extra, extra_len);
        
        // Test path_join - should never crash or overflow
        path_join(result, base, extra);
        
        // Verify result is null-terminated
        ASSERT_EQ(result[MAX_PATH_LENGTH - 1], '\0', 
                  "Result must be null-terminated");
        
        // Verify result length is within bounds
        size_t result_len = strlen(result);
        ASSERT(result_len < MAX_PATH_LENGTH, 
               "Result length must be less than MAX_PATH_LENGTH");
    }
    
    return true;
}

// Test path_join with very long inputs
bool test_path_join_fuzz_long_paths() {
    char base[MAX_PATH_LENGTH * 3];
    char extra[MAX_PATH_LENGTH * 3];
    char result[MAX_PATH_LENGTH];
    
    // Fill with maximum length paths
    memset(base, 'a', MAX_PATH_LENGTH * 3 - 1);
    base[MAX_PATH_LENGTH * 3 - 1] = '\0';
    
    memset(extra, 'b', MAX_PATH_LENGTH * 3 - 1);
    extra[MAX_PATH_LENGTH * 3 - 1] = '\0';
    
    // Should handle gracefully without overflow
    path_join(result, base, extra);
    
    ASSERT_EQ(result[MAX_PATH_LENGTH - 1], '\0', 
              "Result must be null-terminated even with long inputs");
    ASSERT_EQ(strlen(result), MAX_PATH_LENGTH - 1, 
              "Result should be truncated to MAX_PATH_LENGTH - 1");
    
    return true;
}

// Test path_join with edge case combinations
bool test_path_join_fuzz_edge_cases() {
    char result[MAX_PATH_LENGTH];
    
    // Test various edge case combinations
    struct {
        const char *base;
        const char *extra;
    } test_cases[] = {
        {"", ""},
        {"/", ""},
        {"", "/"},
        {"/", "/"},
        {"//", "//"},
        {"a", ""},
        {"", "a"},
        {"/a", "/b"},
        {"a/", "b/"},
        {"a/", "/b"},
        {"/a/", "/b/"},
        {"a", "b"},
        {"a", "/b"},
        {"/a", "b"},
        {"a", "b/c"},
        {"a/b", "c"},
        {"a/b", "c/d"},
        {"a", "b/c/d"},
        {".", ".."},
        {"..", "."},
        {".", "."},
        {"..", ".."},
    };
    
    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    
    for (int i = 0; i < num_cases; i++) {
        path_join(result, test_cases[i].base, test_cases[i].extra);
        
        ASSERT_EQ(result[MAX_PATH_LENGTH - 1], '\0', 
                  "Result must be null-terminated");
        ASSERT(strlen(result) < MAX_PATH_LENGTH, 
               "Result length must be within bounds");
    }
    
    return true;
}

// Test path_join with special characters
bool test_path_join_fuzz_special_chars() {
    char base[MAX_PATH_LENGTH];
    char extra[MAX_PATH_LENGTH];
    char result[MAX_PATH_LENGTH];
    
    // Test with various special characters that might appear in paths
    const char *special_cases[] = {
        "path with spaces",
        "path\twith\ttabs",
        "path\nwith\nnewlines",
        "path.with.dots",
        "path-with-dashes",
        "path_with_underscores",
        "path+with+pluses",
        "path@with@ats",
        "path#with#hashes",
        "path$with$dollars",
        "path%with%percents",
        "path&with&ampersands",
        "path*with*asterisks",
        "path(with)parens",
        "path[with]brackets",
        "path{with}braces",
    };
    
    int num_cases = sizeof(special_cases) / sizeof(special_cases[0]);
    
    for (int i = 0; i < num_cases; i++) {
        for (int j = 0; j < num_cases; j++) {
            strncpy(base, special_cases[i], MAX_PATH_LENGTH - 1);
            base[MAX_PATH_LENGTH - 1] = '\0';
            strncpy(extra, special_cases[j], MAX_PATH_LENGTH - 1);
            extra[MAX_PATH_LENGTH - 1] = '\0';
            
            path_join(result, base, extra);
            
            ASSERT_EQ(result[MAX_PATH_LENGTH - 1], '\0', 
                      "Result must be null-terminated");
        }
    }
    
    return true;
}

// Test path_join with repeated operations
bool test_path_join_fuzz_repeated() {
    char result[MAX_PATH_LENGTH];
    char temp[MAX_PATH_LENGTH];
    
    // Start with root
    strcpy(result, "/");
    
    // Repeatedly join paths
    for (int i = 0; i < 100; i++) {
        char segment[20];
        snprintf(segment, sizeof(segment), "dir%d", i);
        
        strncpy(temp, result, MAX_PATH_LENGTH - 1);
        temp[MAX_PATH_LENGTH - 1] = '\0';
        
        path_join(result, temp, segment);
        
        ASSERT_EQ(result[MAX_PATH_LENGTH - 1], '\0', 
                  "Result must be null-terminated after each join");
        ASSERT(strlen(result) < MAX_PATH_LENGTH, 
               "Result length must stay within bounds");
    }
    
    return true;
}

// Test path_join with boundary lengths
bool test_path_join_fuzz_boundary_lengths() {
    char base[MAX_PATH_LENGTH + 10];
    char extra[MAX_PATH_LENGTH + 10];
    char result[MAX_PATH_LENGTH];
    
    // Test lengths around MAX_PATH_LENGTH boundary
    size_t test_lengths[] = {
        MAX_PATH_LENGTH - 10,
        MAX_PATH_LENGTH - 1,
        MAX_PATH_LENGTH,
        MAX_PATH_LENGTH + 1,
        MAX_PATH_LENGTH + 10,
        MAX_PATH_LENGTH * 2,
    };
    
    int num_lengths = sizeof(test_lengths) / sizeof(test_lengths[0]);
    
    for (int i = 0; i < num_lengths; i++) {
        for (int j = 0; j < num_lengths; j++) {
            size_t base_len = test_lengths[i];
            size_t extra_len = test_lengths[j];
            
            if (base_len > sizeof(base) - 1) base_len = sizeof(base) - 1;
            if (extra_len > sizeof(extra) - 1) extra_len = sizeof(extra) - 1;
            
            memset(base, 'a', base_len);
            base[base_len] = '\0';
            
            memset(extra, 'b', extra_len);
            extra[extra_len] = '\0';
            
            path_join(result, base, extra);
            
            ASSERT_EQ(result[MAX_PATH_LENGTH - 1], '\0', 
                      "Result must be null-terminated");
            ASSERT(strlen(result) < MAX_PATH_LENGTH, 
                   "Result must be truncated to MAX_PATH_LENGTH - 1");
        }
    }
    
    return true;
}

// Test path_join with unicode-like sequences (extended ASCII)
bool test_path_join_fuzz_extended_ascii() {
    char base[MAX_PATH_LENGTH];
    char extra[MAX_PATH_LENGTH];
    char result[MAX_PATH_LENGTH];
    
    // Generate paths with extended ASCII characters
    for (int i = 0; i < 50; i++) {
        fuzz_srand(time(NULL) + i * 1000);
        
        // Generate base with some extended ASCII
        size_t base_len = 10 + (fuzz_rand() % 100);
        for (size_t j = 0; j < base_len && j < MAX_PATH_LENGTH - 1; j++) {
            if (j % 5 == 0) {
                base[j] = 128 + (fuzz_rand() % 128); // Extended ASCII
            } else {
                base[j] = 32 + (fuzz_rand() % 95); // Printable ASCII
            }
        }
        base[MIN(base_len, MAX_PATH_LENGTH - 1)] = '\0';
        
        // Generate extra similarly
        size_t extra_len = 10 + (fuzz_rand() % 100);
        for (size_t j = 0; j < extra_len && j < MAX_PATH_LENGTH - 1; j++) {
            if (j % 5 == 0) {
                extra[j] = 128 + (fuzz_rand() % 128);
            } else {
                extra[j] = 32 + (fuzz_rand() % 95);
            }
        }
        extra[MIN(extra_len, MAX_PATH_LENGTH - 1)] = '\0';
        
        path_join(result, base, extra);
        
        ASSERT_EQ(result[MAX_PATH_LENGTH - 1], '\0', 
                  "Result must be null-terminated");
    }
    
    return true;
}

// Test path_join stress test - many rapid operations
bool test_path_join_fuzz_stress() {
    char result[MAX_PATH_LENGTH];
    char base[100];
    char extra[100];
    
    // Run many operations rapidly
    for (int i = 0; i < 10000; i++) {
        fuzz_srand(i);
        
        // Generate short random paths
        size_t base_len = 5 + (fuzz_rand() % 50);
        size_t extra_len = 5 + (fuzz_rand() % 50);
        
        generate_random_path(base, base_len);
        generate_random_path(extra, extra_len);
        
        path_join(result, base, extra);
        
        // Check every 1000 iterations to avoid slowing down too much
        if (i % 1000 == 0) {
            ASSERT_EQ(result[MAX_PATH_LENGTH - 1], '\0', 
                      "Result must be null-terminated");
        }
    }
    
    return true;
}

int main() {
    printf("=== Path Join Fuzzing Tests ===\n");
    printf("Note: Fuzzing tests run many iterations with multiple assertions each.\n");
    printf("The assertion count will be high, but there are only 8 test functions.\n\n");
    
    // Seed with current time for randomness
    fuzz_srand((uint32_t)time(NULL));
    
    RUN_TEST(test_path_join_fuzz_random);
    RUN_TEST(test_path_join_fuzz_long_paths);
    RUN_TEST(test_path_join_fuzz_edge_cases);
    RUN_TEST(test_path_join_fuzz_special_chars);
    RUN_TEST(test_path_join_fuzz_repeated);
    RUN_TEST(test_path_join_fuzz_boundary_lengths);
    RUN_TEST(test_path_join_fuzz_extended_ascii);
    RUN_TEST(test_path_join_fuzz_stress);
    
    PRINT_SUMMARY();
}

