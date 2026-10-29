#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/bin/cupidscript"
EXAMPLES_DIR="$ROOT_DIR/examples"

if [[ ! -x "$BIN" ]]; then
  echo "Error: $BIN not found or not executable. Run 'make' first." >&2
  exit 1
fi

status=0
expected_fail=(
  "examples/throwtrace.cs"
)
while IFS= read -r -d '' file; do
  rel="${file#$ROOT_DIR/}"
  echo "Running $rel"
  if ! timeout 15s "$BIN" "$file"; then
    if printf '%s\n' "${expected_fail[@]}" | grep -qx "$rel"; then
      echo "EXPECTED FAIL: $rel" >&2
    else
      echo "FAILED: $rel" >&2
      status=1
    fi
  fi
done < <(find "$EXAMPLES_DIR" -type f -name "*.cs" -print0 | sort -z)

if [[ $status -ne 0 ]]; then
  exit $status
fi

echo "All examples passed."
