# Development notes

Run `make test` for application regressions and `make test-libs` for the bundled library suites. Build the application before running `tests/test_terminal_smoke.py` or `tests/test_scroll_smoke.py`. Hosted checks run the focused tests and the terminal paths with AddressSanitizer and UndefinedBehaviorSanitizer.

## Vector ownership and growth

`Vector_add(vector, additional)` reserves capacity for the current length plus `additional`. It returns a boolean and preserves the vector on allocation failure or size overflow. Check the result before writing new elements, then update the length. Reserving capacity does not change the length.

Search results borrow file attributes from the loaded directory list. They reserve enough slots for all loaded entries before collecting matches; clearing or destroying a search view must keep the attributes owned by the directory list.

`VecStack_push` also returns a boolean. On failure the caller retains its item. Directory loading releases a newly created file attribute when vector growth fails. Navigation allocates the replacement path before releasing its current path.

## Scrolling and preview caches

Movement bursts can share repaints within a 16 ms budget while every key still reaches input handling and plugin hooks. Slow work must not flush the input queue. `app_input_next_key` returns any key held during the repaint decision before reading another terminal key.

Text preview checkpoints store physical-line offsets and comment state. The index has a fixed number of entries and becomes sparser as files grow. It is intended for the UI thread. MIME lookups use a separate synchronized cache and copy results into caller-owned buffers. File metadata and syntax changes invalidate cached state; recent regular-file changes bypass timestamp-only reuse.

See [the performance notes](../PERFORMANCE.md) for measurements, commands, and limits.

## C formatting

Use clang-format 18 with the repository's `.clang-format`. It sets four-space indentation, spaces instead of tabs, a 100-column limit, attached braces, pointer stars beside names, case-sensitive include sorting, and separate definition blocks. CI checks application and test sources. Library sources are formatted and checked in their original repositories before their pinned snapshots are updated here.

```sh
git ls-files -z -- '*.c' '*.h' ':(exclude)lib/**' | xargs -0 -r clang-format-18 -i
```
