CUPID_LIBS=-Isrc -lcurses
CUPID_FLAGS=--std=c2x

all: clean cupidfm

cupidfm: src/*.c src/*.h src/data_struct/*.c src/data_struct/*.h
	$(CC) -o $@ src/*.c src/data_struct/*.c $(CUPID_FLAGS) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)


.PHONY: clean

clean:
	rm -f cupidfm *.o



