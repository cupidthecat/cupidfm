"""Optional real-font wrapper checks using Python stdlib and Fontconfig.

No fonts are distributed here. Supply an appropriately licensed installed SFNT font.
The reference writer packages its existing tables; fc-scan independently reads them.
"""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import zlib

source = Path(sys.argv[1]).read_bytes()
flavor, count = struct.unpack_from(">IH", source)
assert flavor in (0x10000, 0x4F54544F)
records = [struct.unpack_from(">4sIII", source, 12 + 16 * i) for i in range(count)]
directory = 12 + 16 * count
pad = lambda n: (n + 3) & ~3

woff = bytearray(44 + 20 * count)
collection = bytearray(20 + 2 * directory)
struct.pack_into(">4sIIII", collection, 0, b"ttcf", 0x10000, 2, 20, 20 + directory)
for face in (20, 20 + directory):
    collection[face:face + directory] = source[:directory]
expanded = directory
for i, (tag, checksum, offset, length) in enumerate(records):
    data = source[offset:offset + length]
    assert len(data) == length
    compressed = zlib.compress(data)
    stored = compressed if len(compressed) < length else data
    struct.pack_into(">4sIIII", woff, 44 + 20 * i, tag, len(woff), len(stored), length, checksum)
    woff += stored + bytes(pad(len(stored)) - len(stored))
    expanded += pad(length)
    for face in (20, 20 + directory):
        struct.pack_into(">I", collection, face + 12 + 16 * i + 8, len(collection))
    collection += data + bytes(pad(length) - length)
struct.pack_into(">4sIIHHIHHIIIII", woff, 0, b"wOFF", flavor, len(woff), count, 0,
                 expanded, 1, 0, 0, 0, 0, 0, 0)

cli = os.environ.get("CUPIDMIME", "./build/cupidmime")
with tempfile.TemporaryDirectory() as temp:
    for name, data, mime, faces in [("native", source, "font/ttf" if flavor == 0x10000 else "font/otf", 1),
                                    ("woff", woff, "font/woff", 1),
                                    ("collection", collection, "font/collection", 2)]:
        path = Path(temp) / (name + ".png")
        path.write_bytes(data)
        reader = subprocess.run(["fc-scan", "--format", "%{family}\n", str(path)],
                                check=True, text=True, capture_output=True)
        assert len(reader.stdout.strip().splitlines()) == faces, reader.stdout
        result = subprocess.run([cli, "-v", str(path)], check=True, text=True, capture_output=True)
        assert f": {mime} [evidence=3 flags=0 " in result.stdout, result.stdout
        print(result.stdout.strip())
print("Independent Fontconfig checks passed")
