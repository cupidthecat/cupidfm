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
SOURCES = $(SRCDIR)/arc_stream.c $(SRCDIR)/arc_filter.c $(SRCDIR)/arc_tar.c $(SRCDIR)/arc_zip.c $(SRCDIR)/arc_compressed.c $(SRCDIR)/arc_reader.c $(SRCDIR)/arc_extract.c
OBJECTS = $(OBJDIR)/arc_stream.o $(OBJDIR)/arc_filter.o $(OBJDIR)/arc_tar.o $(OBJDIR)/arc_zip.o $(OBJDIR)/arc_compressed.o $(OBJDIR)/arc_reader.o $(OBJDIR)/arc_extract.o

# Library
LIBRARY = libcupidarchive.a

# External libraries
LIBS = -lz -lbz2

# Default target
all: $(LIBRARY)

# Create object directory
$(OBJDIR):
	mkdir -p $(OBJDIR)

# Build static library
$(LIBRARY): $(OBJDIR) $(OBJECTS)
	$(AR) $(ARFLAGS) $(LIBDIR)/$(LIBRARY) $(OBJECTS)
	@echo "Built $(LIBRARY)"

# Compile source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test program
test-extract: $(LIBRARY) tests/test_extract.c
	$(CC) $(CFLAGS) -I. -o test-extract tests/test_extract.c -L. -lcupidarchive -lz -lbz2
	@echo "Built test-extract"

# Test target
test:
	$(MAKE) -C tests test

# Clean
clean:
	rm -rf $(OBJDIR) $(LIBRARY) test-extract
	$(MAKE) -C tests clean

# Install (optional)
install: $(LIBRARY)
	@echo "To install, copy $(LIBRARY) and headers to your system library path"

.PHONY: all clean install test-extract test

