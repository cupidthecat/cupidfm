#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/bin/cupidscript"
TEST_DIR="$ROOT_DIR/tests"

if [[ ! -x "$BIN" ]]; then
  echo "error: test runner expected executable at $BIN" >&2
  echo "hint: run 'make' first" >&2
  exit 2
fi

mapfile -t tests < <(find "$TEST_DIR" -maxdepth 1 -type f -name "*.cs" -printf "%f\n" | sort)

if [[ ${#tests[@]} -eq 0 ]]; then
  echo "no tests found in $TEST_DIR" >&2
  exit 2
fi

pass=0
fail=0

for t in "${tests[@]}"; do
  # Convention: skip helper files prefixed with '_'
  if [[ "$t" == _* ]]; then
    continue
  fi

  path="$TEST_DIR/$t"
  tmp_out="$(mktemp)"
  expect_fail=0
  if grep -qE '^//\s*EXPECT_FAIL\b' "$path"; then
    expect_fail=1
  fi

  if "$BIN" "$path" >"$tmp_out" 2>&1; then
    if [[ $expect_fail -eq 1 ]]; then
      echo "FAIL $t (expected failure, got success)"
      cat "$tmp_out"
      fail=$((fail + 1))
    else
      echo "PASS $t"
      pass=$((pass + 1))
    fi
  else
    if [[ $expect_fail -eq 1 ]]; then
      echo "PASS $t (expected failure)"
      pass=$((pass + 1))
    else
      echo "FAIL $t"
      cat "$tmp_out"
      fail=$((fail + 1))
    fi
  fi

  rm -f "$tmp_out"
done

if [[ -x "$ROOT_DIR/bin/c_api_tests" ]]; then
  tmp_out="$(mktemp)"
  if "$ROOT_DIR/bin/c_api_tests" >"$tmp_out" 2>&1; then
    echo "PASS c_api_tests"
    pass=$((pass + 1))
  else
    echo "FAIL c_api_tests"
    cat "$tmp_out"
    fail=$((fail + 1))
  fi
  rm -f "$tmp_out"
else
  echo "FAIL c_api_tests (missing $ROOT_DIR/bin/c_api_tests)"
  fail=$((fail + 1))
fi

echo
printf "Summary: %d passed, %d failed\n" "$pass" "$fail"

if [[ $fail -ne 0 ]]; then
  exit 1
fi
