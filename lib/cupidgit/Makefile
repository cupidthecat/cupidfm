# cupidgit Makefile

CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11 -fPIC -Isrc
AR = ar
ARFLAGS = rcs

# Directories
SRCDIR = src
OBJDIR = obj
LIBDIR = .

# Source files (will expand as we add modules)
SOURCES = $(SRCDIR)/git_repo.c $(SRCDIR)/git_hash.c $(SRCDIR)/git_index.c $(SRCDIR)/git_status.c $(SRCDIR)/sha1.c
OBJECTS = $(OBJDIR)/git_repo.o $(OBJDIR)/git_hash.o $(OBJDIR)/git_index.o $(OBJDIR)/git_status.o $(OBJDIR)/sha1.o

# Library
LIBRARY = libcupidgit.a

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

# Clean
clean:
	rm -rf $(OBJDIR) $(LIBRARY) tests/status_driver

.PHONY: all clean

-include $(OBJECTS:.o=.d)

tests/status_driver: tests/status_driver.c $(LIBRARY) cupidgit.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. $< $(LIBRARY) $(LDFLAGS) $(LDLIBS) -o $@

test: tests/status_driver
	python3 tests/test_status.py

.PHONY: test
