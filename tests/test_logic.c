#include <stdio.h>
#include <string.h>
#include <stdbool.h>

int main() {
    // Simulate mutation 2 test
    char result[1024];
    // Mutation produces "/home\user" instead of "/home/user"
    strcpy(result, "/home\\user");
    
    // Test code: should be "/home/user" but mutation produces "/home\\user"
    bool test_passed = (strcmp(result, "/home/user") == 0);
    printf("test_passed: %d (should be 0/false)\n", test_passed);
    
    // In macro: mut_test_result = (test_code);
    bool mut_test_result = test_passed;
    printf("mut_test_result: %d\n", mut_test_result);
    
    // In macro: if (!mut_test_result) { mut_test_failed = true; }
    bool mut_test_failed = false;
    if (!mut_test_result) {
        mut_test_failed = true;
    }
    printf("mut_test_failed: %d (should be 1/true to KILL mutation)\n", mut_test_failed);
    
    // If mut_test_failed is true, mutation is KILLED
    if (mut_test_failed) {
        printf("Mutation should be KILLED\n");
    } else {
        printf("Mutation SURVIVED (this is wrong!)\n");
    }
    
    return 0;
}
