CFLAGS='-g -O0 -Wall -Wextra -pedantic -Wshadow -Wstrict-overflow'
LDFLAGS='-lmagic -lcurses'

cc -o cupidfm src/*.c lib/cupidconf.c --std=c2x $CFLAGS $LDFLAGS -Isrc -Ilib
