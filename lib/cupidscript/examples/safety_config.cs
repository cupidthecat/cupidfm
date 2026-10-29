// Demonstrate per-script safety control configuration

print("=== Safety Control Configuration ===\n");

// Check default limits
print("Initial configuration:");
print("  Timeout:", get_timeout(), "ms");
print("  Instruction limit:", get_instruction_limit());
print("  Instructions executed:", get_instruction_count());

// Heavy processing script - request more resources
print("\n=== Configuring for heavy processing ===");
set_timeout(60000);           // 60 seconds
set_instruction_limit(500000000);  // 500M instructions

print("New configuration:");
print("  Timeout:", get_timeout(), "ms");
print("  Instruction limit:", get_instruction_limit());

// Do some work
print("\n=== Processing ===");
let total = 0;
let i = 0;
for (i = 0; i < 10000000; i = i + 1) {
    total = total + i;
    if (i % 2000000 == 0) {
        let count = get_instruction_count();
        print("Progress:", i, "iterations,", count, "instructions");
    }
}

print("\n=== Results ===");
print("Total:", total);
print("Final instruction count:", get_instruction_count());
print("Script completed within limits!");

// Quick operation mode
print("\n=== Switching to quick operation mode ===");
set_timeout(1000);   // 1 second for fast response
set_instruction_limit(1000000);  // 1M instructions

print("Quick mode config:");
print("  Timeout:", get_timeout(), "ms");
print("  Instruction limit:", get_instruction_limit());

// Reset instruction counter is automatic per run
// but we can check remaining budget
let used = get_instruction_count();
let limit = get_instruction_limit();
print("\nInstructions used:", used, "of", limit);

print("\n=== Demo Complete ===");
