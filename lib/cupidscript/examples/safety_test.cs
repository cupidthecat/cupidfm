// Test safety controls - instruction limit and timeout

print("=== Safety Controls Demo ===\n");

// Check initial limits
print("Initial limits:");
print("  Timeout:", get_timeout(), "ms");
print("  Instruction limit:", get_instruction_limit());
print("");

// Configure reasonable limits for this test
print("Setting safety limits:");
print("  Timeout: 5000 ms (5 seconds)");
print("  Instruction limit: 10,000,000");
set_timeout(5000);
set_instruction_limit(10000000);
print("");

print("=== Testing infinite loop (will be stopped by safety controls) ===\n");

// This would run forever without safety controls
let i = 0;
while (true) {
  i = i + 1;
  if (i % 1000000 == 0) {
    print("Iteration:", i, "- Instructions:", get_instruction_count());
  }
}

print("This line should never execute!");
