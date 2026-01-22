#ifndef MUTATION_TEST_H
#define MUTATION_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define MAX_PATH_LENGTH 1024

// Mutation test state
typedef struct {
    int mutations_run;
    int mutations_killed;
    int mutations_survived;
    bool verbose;
} MutationTestState;

static MutationTestState mut_state = {0};

// Initialize mutation testing
static void mut_init(bool verbose) {
    mut_state.mutations_run = 0;
    mut_state.mutations_killed = 0;
    mut_state.mutations_survived = 0;
    mut_state.verbose = verbose;
}

// Test a mutation
#define TEST_MUTATION(name, mutated_code, test_code) \
    do { \
        mut_state.mutations_run++; \
        bool test_passed = false; \
        \
        /* Run test with mutated code */ \
        { \
            mutated_code; \
            test_code; \
            test_passed = true; \
        } \
        \
        if (test_passed) { \
            /* Mutation survived - tests didn't catch it */ \
            mut_state.mutations_survived++; \
            if (mut_state.verbose) { \
                printf(ANSI_COLOR_RED "✗ SURVIVED" ANSI_COLOR_RESET ": %s\n", name); \
            } \
        } else { \
            /* Mutation killed - tests caught it */ \
            mut_state.mutations_killed++; \
            if (mut_state.verbose) { \
                printf(ANSI_COLOR_GREEN "✓ KILLED" ANSI_COLOR_RESET ": %s\n", name); \
            } \
        } \
    } while(0)

// Test a mutation that should be caught (test should fail)
#define TEST_MUTATION_SHOULD_FAIL(name, mutated_code, test_code) \
    do { \
        mut_state.mutations_run++; \
        bool mut_test_failed = false; \
        \
        /* Run test with mutated code */ \
        { \
            mutated_code; \
            bool mut_test_result = (test_code); \
            if (!mut_test_result) { \
                mut_test_failed = true; \
            } \
        } \
        \
        if (mut_test_failed) { \
            /* Mutation killed - tests caught it */ \
            mut_state.mutations_killed++; \
            if (mut_state.verbose) { \
                printf(ANSI_COLOR_GREEN "✓ KILLED" ANSI_COLOR_RESET ": %s\n", name); \
            } \
        } else { \
            /* Mutation survived - tests didn't catch it */ \
            mut_state.mutations_survived++; \
            if (mut_state.verbose) { \
                printf(ANSI_COLOR_RED "✗ SURVIVED" ANSI_COLOR_RESET ": %s\n", name); \
            } \
        } \
    } while(0)

// Print mutation test summary
static void mut_print_summary(void) {
    printf("\n" ANSI_COLOR_YELLOW "=== Mutation Test Summary ===" ANSI_COLOR_RESET "\n");
    printf("Mutations tested: %d\n", mut_state.mutations_run);
    printf(ANSI_COLOR_GREEN "Killed: %d" ANSI_COLOR_RESET "\n", mut_state.mutations_killed);
    if (mut_state.mutations_survived > 0) {
        printf(ANSI_COLOR_RED "Survived: %d" ANSI_COLOR_RESET "\n", mut_state.mutations_survived);
    }
    
    if (mut_state.mutations_run > 0) {
        double score = (double)mut_state.mutations_killed / mut_state.mutations_run * 100.0;
        printf("Mutation score: %.1f%%\n", score);
        
        if (score == 100.0) {
            printf(ANSI_COLOR_GREEN "Perfect! All mutations were killed." ANSI_COLOR_RESET "\n");
        } else if (score >= 80.0) {
            printf(ANSI_COLOR_YELLOW "Good mutation score, but some mutations survived." ANSI_COLOR_RESET "\n");
        } else {
            printf(ANSI_COLOR_RED "Low mutation score. Consider adding more tests." ANSI_COLOR_RESET "\n");
        }
    }
    printf("\n");
}

#endif // MUTATION_TEST_H
