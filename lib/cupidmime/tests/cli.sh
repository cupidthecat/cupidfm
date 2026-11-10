#!/bin/sh
set -eu
cli=${CUPIDMIME:-./build/cupidmime}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
printf '{"ok":true}\n' > "$tmp/wrong.png"
"$cli" "$tmp/wrong.png" | grep -q 'application/json'
printf 'hello\n' | "$cli" - | grep -q 'text/plain'
"$cli" -v "$tmp/wrong.png" | grep -q 'evidence=3 flags=0'
"$cli" --version | grep -q '^0.1.0$'
if "$cli" "$tmp/missing" > /dev/null 2>&1; then exit 1; fi
if "$cli" > /dev/null 2>&1; then exit 1; else test "$?" -eq 2; fi
if "$cli" --bad > /dev/null 2>&1; then exit 1; else test "$?" -eq 2; fi
"$cli" --help | grep -q '^Usage:'
printf 'CLI checks passed\n'
