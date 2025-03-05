#!/bin/sh

CFLAGS='-Wall -Wextra -pedantic -Wshadow -Werror -Wstrict-overflow -fsanitize=address -fsanitize=undefined'
LDFLAGS='-lmagic -lcurses'
cc -o cupidfm src/*.c lib/cupidconf.c --std=c2x $CFLAGS $LDFLAGS -Isrc -Ilib

echo 'COMPILED! PRESS ENTER TO CONTINUE'
read something

./cupidfm 2> log.txt
