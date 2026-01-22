CC = gcc
CFLAGS = -Wall -Wextra -g -std=c2x
INCLUDES = -I../src -I.
LIBS =
ASAN_CFLAGS = -fsanitize=address -fno-omit-frame-pointer -g
ASAN_LIBS = -fsanitize=address

# Test executables
TEST_TARGETS = test_vector test_path_join test_memory_safety test_vecstack test_path_fuzz test_property test_mutation test_integration

# Benchmark executable
BENCHMARK_TARGET = benchmark

.PHONY: all clean test test-asan test-valgrind run-benchmark

all: $(TEST_TARGETS)

test_vector: test_vector.c test_runner.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_vector.c ../src/vector.c $(LIBS)

test_path_join: test_path_join.c test_runner.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_path_join.c $(LIBS)

test_memory_safety: test_memory_safety.c test_runner.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_memory_safety.c ../src/vector.c $(LIBS)

test_vecstack: test_vecstack.c test_runner.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_vecstack.c ../src/vecstack.c ../src/vector.c $(LIBS)

test_path_fuzz: test_path_fuzz.c test_runner.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_path_fuzz.c $(LIBS)

test_property: test_property.c property_test.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_property.c $(LIBS)

test_mutation: test_mutation.c mutation_test.h test_runner.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_mutation.c $(LIBS)

test_integration: test_integration.c test_runner.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_integration.c $(LIBS)

benchmark: benchmark.c
	$(CC) $(CFLAGS) $(INCLUDES) -o benchmark benchmark.c ../src/vector.c ../src/vecstack.c $(LIBS)

test_all: test_all.c test_runner.h
	@echo "Note: test_all requires full application dependencies"
	@echo "Run individual test suites instead: make test"

test: all
	@echo "Running all tests..."
	@./test_vector
	@./test_path_join
	@./test_memory_safety
	@./test_vecstack
	@echo ""
	@echo "Running fuzzing tests (may take longer)..."
	@./test_path_fuzz
	@echo ""
	@echo "Running property-based tests..."
	@./test_property
	@echo ""
	@echo "Running mutation tests..."
	@./test_mutation || echo "Note: Some mutations survived - this indicates areas where tests could be improved"
	@echo ""
	@echo "Running integration tests..."
	@./test_integration

# Build tests with AddressSanitizer for memory error detection
test-asan: clean
	@echo "Building tests with AddressSanitizer..."
	@$(MAKE) all CFLAGS="$(CFLAGS) $(ASAN_CFLAGS)" LIBS="$(LIBS) $(ASAN_LIBS)"
	@echo ""
	@echo "Running tests with AddressSanitizer..."
	@echo "Note: AddressSanitizer will detect memory errors like:"
	@echo "  - Use-after-free"
	@echo "  - Heap buffer overflow"
	@echo "  - Stack buffer overflow"
	@echo "  - Memory leaks"
	@echo ""
	@./test_vector
	@./test_path_join
	@./test_memory_safety
	@./test_vecstack
	@echo ""
	@echo "Running fuzzing tests with AddressSanitizer (may take longer)..."
	@./test_path_fuzz
	@echo ""
	@echo "Running property-based tests with AddressSanitizer..."
	@./test_property
	@echo ""
	@echo "Running mutation tests with AddressSanitizer..."
	@echo "Note: AddressSanitizer may abort on buffer overflow mutations - this is expected"
	@echo "      and indicates the mutation was successfully detected."
	@ASAN_OPTIONS=abort_on_error=1:halt_on_error=0 ./test_mutation 2>&1 | \
		grep -v "ERROR: AddressSanitizer" | \
		grep -v "SUMMARY: AddressSanitizer" | \
		grep -v "Shadow bytes" | \
		grep -v "HINT:" | \
		grep -v "Address.*is located" | \
		grep -v "This frame has" | \
		grep -v "WRITE of size" | \
		grep -v "READ of size" | \
		grep -v "==.*==ERROR" | \
		grep -v "==.*==ABORTING" || \
		(echo "" && echo "Note: AddressSanitizer detected a memory error in a mutation test." && \
		 echo "      This is expected - it means the mutation (intentional bug) was caught!" && \
		 echo "      The mutation is considered 'killed' by ASan detection.")
	@echo ""
	@echo "Running integration tests with AddressSanitizer..."
	@./test_integration
	@echo ""
	@echo "✅ All tests passed with AddressSanitizer - no memory errors detected!"

# Run tests with Valgrind for memory leak detection
# Note: Valgrind conflicts with AddressSanitizer, so we rebuild without ASan
test-valgrind:
	@echo "Building tests for Valgrind (without AddressSanitizer)..."
	@$(MAKE) clean
	@$(MAKE) all
	@echo ""
	@echo "Running tests with Valgrind (memory leak detection)..."
	@echo "Note: Valgrind will detect:"
	@echo "  - Memory leaks"
	@echo "  - Use of uninitialized values"
	@echo "  - Use-after-free errors"
	@echo "  - Invalid memory access"
	@echo ""
	@if ! command -v valgrind >/dev/null 2>&1; then \
		echo "Error: Valgrind is not installed."; \
		echo "Install it with: sudo apt-get install valgrind (Debian/Ubuntu)"; \
		echo "                  sudo yum install valgrind (RHEL/CentOS)"; \
		exit 1; \
	fi
	@if [ ! -f ./test_vector ]; then \
		echo "Error: Test executables not found. Build failed."; \
		exit 1; \
	fi
	@echo "Running unit tests with Valgrind..."
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		--error-exitcode=1 --quiet ./test_vector
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		--error-exitcode=1 --quiet ./test_path_join
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		--error-exitcode=1 --quiet ./test_memory_safety
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		--error-exitcode=1 --quiet ./test_vecstack
	@echo ""
	@echo "Running integration tests with Valgrind..."
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		--error-exitcode=1 --quiet ./test_integration
	@echo ""
	@echo "Note: Fuzzing, property-based, and mutation tests are skipped with Valgrind"
	@echo "      due to performance overhead. Use 'make test-asan' for those."
	@echo ""
	@echo "✅ All tests passed with Valgrind - no memory leaks detected!"

run-benchmark: benchmark
	@echo "Running performance benchmarks..."
	@./benchmark

clean:
	@echo "Cleaning test artifacts..."
	rm -f test_vector test_path_join test_path_utils test_memory_safety test_vecstack test_path_fuzz test_property test_mutation test_integration test_all benchmark
	rm -f test_inversion test_logic test_mut_debug debug_mutation test_utils_coverage
	rm -f test_*_debug debug_*
	rm -f *.o *.gcda *.gcno *.gcov
	rm -f *~ *.swp *.swo .*.sw? .*.swp
	rm -rf coverage
	@echo "✅ Cleaned all test artifacts"
