CUPID_CFLAGS=-Isrc -Isrc/app -Isrc/core -Isrc/ui -Isrc/fs -Isrc/ds -Ilib -Icupidarchive -Icupidarchive/src -D_POSIX_C_SOURCE=200809L
CUPID_FLAGS=--std=c2x
CUPIDARCHIVE_LIB?=lib/libcupidarchive.a
CUPIDSCRIPT_LIB?=lib/libcupidscript.a

SRC := $(shell find src -name '*.c')
HDRS := $(shell find src -name '*.h')

# We only link against prebuilt static libs stored in ./lib.
# (In some environments the external repos may be read-only.)
CUPIDSCRIPT_INC?=lib/
CUPID_CFLAGS += -I$(CUPIDSCRIPT_INC)
# Link order matters: static archives first, then their dependent shared/system libs.
override CUPID_LIBS := $(CUPIDARCHIVE_LIB) $(CUPIDSCRIPT_LIB) -lssl -lcrypto -lncursesw -lmagic -lz -lbz2 -llzma -lm

all: cupidfm

$(CUPIDARCHIVE_LIB):
	@echo "Missing $(CUPIDARCHIVE_LIB). Provide it in ./lib (expected: lib/libcupidarchive.a)." 1>&2
	@exit 1

$(CUPIDSCRIPT_LIB):
	@echo "Missing $(CUPIDSCRIPT_LIB). Provide it in ./lib (expected: lib/libcupidscript.a)." 1>&2
	@exit 1

cupidfm: $(CUPIDARCHIVE_LIB) $(CUPIDSCRIPT_LIB) $(SRC) $(HDRS) lib/cupidconf.c
	$(CC) -o $@ $(SRC) lib/cupidconf.c $(CUPID_FLAGS) $(CFLAGS) $(CUPID_CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test:
	$(MAKE) -C tests test

.PHONY: clean test cupidarchive

clean:
	rm -f cupidfm *.o
	$(MAKE) -C tests clean
	@true
