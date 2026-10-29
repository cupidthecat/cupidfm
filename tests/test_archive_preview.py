#!/usr/bin/env python3
import gzip
import io
from pathlib import Path
import subprocess
import tempfile
import zipfile

DRIVER = Path(__file__).resolve().parent / "test_archive_preview"
with tempfile.TemporaryDirectory(prefix="cupidfm-archive-preview-") as name:
    base = Path(name)
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, "w") as z:
        z.writestr("member.txt", b"contents")
    raw = archive.getvalue()
    cases = []
    local = base / "local.zip"
    local.write_bytes(raw[:raw.index(b"PK\x01\x02")])
    cases.append((local, "Archive Contents:", "member.txt", "0"))
    cases.append((local, "Archive Contents:", "End of archive", "99"))
    broken = base / "broken.zip"
    broken.write_bytes(b"PK\x03\x04")
    cases.append((broken, "Archive Contents:", "Unable to read archive", "0"))
    wrapped = base / "wrapped.gz"
    wrapped.write_bytes(gzip.compress(raw))
    cases.append((wrapped, "Archive Contents:", "member.txt", "0"))
    folder = base / "folder.tar"
    folder.mkdir()
    compressed = folder / "data.gz"
    compressed.write_bytes(gzip.compress(b"contents"))
    cases.append((compressed, "Compressed File Contents:", "?", "0"))
    corrupt = base / "corrupt.gz"
    payload = bytearray(gzip.compress(b"contents" * 1000))
    payload[-8] ^= 1
    corrupt.write_bytes(payload)
    cases.append((corrupt, "Compressed File Contents:", "Unable to read archive", "0"))
    empty_file = base / "empty-file.zip"
    with zipfile.ZipFile(empty_file, "w") as z:
        z.writestr("empty", b"")
    cases.append((empty_file, "Archive Contents:", "0.00 B", "0"))
    empty = base / "empty.zip"
    with zipfile.ZipFile(empty, "w"):
        pass
    cases.append((empty, "Archive Contents:", "Archive is empty", "0"))
    for path, header, expected, start in cases:
        subprocess.run([str(DRIVER), str(path), header, expected, start], check=True, timeout=10)
print("Archive preview format, error, empty-entry, and scrolling checks passed")
