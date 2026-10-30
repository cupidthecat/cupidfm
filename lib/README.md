# Vendored libraries

Each library lives in its own directory, with its upstream source, license, build files, and tests. `sources.json` records the repository, commit, and Git tree hash used for each snapshot. CupidFM builds these sources locally; it does not ship precompiled archives or library executables.

`make -j` builds the application and its libraries. Compiler and linker flags select a separate directory under `build/`, so sanitizer and release builds have separate objects. `make test` runs the application regressions; `make test-libs` runs the bundled library suites.

To update a library, fix and test its source repository first, copy a snapshot of the reviewed commit into that library's directory, and update `sources.json`. Keep the upstream files intact so fixes can be compared with their source. CupidImage is pinned to its upstream main revision; PDF development remains in that repository.

After committing an update, compare the stored Git blob hashes with the upstream tree as well as checking the working files. Git text attributes can normalize line endings during import; resolve any such mismatch upstream before recording the snapshot.

To check committed file names, blob hashes, executable modes, and symlink types, keep a local clone of each library under one directory, then run:

```sh
python3 tools/verify_library_sources.py --upstream-root /path/to/library-clones
```

The clone folder names must match the keys in `lib/sources.json` and contain the pinned commits. The check reads Git objects without fetching or changing either repository. It also rejects generated binaries, bytecode, and other extra tracked files in a snapshot. Use `--ref` to check a different CupidFM commit.

CI verifies the committed library tree hashes without network access:

```sh
python3 tools/verify_library_sources.py
```

The tree hashes cover file names, contents, modes, and symlinks. Record the upstream root tree from `git rev-parse <commit>^{tree}` when updating a pin. The optional clone-based check also verifies that each recorded tree belongs to its pinned upstream commit.
