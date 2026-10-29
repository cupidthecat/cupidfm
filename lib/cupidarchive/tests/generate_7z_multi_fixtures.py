"""Regenerate multi-file fixtures with the independent 7-Zip command-line writer."""
from pathlib import Path
import os
import subprocess
import tempfile

fixtures = Path(__file__).resolve().parent / "fixtures"
with tempfile.TemporaryDirectory(prefix="7z-fixtures-") as temporary:
    work = Path(temporary)
    (work / "folder").mkdir()
    payloads = {"first.txt": b"first contents\n" * 100,
                "folder/second.txt": b"second contents\n" * 100,
                "caf\u00e9-\U0001f600.txt": b"Unicode name\n", "empty": b""}
    for name, data in payloads.items():
        path = work / name
        path.write_bytes(data)
        path.chmod(0o640)
        os.utime(path, (1700000000, 1700000000))
    for method in ("Copy", "LZMA", "LZMA2"):
        for solid in ("on", "off"):
            name = f"multi-{method.lower()}-{solid}.7z"
            output = work.parent / (work.name + "-" + name)
            subprocess.run(["7z", "a", "-t7z", "-y", "-m0=" + method,
                            "-ms=" + solid, "-mhc=off", "-mf=off", str(output), "."],
                           cwd=work, check=True, stdout=subprocess.DEVNULL)
            subprocess.run(["7z", "t", str(output)], check=True, stdout=subprocess.DEVNULL)
            (fixtures / name).write_bytes(output.read_bytes())
            output.unlink()
print("Generated and independently checked six multi-file 7z fixtures")
