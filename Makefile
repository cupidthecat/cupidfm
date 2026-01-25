CUPID_CFLAGS=-Isrc -Ilib -Icupidarchive -Icupidarchive/src -D_POSIX_C_SOURCE=200809L
CUPID_FLAGS=--std=c2x
CUPIDARCHIVE_LIB?=lib/libcupidarchive.a
CUPIDSCRIPT_LIB?=lib/libcupidscript.a

# We only link against prebuilt static libs stored in ./lib.
# (In some environments the external repos may be read-only.)
CUPIDSCRIPT_INC?=lib/
CUPID_CFLAGS += -I$(CUPIDSCRIPT_INC)
# Link order matters: static archives first, then their dependent shared/system libs.
override CUPID_LIBS := $(CUPIDARCHIVE_LIB) $(CUPIDSCRIPT_LIB) -lncursesw -lmagic -lz -lbz2 -llzma

all: cupidfm

$(CUPIDARCHIVE_LIB):
	@echo "Missing $(CUPIDARCHIVE_LIB). Provide it in ./lib (expected: lib/libcupidarchive.a)." 1>&2
	@exit 1

$(CUPIDSCRIPT_LIB):
	@echo "Missing $(CUPIDSCRIPT_LIB). Provide it in ./lib (expected: lib/libcupidscript.a)." 1>&2
	@exit 1

cupidfm: $(CUPIDARCHIVE_LIB) $(CUPIDSCRIPT_LIB) src/*.c src/*.h lib/cupidconf.c
	$(CC) -o $@ src/*.c lib/cupidconf.c $(CUPID_FLAGS) $(CFLAGS) $(CUPID_CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test:
	$(MAKE) -C tests test

.PHONY: clean test cupidarchive

clean:
	rm -f cupidfm *.o
	$(MAKE) -C tests clean
	@true
