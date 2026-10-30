#!/usr/bin/env python3
"""Check that nested vendored source/header edits invalidate the library target."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
library = Path(subprocess.check_output(["make", "--no-print-directory", "-s", "print-image-lib"], cwd=root, text=True).strip())
if not library.is_file():
    raise SystemExit("Build CupidFM before checking library dependencies")
relative = str(library.relative_to(root))
base = root / "lib/cupidimage/src"
for extension in (".c", ".h"):
    source = next(path for path in sorted(base.rglob("*" + extension)) if path.parent != base)
    saved = source.stat()
    try:
        os.utime(source, ns=(saved.st_atime_ns, library.stat().st_mtime_ns + 1000000000))
        result = subprocess.run(["make", "--no-print-directory", "-q", relative], cwd=root,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if result.returncode != 1:
            raise AssertionError((source, result.returncode, result.stdout, result.stderr))
    finally:
        os.utime(source, ns=(saved.st_atime_ns, saved.st_mtime_ns))
print("Nested CupidImage source and header edits invalidate the built library")
