CC ?= cc
CFLAGS ?= -O2 -g
CUPID_FLAGS = -std=c2x
CUPID_INCLUDES = -Isrc -Isrc/app -Isrc/core -Isrc/ui -Isrc/fs -Isrc/ds \
 -Ilib/cupidconf -Ilib/cupidgit -Ilib/cupidimage/src -Ilib/cupidscript/src -Ilib/cupidarchive
CUPID_CPPFLAGS = -D_POSIX_C_SOURCE=200809L $(CUPID_INCLUDES)
VENDOR_CFLAGS = $(CPPFLAGS) $(CFLAGS) -std=c11 -fPIC -D_POSIX_C_SOURCE=200809L -Isrc
# Separate output directories keep release and sanitizer builds from sharing objects.
BUILD_KEY := $(shell printf '%s\n' '$(CC)|$(AR)|$(CPPFLAGS)|$(CFLAGS)|$(LDFLAGS)' | sha256sum | cut -c1-12)
BUILD_DIR := build/$(BUILD_KEY)
CUPIDARCHIVE_LIB := $(BUILD_DIR)/cupidarchive/libcupidarchive.a
CUPIDSCRIPT_LIB := $(BUILD_DIR)/cupidscript/libcupidscript.a
CUPIDIMAGE_LIB := $(BUILD_DIR)/cupidimage/libcupidimage.a
CUPIDGIT_LIB := $(BUILD_DIR)/cupidgit/libcupidgit.a
CUPID_LIBS = $(CUPIDARCHIVE_LIB) $(CUPIDSCRIPT_LIB) $(CUPIDIMAGE_LIB) $(CUPIDGIT_LIB) \
 -lssl -lcrypto -lncursesw -lmagic -lz -lbz2 -llzma -lm -pthread
SRC := $(shell find src -name '*.c') lib/cupidconf/cupidconf.c
OBJ := $(patsubst %.c,$(BUILD_DIR)/app/%.o,$(SRC))
LIB_FILES = $(wildcard lib/$(1)/Makefile lib/$(1)/*.c lib/$(1)/*.h lib/$(1)/src/*.c lib/$(1)/src/*.h)

all: cupidfm
libraries: $(CUPIDARCHIVE_LIB) $(CUPIDSCRIPT_LIB) $(CUPIDIMAGE_LIB) $(CUPIDGIT_LIB)
script-library: $(CUPIDSCRIPT_LIB)

$(CUPIDARCHIVE_LIB): $(call LIB_FILES,cupidarchive)
	$(MAKE) -C lib/cupidarchive CC="$(CC)" AR="$(AR)" CFLAGS="$(VENDOR_CFLAGS)" OBJDIR=../../$(BUILD_DIR)/cupidarchive/obj LIBRARY=../../$@
$(CUPIDGIT_LIB): $(call LIB_FILES,cupidgit)
	$(MAKE) -C lib/cupidgit CC="$(CC)" AR="$(AR)" CFLAGS="$(VENDOR_CFLAGS)" OBJDIR=../../$(BUILD_DIR)/cupidgit/obj LIBRARY=../../$@
$(CUPIDSCRIPT_LIB): $(call LIB_FILES,cupidscript)
	$(MAKE) -C lib/cupidscript lib CC="$(CC)" AR="$(AR)" CFLAGS="$(VENDOR_CFLAGS)" OBJDIR=../../$(BUILD_DIR)/cupidscript/obj BINDIR=../../$(BUILD_DIR)/cupidscript
$(CUPIDIMAGE_LIB): $(call LIB_FILES,cupidimage)
	$(MAKE) -C lib/cupidimage lib CC="$(CC)" AR="$(AR)" CFLAGS="$(VENDOR_CFLAGS)" OBJDIR=../../$(BUILD_DIR)/cupidimage/obj BINDIR=../../$(BUILD_DIR)/cupidimage

$(BUILD_DIR)/app/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CUPID_CPPFLAGS) $(CFLAGS) $(CUPID_FLAGS) -MMD -MP -c $< -o $@

cupidfm: $(OBJ) $(CUPIDARCHIVE_LIB) $(CUPIDSCRIPT_LIB) $(CUPIDIMAGE_LIB) $(CUPIDGIT_LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test: libraries
	$(MAKE) -C tests test CUPIDSCRIPT_LIB="$(abspath $(CUPIDSCRIPT_LIB))"
test-libs:
	$(MAKE) -C lib/cupidconf test
	$(MAKE) -C lib/cupidgit test
	$(MAKE) -C lib/cupidarchive test
	$(MAKE) -C lib/cupidscript test
	$(MAKE) -C lib/cupidimage test

print-script-lib:
	@printf '%s\n' '$(abspath $(CUPIDSCRIPT_LIB))'
clean:
	rm -rf build
	rm -f cupidfm
	$(MAKE) -C tests clean

-include $(OBJ:.o=.d)
.PHONY: all libraries script-library cupidfm test test-libs print-script-lib clean
