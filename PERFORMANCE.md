# Preview performance

Text previews read physical lines until the visible range is filled. Earlier lines are scanned for syntax state and discarded. The reader retains one line buffer instead of allocating a copy of every line in the file. Delimiters outside the visible columns still affect the following line.

## Focused measurement

A generated 32 MiB input with short text lines was used to compare the first-screen reader with the full-file allocation loop from commit `b18c2fd13439893cc432e0158ab6142e572b8e5b`. Both harnesses were compiled with `-O2`; each ran five times on the same WSL host.

| Reader | Bytes consumed | Median elapsed time |
| --- | ---: | ---: |
| Full-file allocation loop | 33,554,432 | 136.424 ms |
| Visible-range reader | 8,192 | 0.032 ms |

These figures measure the text reader, not total application startup, MIME detection, disk throughput, or frame rate. The generated input was supplied through a buffered stream. The new path drew the viewport; the comparison loop read, allocated, and freed the full input without drawing it. The read budget is the regression criterion; elapsed time varies by host.

## Reproduce the current reader

```sh
make -B -C tests test_text_preview CFLAGS='-O2 -g -std=c2x -Wall -Wextra'
./tests/test_text_preview
./tests/test_text_preview --benchmark
```

The benchmark emits JSON with input size, bytes consumed, and elapsed seconds. The regression test checks that a first-screen preview consumes less than 64 KiB of a 32 MiB stream. It also checks physical lines longer than 255 bytes, scrolled comments, quoted delimiters, comments beyond the visible columns, CRLF, EOF, and read failures.

Deep scrolling still scans the prefix on each redraw. A single very long physical line can grow the line buffer to that line's size. Image decoding, recursive directory sizes, Git content hashing, and archive previews have separate costs; this measurement does not qualify their performance.
