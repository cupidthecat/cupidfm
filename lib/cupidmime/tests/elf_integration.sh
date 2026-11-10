#!/bin/sh
set -eu
compiler=${CC:-clang}
detector=${CUPIDMIME:-./build/cupidmime}
directory=$(mktemp -d)
trap 'rm -rf "$directory"' EXIT HUP INT TERM
printf '%s\n' 'int main(void) { return 0; }' > "$directory/source.c"
"$compiler" -c "$directory/source.c" -o "$directory/object.o"
"$compiler" -m32 -c "$directory/source.c" -o "$directory/object32.o"
"$compiler" -no-pie "$directory/source.c" -o "$directory/executable"
"$compiler" -fPIE -pie "$directory/source.c" -o "$directory/pie"
"$compiler" -fPIC -shared "$directory/source.c" -o "$directory/library.so"
"$detector" "$directory/object32.o" | grep -F ': application/x-object' >/dev/null
"$detector" "$directory/object.o" | grep -F ': application/x-object' >/dev/null
"$detector" "$directory/executable" | grep -F ': application/x-executable' >/dev/null
"$detector" -v "$directory/pie" | grep -F 'flags=8' >/dev/null
"$detector" -v "$directory/library.so" | grep -F 'flags=8' >/dev/null
printf '%s\n' 'Clang-generated ELF object, executable, PIE, and shared-library detection passed'
