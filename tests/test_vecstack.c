#include "test_runner.h"
#include "../src/vecstack.h"
#include <stdlib.h>
#include <string.h>

// Test VecStack_empty creates empty stack
bool test_vecstack_empty() {
    VecStack stack = VecStack_empty();
    ASSERT_NOT_NULL(stack.v.el, "Stack should allocate memory");
    ASSERT_EQ(Vector_len(stack.v), 0, "New stack should be empty");
    VecStack_bye(&stack);
    return true;
}

// Test VecStack_push adds elements
bool test_vecstack_push() {
    VecStack stack = VecStack_empty();
    
    char *str1 = malloc(10);
    strcpy(str1, "test1");
    VecStack_push(&stack, str1);
    
    ASSERT_EQ(Vector_len(stack.v), 1, "Stack should have 1 element after push");
    
    char *str2 = malloc(10);
    strcpy(str2, "test2");
    VecStack_push(&stack, str2);
    
    ASSERT_EQ(Vector_len(stack.v), 2, "Stack should have 2 elements after second push");
    
    VecStack_bye(&stack);
    return true;
}

// Test VecStack_pop removes and returns elements (LIFO)
bool test_vecstack_pop() {
    VecStack stack = VecStack_empty();
    
    char *str1 = malloc(10);
    char *str2 = malloc(10);
    strcpy(str1, "first");
    strcpy(str2, "second");
    
    VecStack_push(&stack, str1);
    VecStack_push(&stack, str2);
    
    ASSERT_EQ(Vector_len(stack.v), 2, "Stack should have 2 elements");
    
    // Pop should return last pushed element (LIFO)
    void *popped = VecStack_pop(&stack);
    ASSERT_EQ(popped, str2, "Pop should return last pushed element");
    ASSERT_EQ(Vector_len(stack.v), 1, "Stack should have 1 element after pop");
    
    // Pop again
    popped = VecStack_pop(&stack);
    ASSERT_EQ(popped, str1, "Pop should return first element");
    ASSERT_EQ(Vector_len(stack.v), 0, "Stack should be empty after second pop");
    
    // Free the popped elements
    free(str1);
    free(str2);
    
    VecStack_bye(&stack);
    return true;
}

// Test VecStack_pop on empty stack
bool test_vecstack_pop_empty() {
    VecStack stack = VecStack_empty();
    
    void *popped = VecStack_pop(&stack);
    ASSERT_NULL(popped, "Pop on empty stack should return NULL");
    ASSERT_EQ(Vector_len(stack.v), 0, "Stack should remain empty");
    
    VecStack_bye(&stack);
    return true;
}

// Test VecStack_peek returns top without removing
bool test_vecstack_peek() {
    VecStack stack = VecStack_empty();
    
    char *str1 = malloc(10);
    char *str2 = malloc(10);
    strcpy(str1, "first");
    strcpy(str2, "second");
    
    VecStack_push(&stack, str1);
    
    void *peeked = VecStack_peek(&stack);
    ASSERT_EQ(peeked, str1, "Peek should return top element");
    ASSERT_EQ(Vector_len(stack.v), 1, "Peek should not remove element");
    
    VecStack_push(&stack, str2);
    peeked = VecStack_peek(&stack);
    ASSERT_EQ(peeked, str2, "Peek should return new top element");
    ASSERT_EQ(Vector_len(stack.v), 2, "Stack should still have 2 elements");
    
    // VecStack_bye will free both str1 and str2 (they're still in the stack)
    VecStack_bye(&stack);
    return true;
}

// Test VecStack_peek on empty stack
bool test_vecstack_peek_empty() {
    VecStack stack = VecStack_empty();
    
    void *peeked = VecStack_peek(&stack);
    ASSERT_NULL(peeked, "Peek on empty stack should return NULL");
    
    VecStack_bye(&stack);
    return true;
}

// Test multiple push/pop operations
bool test_vecstack_multiple_operations() {
    VecStack stack = VecStack_empty();
    
    // Push multiple elements
    char *elements[5];
    for (int i = 0; i < 5; i++) {
        elements[i] = malloc(10);
        snprintf(elements[i], 10, "elem%d", i);
        VecStack_push(&stack, elements[i]);
    }
    
    ASSERT_EQ(Vector_len(stack.v), 5, "Stack should have 5 elements");
    
    // Pop all elements (should be in reverse order)
    // Note: VecStack_pop doesn't free elements, just removes them from stack
    for (int i = 4; i >= 0; i--) {
        void *popped = VecStack_pop(&stack);
        ASSERT_EQ(popped, elements[i], "Pop should return elements in reverse order");
        // Free popped elements manually since VecStack_pop doesn't free them
        free(elements[i]);
    }
    
    ASSERT_EQ(Vector_len(stack.v), 0, "Stack should be empty after popping all");
    
    // VecStack_bye should not try to free anything since stack is empty
    VecStack_bye(&stack);
    return true;
}

// Test VecStack_bye cleans up memory
bool test_vecstack_bye() {
    VecStack stack = VecStack_empty();
    
    // Add some elements
    for (int i = 0; i < 3; i++) {
        char *str = malloc(10);
        snprintf(str, 10, "test%d", i);
        VecStack_push(&stack, str);
    }
    
    ASSERT_EQ(Vector_len(stack.v), 3, "Stack should have 3 elements");
    
    // VecStack_bye should free all elements
    VecStack_bye(&stack);
    
    // If we got here without crashing, cleanup worked
    return true;
}

// Test push/pop/peek sequence
bool test_vecstack_sequence() {
    VecStack stack = VecStack_empty();
    
    char *str1 = malloc(10);
    char *str2 = malloc(10);
    strcpy(str1, "one");
    strcpy(str2, "two");
    
    VecStack_push(&stack, str1);
    ASSERT_EQ(VecStack_peek(&stack), str1, "Peek should return first element");
    
    VecStack_push(&stack, str2);
    ASSERT_EQ(VecStack_peek(&stack), str2, "Peek should return second element");
    
    void *popped = VecStack_pop(&stack);
    ASSERT_EQ(popped, str2, "Pop should return second element");
    ASSERT_EQ(VecStack_peek(&stack), str1, "Peek should now return first element");
    
    // Free str2 since it's been popped (no longer in stack)
    free(str2);
    
    popped = VecStack_pop(&stack);
    ASSERT_EQ(popped, str1, "Pop should return first element");
    ASSERT_NULL(VecStack_peek(&stack), "Peek on empty stack should return NULL");
    
    // Free str1 since it's been popped (no longer in stack)
    free(str1);
    
    // VecStack_bye won't free anything since stack is empty (len is 0)
    VecStack_bye(&stack);
    return true;
}

// Test that VecStack doesn't free elements on pop (only on bye)
bool test_vecstack_pop_no_free() {
    VecStack stack = VecStack_empty();
    
    char *str = malloc(10);
    strcpy(str, "test");
    VecStack_push(&stack, str);
    
    void *popped = VecStack_pop(&stack);
    ASSERT_EQ(popped, str, "Pop should return the element");
    
    // Element should still be valid (not freed by pop)
    ASSERT_EQ(strcmp(str, "test"), 0, "Popped element should still be valid");
    
    // We need to free it manually
    free(str);
    VecStack_bye(&stack);
    return true;
}

int main() {
    printf("=== VecStack Tests ===\n\n");
    
    RUN_TEST(test_vecstack_empty);
    RUN_TEST(test_vecstack_push);
    RUN_TEST(test_vecstack_pop);
    RUN_TEST(test_vecstack_pop_empty);
    RUN_TEST(test_vecstack_peek);
    RUN_TEST(test_vecstack_peek_empty);
    RUN_TEST(test_vecstack_multiple_operations);
    RUN_TEST(test_vecstack_bye);
    RUN_TEST(test_vecstack_sequence);
    RUN_TEST(test_vecstack_pop_no_free);
    
    PRINT_SUMMARY();
}

