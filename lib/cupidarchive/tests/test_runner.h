#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_RESET   "\x1b[0m"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (!(condition)) { \
            printf(ANSI_COLOR_RED "FAIL" ANSI_COLOR_RESET ": %s:%d: %s\n", \
                   __FILE__, __LINE__, message); \
            tests_failed++; \
            return false; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

#define ASSERT_EQ(a, b, message) \
    ASSERT((a) == (b), message)

#define ASSERT_NE(a, b, message) \
    ASSERT((a) != (b), message)

#define ASSERT_STR_EQ(a, b, message) \
    ASSERT(strcmp(a, b) == 0, message)

#define ASSERT_NOT_NULL(ptr, message) \
    ASSERT((ptr) != NULL, message)

#define ASSERT_NULL(ptr, message) \
    ASSERT((ptr) == NULL, message)

#define ASSERT_TRUE(condition, message) \
    ASSERT((condition) == true, message)

#define ASSERT_FALSE(condition, message) \
    ASSERT((condition) == false, message)

#define RUN_TEST(test) \
    do { \
        printf("Running " #test "...\n"); \
        if (test()) { \
            printf(ANSI_COLOR_GREEN "PASS" ANSI_COLOR_RESET ": " #test "\n"); \
        } else { \
            printf(ANSI_COLOR_RED "FAIL" ANSI_COLOR_RESET ": " #test "\n"); \
        } \
        printf("\n"); \
    } while(0)

#define PRINT_SUMMARY() \
    do { \
        printf("\n" ANSI_COLOR_YELLOW "=== Test Summary ===" ANSI_COLOR_RESET "\n"); \
        printf("Total assertions:  %d\n", tests_run); \
        printf(ANSI_COLOR_GREEN "Passed: %d" ANSI_COLOR_RESET "\n", tests_passed); \
        if (tests_failed > 0) { \
            printf(ANSI_COLOR_RED "Failed: %d" ANSI_COLOR_RESET "\n", tests_failed); \
        } \
        printf("\n"); \
        return (tests_failed == 0) ? 0 : 1; \
    } while(0)

#endif // TEST_RUNNER_H

