#!/bin/sh
set -e

CFLAGS='-Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer'
make CFLAGS="$CFLAGS"

echo 'COMPILED! PRESS ENTER TO CONTINUE'
read something

./cupidfm 2> log.txt
