CC = clang
CFLAGS ?= -O2 -g
CUPID_FLAGS = -std=c2x -Werror=implicit-function-declaration
CUPID_INCLUDES = -Isrc -Isrc/app -Isrc/core -Isrc/ui -Isrc/fs -Isrc/ds \
 -Ilib/cupidmime/include -Ilib/cupidconf -Ilib/cupidgit -Ilib/cupidimage/src -Ilib/cupidscript/src -Ilib/cupidarchive
GIO_CFLAGS := $(shell pkg-config --cflags gio-2.0)
GIO_LIBS := $(shell pkg-config --libs gio-2.0)
CUPID_CPPFLAGS = -D_POSIX_C_SOURCE=200809L $(CUPID_INCLUDES) $(GIO_CFLAGS)
VENDOR_CFLAGS = $(CPPFLAGS) $(CFLAGS) -std=c11 -fPIC -D_POSIX_C_SOURCE=200809L -Isrc
# Separate output directories keep release and sanitizer builds from sharing objects.
BUILD_KEY := $(shell printf '%s\n' '$(CC)|$(AR)|$(CPPFLAGS)|$(CFLAGS)|$(LDFLAGS)' | sha256sum | cut -c1-12)
BUILD_DIR := build/$(BUILD_KEY)
CUPIDARCHIVE_LIB := $(BUILD_DIR)/cupidarchive/libcupidarchive.a
CUPIDSCRIPT_LIB := $(BUILD_DIR)/cupidscript/libcupidscript.a
CUPIDIMAGE_LIB := $(BUILD_DIR)/cupidimage/libcupidimage.a
CUPIDMIME_LIB := $(BUILD_DIR)/cupidmime/libcupidmime.a
CUPIDGIT_LIB := $(BUILD_DIR)/cupidgit/libcupidgit.a
CUPID_LIBS = $(CUPIDARCHIVE_LIB) $(CUPIDSCRIPT_LIB) $(CUPIDIMAGE_LIB) $(CUPIDGIT_LIB) $(CUPIDMIME_LIB) \
 $(GIO_LIBS) -lssl -lcrypto -lncursesw -lmagic -lz -lbz2 -llzma -lm -pthread
SRC := $(shell find src -name '*.c') lib/cupidconf/cupidconf.c
OBJ := $(patsubst %.c,$(BUILD_DIR)/app/%.o,$(SRC))
LIB_FILES = $(wildcard lib/$(1)/Makefile lib/$(1)/*.c lib/$(1)/*.h) $(shell find lib/$(1)/src -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null)

all: cupidfm
libraries: $(CUPIDARCHIVE_LIB) $(CUPIDSCRIPT_LIB) $(CUPIDIMAGE_LIB) $(CUPIDGIT_LIB) $(CUPIDMIME_LIB)
script-library: $(CUPIDSCRIPT_LIB)
archive-library: $(CUPIDARCHIVE_LIB)
mime-library: $(CUPIDMIME_LIB)

$(CUPIDMIME_LIB): $(call LIB_FILES,cupidmime) $(wildcard lib/cupidmime/include/*.h)
	$(MAKE) -C lib/cupidmime CC="$(CC)" AR="$(AR)" CFLAGS="$(VENDOR_CFLAGS)" BUILD=../../$(BUILD_DIR)/cupidmime ../../$@

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

cupidfm: $(OBJ) $(CUPIDARCHIVE_LIB) $(CUPIDSCRIPT_LIB) $(CUPIDIMAGE_LIB) $(CUPIDGIT_LIB) $(CUPIDMIME_LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test: libraries
	$(MAKE) -C tests test CC=$(CC) CUPIDSCRIPT_LIB="$(abspath $(CUPIDSCRIPT_LIB))" CUPIDARCHIVE_LIB="$(abspath $(CUPIDARCHIVE_LIB))"
test-libs:
	$(MAKE) -C lib/cupidmime test CC=$(CC)
	$(MAKE) -C lib/cupidconf test CC=$(CC)
	$(MAKE) -C lib/cupidgit test CC=$(CC)
	$(MAKE) -C lib/cupidarchive test CC=$(CC)
	$(MAKE) -C lib/cupidscript test CC=$(CC)
	$(MAKE) -C lib/cupidimage test CC=$(CC)

print-archive-lib:
	@printf '%s\n' '$(abspath $(CUPIDARCHIVE_LIB))'
print-image-lib:
	@printf '%s\n' '$(abspath $(CUPIDIMAGE_LIB))'
print-mime-lib:
	@printf '%s\n' '$(abspath $(CUPIDMIME_LIB))'
print-script-lib:
	@printf '%s\n' '$(abspath $(CUPIDSCRIPT_LIB))'
clean:
	rm -rf build
	rm -f cupidfm
	$(MAKE) -C tests clean

-include $(OBJ:.o=.d)
.PHONY: mime-library print-mime-lib all libraries script-library cupidfm test test-libs print-script-lib print-image-lib print-archive-lib archive-library clean
