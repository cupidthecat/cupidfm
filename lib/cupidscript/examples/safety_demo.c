// Example C program demonstrating safety controls

#include "../src/cupidscript.h"
#include "../src/cs_vm.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    cs_vm* vm = cs_vm_new();
    if (!vm) {
        fprintf(stderr, "Failed to create VM\n");
        return 1;
    }
    
    cs_register_stdlib(vm);
    
    // Example 1: Set instruction limit
    printf("=== Example 1: Instruction Limit ===\n");
    cs_vm_set_instruction_limit(vm, 10000000);  // 10 million instructions
    printf("Instruction limit set to 10,000,000\n\n");
    
    const char* code1 = 
        "let i = 0;\n"
        "while (true) {\n"
        "  i = i + 1;\n"
        "}";
    
    int rc = cs_vm_run_string(vm, code1, "infinite_loop");
    if (rc != 0) {
        printf("Script aborted: %s\n", cs_vm_last_error(vm));
        printf("Instructions executed: %llu\n\n", 
               (unsigned long long)cs_vm_get_instruction_count(vm));
    }
    
    // Example 2: Set timeout
    printf("=== Example 2: Timeout (500ms) ===\n");
    cs_vm_set_instruction_limit(vm, 0);  // Remove instruction limit
    cs_vm_set_timeout(vm, 500);  // 500ms timeout
    printf("Timeout set to 500ms\n\n");
    
    const char* code2 = 
        "let sum = 0;\n"
        "for i in range(100000000) {\n"
        "  sum = sum + i;\n"
        "}";
    
    rc = cs_vm_run_string(vm, code2, "slow_loop");
    if (rc != 0) {
        printf("Script aborted: %s\n", cs_vm_last_error(vm));
        printf("Instructions executed: %llu\n\n",
               (unsigned long long)cs_vm_get_instruction_count(vm));
    }
    
    // Example 3: Safe script runs normally
    printf("=== Example 3: Safe Script ===\n");
    cs_vm_set_instruction_limit(vm, 0);  // Unlimited
    cs_vm_set_timeout(vm, 0);            // Unlimited
    
    const char* code3 = 
        "let result = 0;\n"
        "for i in range(10) {\n"
        "  result = result + i;\n"
        "}\n"
        "print(\"Sum of 0-9:\", result);";
    
    rc = cs_vm_run_string(vm, code3, "safe_script");
    if (rc != 0) {
        printf("Error: %s\n", cs_vm_last_error(vm));
    }
    printf("Instructions executed: %llu\n\n",
           (unsigned long long)cs_vm_get_instruction_count(vm));
    
    cs_vm_free(vm);
    return 0;
}
