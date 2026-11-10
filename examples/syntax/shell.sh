#!/usr/bin/env bash
# TODO: check the return value
name="world"
printf 'Hello, %s\n' "$name"
if [ -f "$HOME/.profile" ]; then
    echo "found"
fi
