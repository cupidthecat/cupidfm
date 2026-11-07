# Development

Work in Linux or WSL on the Linux filesystem. A normal build uses Clang, `make`, and `ar`. The code is C99 and is also checked with GCC. Runtime code uses only the C standard library. Test shell scripts use ordinary POSIX tools. Clang sanitizers and clang-format are development tools rather than library dependencies.

```sh
make clean
make test
make clean
make test CC=gcc
make sanitize
make fuzz-smoke
make format-check
clang --analyze -std=c99 -Iinclude src/cupidmime.c src/cli.c
clang --analyze -std=c99 -Iinclude src/zip.c
clang --analyze -std=c99 -Iinclude src/audio.c
clang --analyze -std=c99 -Iinclude src/mpeg.c
clang --analyze -std=c99 -Iinclude src/ebml.c
clang --analyze -std=c99 -Iinclude src/font.c
```

Clean when changing compiler flags: make does not track command-line changes. Sanitizer builds use `build/sanitize/` and leave normal library and CLI artifacts in `build/` alone, so `make sanitize` can be followed by an ordinary build or install. The default flags treat warnings as errors. CI tests GCC and Clang independently and runs AddressSanitizer and UndefinedBehaviorSanitizer.

The tests build small byte fixtures in memory. They exercise valid headers, damaged fields, declared extents, truncated prefixes, JSON grammar, invalid UTF-8, bounded recursion, API errors, deterministic random buffers, and the scan limit. Every positive fixture is also inspected at each shorter length using an exact-sized heap allocation, through both complete and prefix APIs. These allocations make sanitizer checks useful for finding reads beyond the supplied buffer.

ZIP tests also use checked-in byte fixtures produced by Python's independent ZIP writer: stored and compressed markers, streaming descriptors, and duplicate names. Python is not needed to run the C suite. Maintainers can regenerate these synthetic fixtures with `python3 tests/generate_zip_fixtures.py`. They are not complete EPUB publications.

`make integration-audio` is an optional test requiring FFmpeg with FLAC, libopus, libvorbis and libmp3lame encoders. It generates short silent streams, saves them under misleading `.png` names, and checks their types and flags. MP3 cases cover MPEG-1/2/2.5 sample rates with no ID3 tag, ID3v2.3 and ID3v2.4. Core C tests construct audio headers, damage fields and CRCs, and exercise exact-allocation prefixes without any encoder dependency.

The MPEG tests use explicit frame lengths for representative rates and versions, then vary padding, bitrate, flags and tag boundaries. They cover extended-header extents, footer contradictions, large synchsafe sizes, metadata-only input and repeated tags. An MP3 payload inside a tag cannot substitute for frames at the declared outer boundary.

EBML tests cover all eight size widths, known and unknown Segment lengths, default-valued profile fields, duplicate/conflicting metadata, track limits and exact-allocation prefixes. `make integration-ebml` optionally uses FFmpeg with libopus, libvpx, FLAC and FFV1 to generate audio/video WebM and Matroska files under misleading names. Core builds and tests do not need these encoders.

Font tests cover SFNT/WOFF directories, readable metrics, shared-table collections, signature/optional-block extents and exact-allocation prefixes. `make integration-fonts` optionally requires Python 3, Fontconfig's `fc-scan`, and an installed source font. It packages existing font tables into WOFF and a collection, checks both with the independent reader, and uses misleading filenames. No fonts are committed or downloaded. The default source is DejaVu Sans; choose another complete font within the scan limit with `make integration-fonts FONT_SOURCE=/path/to/font.otf`.

`make fuzz-smoke` uses Clang's libFuzzer with ASan/UBSan for 10,000 mutations, seeded with synthetic JSON, HTML, gzip, RIFF, BMFF, PNG, EPUB, FLAC, Ogg, MP3, WebM, TrueType, WOFF and shared-table font collection buffers. It checks both APIs and basic result invariants, including that prefixes cannot establish EPUB or audio-only Ogg/WebM/Matroska. The smoke run is intentionally short; it does not establish exhaustive coverage. For a longer local run, build `build/fuzz` and run `./build/fuzz build/corpus -max_total_time=300`. Keep useful regression inputs with a test that explains the failure. A licensed real-world corpus and sustained fuzzing remain open work.

## C formatting

The checked-in `.clang-format` defines the LLVM-based style, with four-space indentation, attached braces, a 100-column limit, right-aligned pointer stars, and expanded functions, conditionals and loops. Use clang-format 18 for reproducible local and CI results:

```sh
make format
make format-check
```

Both targets cover library sources, public and internal headers, and C tests, including generated fixture headers. Run `make format` after regenerating fixtures. `CLANG_FORMAT` can select another executable, but CI uses `clang-format-18`.

For a new rule, document the MIME name, source, evidence level, and fields checked. Add a positive fixture, a near-match, malformed length fields, and truncation cases around each field read. Keep stronger binary evidence ahead of text heuristics. Do not specialize containers from arbitrary embedded strings.

## Installation

```sh
make
make install PREFIX="$HOME/.local"
```

`DESTDIR` supports staging. Installation copies the CLI, public header, and static library. There is no shared-library ABI or package metadata yet.

## Documentation and wiki

Keep the README limited to purpose, build commands, a small API example, and documentation links. Put detailed behavior and limitations in `docs/`. The wiki is a navigation and orientation layer that links to those maintained files. Update the coverage and policy documents whenever behavior changes. Review prose for unsupported claims, filler, and repetitive structure before publishing it.
