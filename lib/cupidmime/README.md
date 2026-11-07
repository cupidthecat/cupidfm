# CupidMIME

CupidMIME detects file types from bytes. It is a C99 library and a small Linux/WSL command-line tool with no external runtime dependencies.

The project is under development. Detection is evidence about a format, not proof that a file is valid or safe to open. See [detection policy](docs/detection.md) for the checks and limits.

## Build and run

```sh
make
make test
./build/cupidmime path/to/file
cat path/to/file | ./build/cupidmime -
make sanitize
```

The default build uses Clang, `make`, and a POSIX shell. GCC is also tested. The library itself uses only standard C headers. C sources follow the checked-in `.clang-format`; run `make format-check` with clang-format 18 to check them.

## Use the library

```c
#include "cupidmime.h"
#include <stdio.h>

cm_result result;
if (cm_detect(bytes, length, &result) == CM_OK)
    printf("%s\n", result.mime);
```

Link `build/libcupidmime.a` and add `include/` to your include path. Detection does not allocate memory, open files, or inspect filenames. Calls are independent and safe to run from multiple threads.

See the [API contract](docs/api.md), [format coverage](docs/formats.md), [ZIP and EPUB policy](docs/zip.md), [standards notes](docs/standards.md), and [development guide](docs/development.md). The [wiki](https://github.com/cupidthecat/cupidMIME/wiki) provides a short entry point; detailed documentation lives in `docs/` alongside the code.
