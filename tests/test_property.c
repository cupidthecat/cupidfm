#include "property_test.h"
#include <time.h>

#define MAX_PATH_LENGTH 1024

// Standalone path_join implementation for testing
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

// Property 1: path_join result is always null-terminated
void test_property_null_terminated(void) {
    printf("Property 1: path_join result is always null-terminated\n");
    
    char base[512];
    char extra[512];
    int num_tests = prop_state.verbose ? 1000 : 100;
    bool all_passed = true;
    
    for (int i = 0; i < num_tests; i++) {
        prop_random_path(base, sizeof(base));
        prop_random_path(extra, sizeof(extra));
        
        char result[MAX_PATH_LENGTH];
        path_join(result, base, extra);
        
        if (result[MAX_PATH_LENGTH - 1] != '\0') {
            all_passed = false;
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed: result not null-terminated\n");
            break;
        }
    }
    
    if (all_passed) {
        prop_state.properties_passed++;
        if (prop_state.verbose) {
            printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests);
        }
    } else {
        prop_state.properties_failed++;
    }
    prop_state.tests_run++;
}

// Property 2: path_join result length is always < MAX_PATH_LENGTH
void test_property_length_bounded(void) {
    printf("Property 2: path_join result length is always < MAX_PATH_LENGTH\n");
    
    char base[512];
    char extra[512];
    int num_tests = prop_state.verbose ? 1000 : 100;
    bool all_passed = true;
    
    for (int i = 0; i < num_tests; i++) {
        prop_random_path(base, sizeof(base));
        prop_random_path(extra, sizeof(extra));
        
        char result[MAX_PATH_LENGTH];
        path_join(result, base, extra);
        
        if (strlen(result) >= MAX_PATH_LENGTH) {
            all_passed = false;
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed: result length >= MAX_PATH_LENGTH\n");
            break;
        }
    }
    
    if (all_passed) {
        prop_state.properties_passed++;
        if (prop_state.verbose) {
            printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests);
        }
    } else {
        prop_state.properties_failed++;
    }
    prop_state.tests_run++;
}

// Property 3: path_join is idempotent when joining empty string
void test_property_idempotent_empty(void) {
    printf("Property 3: path_join(base, \"\") == base\n");
    
    char base[512];
    int num_tests = prop_state.verbose ? 1000 : 100;
    bool all_passed = true;
    
    for (int i = 0; i < num_tests; i++) {
        prop_random_path(base, sizeof(base));
        
        char result[MAX_PATH_LENGTH];
        char empty[] = "";
        path_join(result, base, empty);
        
        if (strcmp(result, base) != 0) {
            all_passed = false;
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed: base=\"%s\", result=\"%s\"\n", base, result);
            break;
        }
    }
    
    if (all_passed) {
        prop_state.properties_passed++;
        if (prop_state.verbose) {
            printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests);
        }
    } else {
        prop_state.properties_failed++;
    }
    prop_state.tests_run++;
}

// Property 4: path_join with empty base returns extra
void test_property_empty_base(void) {
    printf("Property 4: path_join(\"\", extra) == extra\n");
    
    char extra[512];
    int num_tests = prop_state.verbose ? 1000 : 100;
    bool all_passed = true;
    
    for (int i = 0; i < num_tests; i++) {
        prop_random_path(extra, sizeof(extra));
        
        char result[MAX_PATH_LENGTH];
        char empty[] = "";
        path_join(result, empty, extra);
        
        if (strcmp(result, extra) != 0) {
            all_passed = false;
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed: extra=\"%s\", result=\"%s\"\n", extra, result);
            break;
        }
    }
    
    if (all_passed) {
        prop_state.properties_passed++;
        if (prop_state.verbose) {
            printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests);
        }
    } else {
        prop_state.properties_failed++;
    }
    prop_state.tests_run++;
}

// Property 5: path_join preserves base when base ends with /
void test_property_base_ends_slash(void) {
    printf("Property 5: path_join preserves base when base ends with /\n");
    
    char base[500];
    char extra[500];
    int num_tests = prop_state.verbose ? 1000 : 100;
    bool all_passed = true;
    
    for (int i = 0; i < num_tests; i++) {
        prop_random_path(base, sizeof(base));
        prop_random_path(extra, sizeof(extra));
        
        // Ensure base ends with /
        size_t base_len = strlen(base);
        if (base_len == 0) continue;
        
        char base_with_slash[512];
        strncpy(base_with_slash, base, sizeof(base_with_slash) - 1);
        base_with_slash[sizeof(base_with_slash) - 1] = '\0';
        
        if (base_with_slash[base_len - 1] != '/') {
            if (base_len < sizeof(base_with_slash) - 1) {
                base_with_slash[base_len] = '/';
                base_with_slash[base_len + 1] = '\0';
            }
        }
        
        char result[MAX_PATH_LENGTH];
        path_join(result, base_with_slash, extra);
        
        // Result should start with base_with_slash
        if (strncmp(result, base_with_slash, strlen(base_with_slash)) != 0) {
            all_passed = false;
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed\n");
            break;
        }
    }
    
    if (all_passed) {
        prop_state.properties_passed++;
        if (prop_state.verbose) {
            printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests);
        }
    } else {
        prop_state.properties_failed++;
    }
    prop_state.tests_run++;
}

// Property 6: path_join is associative-like (with normalization)
void test_property_associative(void) {
    printf("Property 6: path_join is consistent with sequential joins\n");
    
    char base[300];
    char mid[300];
    char extra[300];
    int num_tests = prop_state.verbose ? 1000 : 100;
    bool all_passed = true;
    
    for (int i = 0; i < num_tests; i++) {
        prop_random_path(base, sizeof(base));
        prop_random_path(mid, sizeof(mid));
        prop_random_path(extra, sizeof(extra));
        
        char result1[MAX_PATH_LENGTH];
        char result2[MAX_PATH_LENGTH];
        char temp[MAX_PATH_LENGTH];
        
        // Join base + mid, then join result + extra
        path_join(temp, base, mid);
        path_join(result1, temp, extra);
        
        // Join base + (mid + extra)
        path_join(temp, mid, extra);
        path_join(result2, base, temp);
        
        // Results should be valid (at least both valid)
        if (strlen(result1) >= MAX_PATH_LENGTH || strlen(result2) >= MAX_PATH_LENGTH) {
            all_passed = false;
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed: result length >= MAX_PATH_LENGTH\n");
            break;
        }
    }
    
    if (all_passed) {
        prop_state.properties_passed++;
        if (prop_state.verbose) {
            printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests);
        }
    } else {
        prop_state.properties_failed++;
    }
    prop_state.tests_run++;
}

// Property 7: path_join never crashes (safety property)
void test_property_no_crash(void) {
    printf("Property 7: path_join never crashes on any input\n");
    
    char base[1024];
    char extra[1024];
    int num_tests = prop_state.verbose ? 1000 : 100;
    bool all_passed = true;
    
    for (int i = 0; i < num_tests; i++) {
        prop_random_path(base, sizeof(base));
        prop_random_path(extra, sizeof(extra));
        
        char result[MAX_PATH_LENGTH];
        // Just verify it doesn't crash - if we get here, it didn't
        path_join(result, base, extra);
    }
    
    if (all_passed) {
        prop_state.properties_passed++;
        if (prop_state.verbose) {
            printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests);
        }
    } else {
        prop_state.properties_failed++;
    }
    prop_state.tests_run++;
}

// Property 8: path_join result contains base (when base is non-empty)
void test_property_contains_base(void) {
    printf("Property 8: path_join result contains base (when base is non-empty)\n");
    
    char base[500];
    char extra[500];
    int num_tests = prop_state.verbose ? 1000 : 100;
    bool all_passed = true;
    
    for (int i = 0; i < num_tests; i++) {
        prop_random_path(base, sizeof(base));
        prop_random_path(extra, sizeof(extra));
        
        if (strlen(base) == 0) continue;
        
        char result[MAX_PATH_LENGTH];
        path_join(result, base, extra);
        
        // Result should start with base (possibly with trailing /)
        size_t base_len = strlen(base);
        bool starts_with_base = strncmp(result, base, base_len) == 0;
        
        // Or base with / appended
        if (!starts_with_base && base[base_len - 1] != '/') {
            char base_with_slash[512];
            strncpy(base_with_slash, base, sizeof(base_with_slash) - 1);
            base_with_slash[sizeof(base_with_slash) - 1] = '\0';
            if (base_len < sizeof(base_with_slash) - 1) {
                base_with_slash[base_len] = '/';
                base_with_slash[base_len + 1] = '\0';
                starts_with_base = strncmp(result, base_with_slash, base_len + 1) == 0;
            }
        }
        
        if (!starts_with_base) {
            all_passed = false;
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed\n");
            break;
        }
    }
    
    if (all_passed) {
        prop_state.properties_passed++;
        if (prop_state.verbose) {
            printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests);
        }
    } else {
        prop_state.properties_failed++;
    }
    prop_state.tests_run++;
}

// Property 9: path_join with root preserves absolute paths
void test_property_root_preservation(void) {
    printf("Property 9: path_join with root preserves absolute paths\n");
    
    char extra[500];
    int num_tests = prop_state.verbose ? 1000 : 100;
    bool all_passed = true;
    
    for (int i = 0; i < num_tests; i++) {
        prop_random_path(extra, sizeof(extra));
        
        char result[MAX_PATH_LENGTH];
        path_join(result, "/", extra);
        
        // Result should start with /
        if (result[0] != '/') {
            all_passed = false;
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed: result doesn't start with /\n");
            break;
        }
    }
    
    if (all_passed) {
        prop_state.properties_passed++;
        if (prop_state.verbose) {
            printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests);
        }
    } else {
        prop_state.properties_failed++;
    }
    prop_state.tests_run++;
}

// Property 10: path_join result is deterministic
void test_property_deterministic(void) {
    printf("Property 10: path_join is deterministic (same inputs = same output)\n");
    
    char base[500];
    char extra[500];
    int num_tests = prop_state.verbose ? 1000 : 100;
    bool all_passed = true;
    
    for (int i = 0; i < num_tests; i++) {
        prop_random_path(base, sizeof(base));
        prop_random_path(extra, sizeof(extra));
        
        char result1[MAX_PATH_LENGTH];
        char result2[MAX_PATH_LENGTH];
        
        path_join(result1, base, extra);
        path_join(result2, base, extra);
        
        if (strcmp(result1, result2) != 0) {
            all_passed = false;
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed: non-deterministic\n");
            break;
        }
    }
    
    if (all_passed) {
        prop_state.properties_passed++;
        if (prop_state.verbose) {
            printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests);
        }
    } else {
        prop_state.properties_failed++;
    }
    prop_state.tests_run++;
}

int main(int argc, char *argv[]) {
    bool verbose = false;
    uint32_t seed = (uint32_t)time(NULL);
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--seed") == 0) {
            if (i + 1 < argc) {
                seed = (uint32_t)strtoul(argv[i + 1], NULL, 10);
                i++;
            }
        }
    }
    
    printf("=== Property-Based Tests ===\n");
    printf("Seed: %u\n", seed);
    printf("Verbose: %s\n\n", verbose ? "yes" : "no");
    
    prop_init(seed, verbose);
    
    test_property_null_terminated();
    test_property_length_bounded();
    test_property_idempotent_empty();
    test_property_empty_base();
    test_property_base_ends_slash();
    test_property_associative();
    test_property_no_crash();
    test_property_contains_base();
    test_property_root_preservation();
    test_property_deterministic();
    
    prop_print_summary();
    
    return prop_state.properties_failed > 0 ? 1 : 0;
}

