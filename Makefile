CUPID_LIBS=-Isrc -Ilib -lncursesw -lmagic
CUPID_FLAGS=--std=c2x

all: clean cupidfm

cupidfm: src/*.c src/*.h lib/cupidconf.c
	$(CC) -o $@ src/*.c lib/cupidconf.c $(CUPID_FLAGS) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

.PHONY: clean

clean:
	rm -f cupidfm *.o



