# CupidFM Test Suite

This directory contains unit tests for the CupidFM file manager, focusing on the critical memory safety fixes.

## Running Tests

### Run all tests:
```bash
cd tests
make test
```

### Run individual test suites:
```bash
make test_vector
./test_vector

make test_path_join
./test_path_join

make test_vecstack
./test_vecstack

make test_path_fuzz
./test_path_fuzz

make test_property
./test_property
# Or with verbose output:
./test_property -v
# Or with specific seed:
./test_property -s 12345

make test_integration
./test_integration

make test_memory_safety
./test_memory_safety

make benchmark
./benchmark
```

### Run tests with AddressSanitizer (memory error detection):
```bash
make test-asan
```

AddressSanitizer will detect:
- Use-after-free errors
- Heap buffer overflows
- Stack buffer overflows
- Memory leaks
- Use after return/scope

**Note:** AddressSanitizer adds overhead (~2x slower), so use it for debugging memory issues rather than regular testing.

### Run tests with Valgrind (memory leak detection):
```bash
make test-valgrind
```

Valgrind will detect:
- Memory leaks (definite and possible)
- Use of uninitialized values
- Use-after-free errors
- Invalid memory access
- Double free errors

**Note:** 
- Valgrind adds significant overhead (~10-50x slower), so use it for debugging memory issues
- Valgrind requires installation: `sudo apt-get install valgrind` (Debian/Ubuntu) or `sudo yum install valgrind` (RHEL/CentOS)
- Fuzzing, property-based, and mutation tests are skipped with Valgrind due to performance overhead
- For comprehensive memory checking, use AddressSanitizer (`make test-asan`) which is faster

### Run performance benchmarks:
```bash
make benchmark      # Build the benchmark executable
make run-benchmark  # Run the benchmarks
# Or directly:
./benchmark
```

Benchmarks measure:
- Vector operations (add, access, set_len, capacity, large scale)
- Path join operations (basic and variations)
- VecStack operations (push, pop, peek, large stack)
- String operations (strlen, strncpy, snprintf)
- Directory reading operations (opendir/readdir, file counting, stat/lstat)
- Directory size calculations (non-recursive size calculation)
- **Cold cache performance** - Directory operations with cleared OS page cache (realistic browsing scenario)

**Benchmark Reliability:**
All benchmarks use techniques to prevent compiler optimization:
- Results are accumulated in `volatile` variables to prevent dead store elimination
- Function calls are used to prevent constant/path optimization
- Results are actively used to ensure the compiler doesn't optimize away the work
- Labels accurately reflect what's being measured (e.g., "100k iterations, 100 elements each" not "100k elements")

**Cold Cache Benchmarks:**
The benchmark suite includes cold cache tests that measure performance when the OS page cache is cleared, simulating real-world scenarios where users browse folders they haven't accessed recently. These benchmarks:
- Compare hot cache (data in memory) vs cold cache (data from disk) performance
- Require root access to clear the page cache: `sudo ./benchmark`
- Show the realistic performance difference between cached and uncached directory operations

**Note:** Results may vary based on system load and CPU frequency scaling. Run multiple times and average for more accurate results.

### Clean build artifacts:
```bash
make clean
```

## Test Coverage

**Total: 63 test functions across 8 test suites - All Passing ✅**

**Note:** The test runner counts individual assertions, not just test functions. Fuzzing tests run thousands of iterations (e.g., 1,000 random tests, 10,000 stress tests), each with multiple assertions, so the total assertion count is much higher (~2,700+ assertions).

### Vector Tests (`test_vector.c`) - 10 tests
Tests for the Vector data structure, covering:
- ✅ Vector creation and initialization
- ✅ Capacity growth and realloc safety (Critical Fix #6)
- ✅ Memory leak fixes in Vector_bye() (Critical Fix #1)
- ✅ Realloc safety in Vector_sane_cap() and Vector_min_cap()
- ✅ Vector_set_len_no_free functionality

### Path Join Tests (`test_path_join.c`) - 11 tests
Tests for path manipulation functions:
- ✅ Normal path joining
- ✅ Edge cases (empty paths, root paths)
- ✅ Null termination safety (Critical Fix #9)
- ✅ Buffer overflow protection
- ✅ Empty path handling (Critical Fix #10)

### Memory Safety Tests (`test_memory_safety.c`) - 5 tests
General memory safety tests:
- ✅ strncpy null termination (Critical Fix #9)
- ✅ Realloc failure handling (Critical Fix #6)
- ✅ Empty path handling (Critical Fix #10)
- ✅ Memory leak detection
- ✅ Bounds checking

### VecStack Tests (`test_vecstack.c`) - 10 tests
Tests for the VecStack data structure (stack built on Vector):
- ✅ Stack creation and initialization
- ✅ Push operations
- ✅ Pop operations (LIFO behavior)
- ✅ Peek operations (no removal)
- ✅ Empty stack handling (pop/peek on empty)
- ✅ Multiple operations sequence
- ✅ Memory cleanup (VecStack_bye)
- ✅ Pop doesn't free elements (only VecStack_bye does)
- ✅ Complex push/pop/peek sequences

### Path Fuzzing Tests (`test_path_fuzz.c`) - 8 fuzzing test suites
Comprehensive fuzzing tests for path operations to find edge cases and potential bugs:
- ✅ **Random path generation** - 1000 random path combinations
- ✅ **Very long paths** - Tests with paths exceeding MAX_PATH_LENGTH
- ✅ **Edge case combinations** - Empty paths, root paths, special sequences
- ✅ **Special characters** - Spaces, tabs, newlines, punctuation in paths
- ✅ **Repeated operations** - 100 sequential path joins
- ✅ **Boundary lengths** - Testing around MAX_PATH_LENGTH boundary
- ✅ **Extended ASCII** - Non-ASCII characters in paths
- ✅ **Stress test** - 10,000 rapid path join operations

These fuzzing tests help ensure path_join is robust against:
- Buffer overflows
- Null pointer dereferences
- Integer overflows
- Memory corruption
- Unexpected input combinations

### Property-Based Tests (`test_property.c`) - 10 properties
QuickCheck-style property-based testing that verifies invariants hold for randomly generated inputs:
- ✅ **Null termination property** - Result is always null-terminated
- ✅ **Length bounded property** - Result length is always < MAX_PATH_LENGTH
- ✅ **Idempotent with empty** - path_join(base, "") == base
- ✅ **Empty base property** - path_join("", extra) == extra
- ✅ **Base ending with slash** - Preserves base when base ends with /
- ✅ **Associative property** - Consistent with sequential joins
- ✅ **No crash property** - Never crashes on any input
- ✅ **Contains base property** - Result contains base (when non-empty)
- ✅ **Root preservation** - Joining with root preserves absolute paths
- ✅ **Deterministic property** - Same inputs always produce same output

Each property is tested with 100 random inputs (1000 in verbose mode). Properties define invariants that should always hold true, and the framework generates random inputs to find counterexamples.

### Mutation Tests (`test_mutation.c`) - 8 mutation types, 21 test cases
Mutation testing verifies that our tests catch bugs by introducing intentional bugs (mutations) and checking if tests fail:

**Mutation Types:**
- ✅ **Missing null termination** - Tests if null termination checks catch this (2 test cases)
- ✅ **Wrong separator** - Tests if separator validation catches backslash instead of slash (3 test cases)
- ✅ **Always add separator** - Tests if double-slash detection works (3 test cases)
- ✅ **Wrong empty base handling** - Tests if empty base edge case is covered (3 test cases)
- ✅ **Buffer overflow** - Tests if buffer bounds checking works (1 test case)
- ✅ **Wrong empty extra handling** - Tests if empty extra edge case is covered (3 test cases)
- ✅ **Off-by-one error** - Tests if boundary condition tests catch this (3 test cases)
- ✅ **Reversed arguments** - Tests if argument order validation works (4 test cases)

**Mutation Score:** The mutation score shows what percentage of mutations were "killed" (caught by tests). A low score indicates areas where tests need improvement. This is valuable feedback for test quality.

**Note:** Some mutations may "survive" because:
- The mutation might not actually produce wrong results (e.g., `snprintf` always null-terminates)
- The test logic needs refinement to catch specific mutation patterns
- Additional test cases may be needed for edge cases

The framework now includes 21 test cases across 8 mutation types to thoroughly test bug detection capabilities.

**Note on Surviving Mutations:**
Currently, 2 mutations survive (90.5% mutation score), which is expected and correct:
- **"Buffer overflow (no size check)"** - This mutation survives in normal tests because it truncates the result after overflow, but it **is caught by AddressSanitizer** when running with `make test-asan`.
- **"Off-by-one - length validation"** - This mutation survives because the test uses short strings where the off-by-one error doesn't cause observable failures. The mutation writes to `result[MAX_PATH_LENGTH]` instead of `result[MAX_PATH_LENGTH-1]`, but for short strings, this doesn't affect the test's length check.

A 90.5% mutation score is considered good, and the surviving mutations demonstrate edge cases where tests could be strengthened. When running with AddressSanitizer, buffer overflow mutations are caught at runtime.

### Integration Tests (`test_integration.c`) - 12 tests
End-to-end integration tests for actual file system operations:
- ✅ **Create file** - Test file creation
- ✅ **Delete file** - Test file deletion
- ✅ **Rename file** - Test file renaming with content preservation
- ✅ **Create directory** - Test directory creation
- ✅ **Delete directory** - Test directory deletion
- ✅ **Copy file** - Test file copying with content verification
- ✅ **Move file** - Test file moving (rename across directories)
- ✅ **Special characters** - Test file operations with spaces and special chars in names
- ✅ **Multiple operations** - Test sequence of create/delete/rename operations
- ✅ **Nested directories** - Test operations with nested directory structures
- ✅ **File permissions** - Test file permission handling
- ✅ **Error handling** - Test operations on non-existent files

These tests use temporary directories (`/tmp/cupidfm_test_*`) and perform real file system operations to verify the actual behavior of file operations. All tests clean up after themselves.

## AddressSanitizer Support

The test suite supports AddressSanitizer (ASan) for detecting memory errors at runtime. ASan is a fast memory error detector that can catch:

- **Use-after-free** - Accessing memory after it's been freed
- **Heap buffer overflow** - Writing past the end of heap-allocated buffers
- **Stack buffer overflow** - Writing past the end of stack buffers
- **Use after return** - Using stack memory after function returns
- **Use after scope** - Using stack memory after it goes out of scope
- **Memory leaks** - Allocated memory that's never freed
- **Double free** - Freeing the same memory twice
- **Invalid free** - Freeing memory that wasn't allocated

**Usage:**
```bash
# Run all tests with AddressSanitizer
make test-asan
```

**What AddressSanitizer Detects:**

AddressSanitizer instruments your code at compile time to detect memory errors. When a memory error is detected, ASan will:
1. Print a detailed error report showing:
   - The type of error (e.g., "heap-buffer-overflow")
   - The memory address and size
   - A stack trace showing where the error occurred
   - The allocation/deallocation history
2. Terminate the program immediately (preventing undefined behavior)

**Example Output:**
```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x60200000eff0
READ of size 1 at 0x60200000eff0 thread T0
    #0 0x401234 in test_function test_file.c:42
    #1 0x401567 in main test_file.c:100
```

**Performance:**
- AddressSanitizer adds ~2x runtime overhead
- Memory usage increases by ~3x
- Use for debugging memory issues, not for regular testing

**Note on Mutation Tests:**
When running mutation tests with AddressSanitizer, some mutations (especially buffer overflow tests) may cause ASan to abort the program with an error report. This is **expected and desired behavior** - it means AddressSanitizer successfully detected the intentional bug introduced by the mutation. 

When ASan aborts on a mutation:
- The mutation is effectively "killed" (caught) by ASan
- The error output shows detailed information about the memory error
- The test framework may not complete all mutations, but this is acceptable since ASan caught the bug

This demonstrates that AddressSanitizer is working correctly and can catch real memory errors in your code.

**Requirements:**
- GCC 4.8+ or Clang 3.1+
- No additional packages needed (built into modern compilers)

## Valgrind Support

The test suite supports Valgrind for detecting memory leaks and errors. Valgrind is a powerful tool that provides detailed memory analysis:

**What Valgrind Detects:**
- **Memory leaks** - Allocated memory that's never freed (definite and possible leaks)
- **Use of uninitialized values** - Reading from memory before it's initialized
- **Use-after-free** - Accessing memory after it's been freed
- **Invalid memory access** - Reading/writing outside allocated blocks
- **Double free** - Freeing the same memory twice
- **Invalid free** - Freeing memory that wasn't allocated with malloc

**Usage:**
```bash
# Run tests with Valgrind
make test-valgrind
```

**Valgrind Options Used:**
- `--leak-check=full` - Detailed leak information
- `--show-leak-kinds=all` - Show all types of leaks
- `--track-origins=yes` - Track origins of uninitialized values
- `--error-exitcode=1` - Exit with error code if issues found
- `--quiet` - Suppress normal Valgrind output (only show errors)

**Performance:**
- Valgrind adds significant overhead (~10-50x slower)
- Use for debugging memory issues, not for regular testing
- Fuzzing, property-based, and mutation tests are skipped with Valgrind due to performance

**Comparison with AddressSanitizer:**
- **AddressSanitizer** (`make test-asan`): Faster (~2x overhead), catches most memory errors, good for regular testing
- **Valgrind** (`make test-valgrind`): Slower (~10-50x overhead), more detailed leak reports, better for deep analysis

**Requirements:**
- Valgrind must be installed: `sudo apt-get install valgrind` (Debian/Ubuntu) or `sudo yum install valgrind` (RHEL/CentOS)
- Works on Linux systems (not available on macOS/Windows without special setup)

**Mutation Test Usage:**
```bash
./test_mutation        # Run mutation tests
./test_mutation -v     # Verbose mode (shows which mutations survived)
```

## Test Framework

The test suite uses a simple, lightweight test framework (`test_runner.h`) that:
- Provides color-coded output (green for pass, red for fail)
- Tracks test statistics
- Requires no external dependencies
- Is easy to extend

## Adding New Tests

To add a new test:

1. Create a test function that returns `bool`:
```c
bool test_my_feature() {
    ASSERT(condition, "Error message");
    return true;
}
```

2. Add it to the appropriate test file or create a new one

3. Register it with `RUN_TEST(test_my_feature)`

## Test Assertions

Available assertions:
- `ASSERT(condition, message)` - General assertion
- `ASSERT_EQ(a, b, message)` - Equality check
- `ASSERT_NE(a, b, message)` - Inequality check
- `ASSERT_STR_EQ(a, b, message)` - String equality
- `ASSERT_NOT_NULL(ptr, message)` - Pointer not null
- `ASSERT_NULL(ptr, message)` - Pointer is null

## Integration with CI/CD

These tests can be integrated into CI/CD pipelines. Example:

```yaml
# GitHub Actions example
- name: Run tests
  run: |
    cd tests
    make test
```

## Future Improvements

- [X] Add Valgrind integration for leak detection
- [X] Add AddressSanitizer support
- [X] Add fuzzing tests for path operations
- [X] Add property-based testing (e.g., QuickCheck-style)
- [X] Add mutation testing
- [X] Add integration tests for file operations
- [X] Add performance benchmarks
- [X] Add vecstack tests

