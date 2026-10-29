# Vendored libraries

Each library lives in its own directory, with its upstream source, license, build files, and tests. `sources.json` records the repository and commit used for each snapshot. CupidFM builds these sources locally; it does not ship precompiled archives or library executables.

`make -j` builds the application and its libraries. Compiler and linker flags select a separate directory under `build/`, so sanitizer and release builds have separate objects. `make test` runs the application regressions; `make test-libs` runs the bundled library suites.

To update a library, fix and test its source repository first, copy a snapshot of the reviewed commit into that library's directory, and update `sources.json`. Keep the upstream files intact so fixes can be compared with their source. CupidImage is pinned to its upstream main revision; PDF development remains in that repository.
