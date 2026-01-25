CC      := gcc

# Compiler flags
CFLAGS  := -std=c99 -Wall -Wextra -O2 -g -D_POSIX_C_SOURCE=200809L
CFLAGS  += -Isrc -Ilib -MMD -MP

# Directories
SRCDIR  := src
OBJDIR  := obj
BINDIR  := bin

# System / external libraries
LDLIBS  := -lncurses -ltinfo -lmagic -lz -lbz2 -llzma -pthread -lm

# CupidFM sources
SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

# Extra implementation living in lib/
LIBSRCS := lib/cupidconf.c
LIBOBJS := $(patsubst lib/%.c,$(OBJDIR)/lib/%.o,$(LIBSRCS))

# Dependency files
DEPS := $(OBJS:.o=.d) $(LIBOBJS:.o=.d)

# Static libraries (already built)
STATIC_LIBS := lib/libcupidscript.a lib/libcupidarchive.a

# Output binary
BIN := $(BINDIR)/cupidfm

.PHONY: all clean

all: $(BIN)

# Create output directories
$(OBJDIR) $(OBJDIR)/lib $(BINDIR):
	@mkdir -p $@

# Compile src/*.c
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile lib/*.c (for cupidconf.c)
$(OBJDIR)/lib/%.o: lib/%.c | $(OBJDIR)/lib
	$(CC) $(CFLAGS) -c $< -o $@

# Link final binary
$(BIN): $(OBJS) $(LIBOBJS) $(STATIC_LIBS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBOBJS) $(STATIC_LIBS) $(LDLIBS)

clean:
	rm -rf $(OBJDIR) $(BINDIR)

# Auto dependency includes
-include $(DEPS)
