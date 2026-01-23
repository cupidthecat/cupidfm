CUPID_CFLAGS=-Isrc -Ilib -Icupidarchive -Icupidarchive/src
CUPID_LIBS=-Lcupidarchive -lncursesw -lmagic -lcupidarchive -lz -lbz2
CUPID_FLAGS=--std=c2x

all: cupidarchive cupidfm

cupidarchive:
	$(MAKE) -C cupidarchive

cupidfm: cupidarchive src/*.c src/*.h lib/cupidconf.c
	$(CC) -o $@ src/*.c lib/cupidconf.c $(CUPID_FLAGS) $(CFLAGS) $(CUPID_CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test:
	$(MAKE) -C tests test

.PHONY: clean test

clean:
	rm -f cupidfm *.o
	$(MAKE) -C tests clean
	$(MAKE) -C cupidarchive clean



