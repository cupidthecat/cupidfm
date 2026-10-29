# CupidArchive Makefile

CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11 -fPIC -Isrc
AR = ar
ARFLAGS = rcs

# Directories
SRCDIR = src
OBJDIR = obj
LIBDIR = .

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

# Library
LIBRARY = libcupidarchive.a

# External libraries
LIBS = -lz -lbz2 -llzma

# Default target
all: $(LIBRARY)

# Create object directory
$(OBJDIR):
	mkdir -p $(OBJDIR)

# Build static library
$(LIBRARY): $(OBJECTS)
	$(AR) $(ARFLAGS) $(LIBDIR)/$(LIBRARY) $(OBJECTS)
	@echo "Built $(LIBRARY)"

# Compile source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

# Test program
test-extract: $(LIBRARY) tests/test_extract.c
	$(CC) $(CFLAGS) -I. -o test-extract tests/test_extract.c -L. -lcupidarchive $(LIBS)
	@echo "Built test-extract"

# Test target
tests/archive_driver: tests/archive_driver.c $(LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. $< $(LIBRARY) $(LDFLAGS) $(LIBS) -o $@

test: $(LIBRARY) tests/archive_driver
	python3 tests/test_regressions.py
	$(MAKE) -C tests test

# Clean
clean:
	rm -rf $(OBJDIR) $(LIBRARY) test-extract tests/archive_driver
	$(MAKE) -C tests clean

# Install (optional)
install: $(LIBRARY)
	@echo "To install, copy $(LIBRARY) and headers to your system library path"

.PHONY: all clean install test-extract test


-include $(OBJECTS:.o=.d)
