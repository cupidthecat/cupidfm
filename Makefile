CUPID_CFLAGS=-Isrc -Ilib -Icupidarchive -Icupidarchive/src
CUPID_LIBS=-Lcupidarchive -lncursesw -lmagic -lcupidarchive -lz -lbz2 -llzma
CUPID_FLAGS=--std=c2x
CUPIDARCHIVE_LIB=cupidarchive/libcupidarchive.a

all: cupidarchive cupidfm

cupidarchive: $(CUPIDARCHIVE_LIB)

$(CUPIDARCHIVE_LIB):
	$(MAKE) -C cupidarchive

cupidfm: $(CUPIDARCHIVE_LIB) src/*.c src/*.h lib/cupidconf.c
	$(CC) -o $@ src/*.c lib/cupidconf.c $(CUPID_FLAGS) $(CFLAGS) $(CUPID_CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test:
	$(MAKE) -C tests test

.PHONY: clean test cupidarchive

clean:
	rm -f cupidfm *.o
	$(MAKE) -C tests clean
	$(MAKE) -C cupidarchive clean


