#!/bin/sh
set -e

CFLAGS='-Wall -Wextra -pedantic -Wshadow -Werror -Wstrict-overflow -fsanitize=address -fsanitize=undefined'
make CFLAGS="$CFLAGS"

echo 'COMPILED! PRESS ENTER TO CONTINUE'
read something

./cupidfm 2> log.txt
