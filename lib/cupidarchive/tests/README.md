# CupidArchive Test Suite

This directory contains the test suite for the CupidArchive library.

## Test Structure

The test suite follows the same structure as the main CupidFM test suite:

- **test_runner.h** - Test framework with assertion macros
- **test_arc_stream.c** - Tests for stream abstraction (memory, file descriptor, substream)
- **test_arc_reader.c** - Tests for archive reader API
- **test_arc_extract.c** - Tests for extraction functionality

## Running Tests

### Basic Tests

```bash
cd cupidarchive/tests
make test
```

This will:
1. Build the CupidArchive library (if not already built)
2. Compile all test executables
3. Run all tests and report results

### Individual Test Suites

You can run individual test suites:

```bash
make test_arc_stream
./test_arc_stream

make test_arc_reader
./test_arc_reader

make test_arc_extract
./test_arc_extract
```

## Advanced Testing

### AddressSanitizer (ASan)

Run tests with AddressSanitizer to detect memory errors:

```bash
make test-asan
```

AddressSanitizer will detect:
- Use-after-free errors
- Heap buffer overflows
- Stack buffer overflows
- Memory leaks

### Valgrind

Run tests with Valgrind for detailed memory analysis:

```bash
make test-valgrind
```

Valgrind will detect:
- Memory leaks
- Use of uninitialized values
- Use-after-free errors
- Invalid memory access

## Test Coverage

Current test coverage includes:

### ArcStream Tests
- ✅ Memory stream creation and reading
- ✅ Byte limit enforcement (zip bomb prevention)
- ✅ File descriptor stream
- ✅ Stream seek and tell operations
- ✅ Substream functionality
- ✅ Null pointer handling

### ArcReader Tests
- ✅ Error handling for nonexistent files
- ✅ Entry memory management
- ✅ Null pointer handling

### ArcExtract Tests
- ✅ Error handling for invalid inputs
- ✅ Nonexistent archive/destination handling
- ✅ Null pointer handling

## Adding New Tests

To add a new test:

1. Create a test function returning `bool`:
```c
bool test_my_feature() {
    // Your test code
    ASSERT_EQ(expected, actual, "Test message");
    return true;
}
```

2. Add it to the test's `main()` function:
```c
RUN_TEST(test_my_feature);
```

3. Rebuild and run:
```bash
make test
```

## Test Framework

The test framework provides these assertion macros:

- `ASSERT(condition, message)` - Basic assertion
- `ASSERT_EQ(a, b, message)` - Equality assertion
- `ASSERT_NE(a, b, message)` - Inequality assertion
- `ASSERT_STR_EQ(a, b, message)` - String equality
- `ASSERT_NOT_NULL(ptr, message)` - Pointer not null
- `ASSERT_NULL(ptr, message)` - Pointer is null
- `ASSERT_TRUE(condition, message)` - Boolean true
- `ASSERT_FALSE(condition, message)` - Boolean false

## Future Test Additions

Planned test additions:

- [ ] Integration tests with real TAR files
- [ ] Tests for compressed archives (.tar.gz, .tar.bz2)
- [ ] Tests for TAR format parsing (ustar, pax)
- [ ] Tests for symlink and hardlink handling
- [ ] Tests for large file handling
- [ ] Tests for malformed archive handling
- [ ] Property-based tests for stream operations
- [ ] Fuzzing tests for archive parsing

## Dependencies

The test suite requires:
- GCC compiler
- zlib (for gzip support)
- libbz2 (for bzip2 support)
- AddressSanitizer (optional, for `make test-asan`)
- Valgrind (optional, for `make test-valgrind`)

## License

Same as CupidArchive library - GPL v3.0

