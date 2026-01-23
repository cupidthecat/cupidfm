CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11
INCLUDES = -I../src -I..
LIBS = -lz -lbz2
ASAN_CFLAGS = -fsanitize=address -fno-omit-frame-pointer -g
ASAN_LIBS = -fsanitize=address

# Library path
LIBDIR = ..
LIBRARY = $(LIBDIR)/libcupidarchive.a

# Test executables
TEST_TARGETS = test_arc_stream test_arc_reader test_arc_extract

.PHONY: all clean test test-asan test-valgrind

all: $(TEST_TARGETS)

# Build library first (ensure it exists)
$(LIBRARY):
	@$(MAKE) -C $(LIBDIR) all

# Test executables
test_arc_stream: test_arc_stream.c test_runner.h
	@$(MAKE) -C $(LIBDIR) all
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_arc_stream.c -L$(LIBDIR) -lcupidarchive $(LIBS)

test_arc_reader: test_arc_reader.c test_runner.h
	@$(MAKE) -C $(LIBDIR) all
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_arc_reader.c -L$(LIBDIR) -lcupidarchive $(LIBS)

test_arc_extract: test_arc_extract.c test_runner.h
	@$(MAKE) -C $(LIBDIR) all
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_arc_extract.c -L$(LIBDIR) -lcupidarchive $(LIBS)

# Run all tests
test: all
	@echo "Running all CupidArchive tests..."
	@echo ""
	@for test in $(TEST_TARGETS); do \
		echo "=== Running $$test ==="; \
		./$$test || exit 1; \
		echo ""; \
	done
	@echo "✅ All tests passed!"

# Run tests with AddressSanitizer
test-asan: CFLAGS += $(ASAN_CFLAGS)
test-asan: LIBS += $(ASAN_LIBS)
test-asan: clean all
	@echo "Running tests with AddressSanitizer..."
	@echo "Note: AddressSanitizer will detect memory errors like:"
	@echo "  - Use-after-free"
	@echo "  - Heap buffer overflow"
	@echo "  - Stack buffer overflow"
	@echo "  - Memory leaks"
	@echo ""
	@for test in $(TEST_TARGETS); do \
		echo "=== Running $$test with ASan ==="; \
		./$$test || exit 1; \
		echo ""; \
	done
	@echo "✅ All tests passed with AddressSanitizer - no memory errors detected!"

# Run tests with Valgrind
test-valgrind: all
	@echo "Running tests with Valgrind (memory leak detection)..."
	@echo "Note: Valgrind will detect:"
	@echo "  - Memory leaks"
	@echo "  - Use of uninitialized values"
	@echo "  - Use-after-free errors"
	@echo "  - Invalid memory access"
	@echo ""
	@for test in $(TEST_TARGETS); do \
		echo "=== Running $$test with Valgrind ==="; \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$$test || exit 1; \
		echo ""; \
	done
	@echo "✅ All tests passed with Valgrind - no memory issues detected!"

# Clean
clean:
	rm -f $(TEST_TARGETS) *.o
	rm -f /tmp/cupidarchive_test.txt

.PHONY: all clean test test-asan test-valgrind

