#ifndef PROPERTY_TEST_H
#define PROPERTY_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_RESET   "\x1b[0m"

// Property test state
typedef struct {
    uint32_t seed;
    int tests_run;
    int properties_passed;
    int properties_failed;
    bool verbose;
} PropertyTestState;

static PropertyTestState prop_state = {0};

// Simple PRNG (linear congruential generator)
static uint32_t prop_rand(void) {
    prop_state.seed = prop_state.seed * 1103515245 + 12345;
    return (prop_state.seed / 65536) % 32768;
}

static void prop_srand(uint32_t seed) {
    prop_state.seed = seed;
}

// Generate random integer in range [min, max]
static int prop_random_int(int min, int max) {
    if (min >= max) return min;
    return min + (prop_rand() % (max - min + 1));
}

// Generate random size_t in range [min, max]
static size_t prop_random_size_t(size_t min, size_t max) {
    if (min >= max) return min;
    return min + (prop_rand() % (max - min + 1));
}

// Generate random character (printable ASCII)
static char prop_random_char(void) {
    return 32 + (prop_rand() % 95);
}

// Generate random string (kept for future use)
__attribute__((unused))
static void prop_random_string(char *buf, size_t max_len) {
    size_t len = prop_random_size_t(0, max_len - 1);
    for (size_t i = 0; i < len; i++) {
        buf[i] = prop_random_char();
    }
    buf[len] = '\0';
}

// Generate random path-like string
static void prop_random_path(char *buf, size_t max_len) {
    if (max_len < 2) {
        buf[0] = '\0';
        return;
    }
    
    size_t pos = 0;
    bool start_with_slash = (prop_rand() % 3 == 0);
    
    if (start_with_slash && pos < max_len - 1) {
        buf[pos++] = '/';
    }
    
    int segments = prop_random_int(0, 5);
    for (int s = 0; s < segments && pos < max_len - 10; s++) {
        if (s > 0 && pos < max_len - 1) {
            buf[pos++] = '/';
        }
        
        int seg_len = prop_random_int(1, 15);
        for (int i = 0; i < seg_len && pos < max_len - 1; i++) {
            int r = prop_rand() % 100;
            if (r < 70) {
                buf[pos++] = 'a' + (prop_rand() % 26);
            } else if (r < 85) {
                buf[pos++] = 'A' + (prop_rand() % 26);
            } else if (r < 95) {
                buf[pos++] = '0' + (prop_rand() % 10);
            } else {
                buf[pos++] = '_';
            }
        }
    }
    
    buf[pos] = '\0';
}

// Property test result
typedef struct {
    bool passed;
    int num_tests;
    char *failure_message;
    void *counterexample;
} PropertyResult;

// Run a property with random inputs
#define FORALL(type, var, generator, property_body) \
    do { \
        prop_state.tests_run++; \
        int num_tests = prop_state.verbose ? 1000 : 100; \
        bool all_passed = true; \
        type var; \
        char failure_msg[256] = {0}; \
        \
        for (int i = 0; i < num_tests; i++) { \
            generator; \
            bool result = (property_body); \
            if (!result) { \
                all_passed = false; \
                snprintf(failure_msg, sizeof(failure_msg), \
                        "Property failed at iteration %d", i); \
                break; \
            } \
        } \
        \
        if (all_passed) { \
            prop_state.properties_passed++; \
            if (prop_state.verbose) { \
                printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests); \
            } \
        } else { \
            prop_state.properties_failed++; \
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed: %s\n", failure_msg); \
        } \
    } while(0)

// Property test macros for common types
#define FORALL_INT(var, min, max, property) \
    FORALL(int, var, var = prop_random_int(min, max), property)

#define FORALL_SIZE_T(var, min, max, property) \
    FORALL(size_t, var, var = prop_random_size_t(min, max), property)

#define FORALL_STRING(var, max_len, property) \
    do { \
        prop_state.tests_run++; \
        int num_tests = prop_state.verbose ? 1000 : 100; \
        bool all_passed = true; \
        char var[max_len]; \
        char failure_msg[256] = {0}; \
        \
        for (int i = 0; i < num_tests; i++) { \
            prop_random_string(var, max_len); \
            bool result = (property); \
            if (!result) { \
                all_passed = false; \
                snprintf(failure_msg, sizeof(failure_msg), \
                        "Property failed at iteration %d with input: \"%s\"", i, var); \
                break; \
            } \
        } \
        \
        if (all_passed) { \
            prop_state.properties_passed++; \
            if (prop_state.verbose) { \
                printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests); \
            } \
        } else { \
            prop_state.properties_failed++; \
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed: %s\n", failure_msg); \
        } \
    } while(0)

#define FORALL_PATH(var, max_len, property_expr) \
    do { \
        prop_state.tests_run++; \
        int num_tests = prop_state.verbose ? 1000 : 100; \
        bool all_passed = true; \
        char var[max_len]; \
        char failure_msg[256] = {0}; \
        \
        for (int i = 0; i < num_tests; i++) { \
            prop_random_path(var, max_len); \
            bool result = (property_expr); \
            if (!result) { \
                all_passed = false; \
                snprintf(failure_msg, sizeof(failure_msg), \
                        "Property failed at iteration %d with path: \"%s\"", i, var); \
                break; \
            } \
        } \
        \
        if (all_passed) { \
            prop_state.properties_passed++; \
            if (prop_state.verbose) { \
                printf(ANSI_COLOR_GREEN "✓" ANSI_COLOR_RESET " Property passed (%d tests)\n", num_tests); \
            } \
        } else { \
            prop_state.properties_failed++; \
            printf(ANSI_COLOR_RED "✗" ANSI_COLOR_RESET " Property failed: %s\n", failure_msg); \
        } \
    } while(0)

// Initialize property testing
static void prop_init(uint32_t seed, bool verbose) {
    prop_state.seed = seed;
    prop_state.tests_run = 0;
    prop_state.properties_passed = 0;
    prop_state.properties_failed = 0;
    prop_state.verbose = verbose;
    prop_srand(seed);
}

// Print property test summary
static void prop_print_summary(void) {
    printf("\n" ANSI_COLOR_YELLOW "=== Property Test Summary ===" ANSI_COLOR_RESET "\n");
    printf("Properties tested: %d\n", prop_state.tests_run);
    printf(ANSI_COLOR_GREEN "Passed: %d" ANSI_COLOR_RESET "\n", prop_state.properties_passed);
    if (prop_state.properties_failed > 0) {
        printf(ANSI_COLOR_RED "Failed: %d" ANSI_COLOR_RESET "\n", prop_state.properties_failed);
    }
    printf("\n");
}

#endif // PROPERTY_TEST_H

