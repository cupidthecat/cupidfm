#include "test_runner.h"

// Forward declarations
extern bool test_vector_new();
extern bool test_vector_add_capacity();
extern bool test_vector_set_len_frees_elements();
extern bool test_vector_bye_frees_all();
extern bool test_path_join_normal();
extern bool test_path_join_null_termination();
extern bool test_strncpy_null_termination();
extern bool test_realloc_failure_safety();

int main() {
    printf("=== Running All Tests ===\n\n");
    
    printf("--- Vector Tests ---\n");
    RUN_TEST(test_vector_new);
    RUN_TEST(test_vector_add_capacity);
    RUN_TEST(test_vector_set_len_frees_elements);
    RUN_TEST(test_vector_bye_frees_all);
    
    printf("--- Path Utils Tests ---\n");
    RUN_TEST(test_path_join_normal);
    RUN_TEST(test_path_join_null_termination);
    
    printf("--- Memory Safety Tests ---\n");
    RUN_TEST(test_strncpy_null_termination);
    RUN_TEST(test_realloc_failure_safety);
    
    PRINT_SUMMARY();
}

