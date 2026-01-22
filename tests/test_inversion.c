#include <stdio.h>
#include <string.h>
#include <stdbool.h>

int main() {
    char result[1024];
    strcpy(result, "dir1\\dir2"); // Mutation produces backslash
    
    // Current test logic
    bool test_passed = (strchr(result, '\\') == NULL);
    printf("test_passed (no backslash check): %d\n", test_passed);
    printf("!test_passed (what we pass to macro): %d\n", !test_passed);
    
    // What we SHOULD be checking
    bool correct_test = (strchr(result, '\\') != NULL);
    printf("correct_test (has backslash): %d\n", correct_test);
    
    printf("\nIf mutation produces backslash:\n");
    printf("  test_passed = false (correct - mutation detected)\n");
    printf("  !test_passed = true (wrong - this makes macro think test passed!)\n");
    printf("  correct_test = true (correct - mutation detected)\n");
    
    return 0;
}
