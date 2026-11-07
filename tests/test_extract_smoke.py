#!/usr/bin/env python3
"""Extract supported archives through the real browser, including failure and cancellation."""
import fcntl
import bz2
import lzma
import gzip
import io
import os
from pathlib import Path
import pty
import struct
import subprocess
import tarfile
import tempfile
import termios
import time

from terminal_wait import wait_for_terminal
import zipfile

binary = Path(os.environ.get("CUPIDFM_TEST_BINARY", Path(__file__).resolve().parents[1] / "cupidfm"))
with tempfile.TemporaryDirectory(prefix="cupidfm-extract-") as temporary:
    root = Path(temporary)
    home = root / "home"; home.mkdir()
    source = root / "files"; source.mkdir()
    target = root / "output"; target.mkdir()
    literal = "a ' $(touch OOPS)\nname.txt"
    with zipfile.ZipFile(source / "bundle.zip", "w") as archive:
        archive.writestr(literal, "literal")
        archive.writestr("nested/data.txt", "nested")
    with tarfile.open(source / "tree.tar.gz", "w:gz") as archive:
        hardlink = tarfile.TarInfo("child/hard")
        hardlink.type = tarfile.LNKTYPE; hardlink.linkname = "child/data.txt"
        archive.addfile(hardlink)
        entry = tarfile.TarInfo("child/data.txt")
        entry.mode = 0o4751
        entry.size = 4
        archive.addfile(entry, io.BytesIO(b"data"))
        link = tarfile.TarInfo("child/link")
        link.type = tarfile.SYMTYPE; link.linkname = "data.txt"
        archive.addfile(link)
    fixture_root = Path(__file__).resolve().parents[1] / "lib/cupidarchive/tests/fixtures"
    (source / "seven.7z").write_bytes((fixture_root / "encoded-header.7z").read_bytes())
    with zipfile.ZipFile(source / "empty.zip", "w"):
        pass
    (source / "plain.gz").write_bytes(gzip.compress(b"plain bytes"))
    with tarfile.open(source / "locked-bad.tar", "w") as archive:
        directory = tarfile.TarInfo("locked")
        directory.type = tarfile.DIRTYPE; directory.mode = 0
        archive.addfile(directory)
        entry = tarfile.TarInfo("locked/kept")
        entry.size = 4; archive.addfile(entry, io.BytesIO(b"kept"))
        entry = tarfile.TarInfo("../escaped")
        entry.size = 4; archive.addfile(entry, io.BytesIO(b"oops"))
    outside = root / "outside"; outside.mkdir(mode=0o751)
    (outside / "protected").write_text("keep")
    with tarfile.open(source / "link-bad.tar", "w") as archive:
        link = tarfile.TarInfo("jump")
        link.type = tarfile.SYMTYPE; link.linkname = str(outside)
        archive.addfile(link)
        entry = tarfile.TarInfo("jump/protected")
        entry.size = 4; archive.addfile(entry, io.BytesIO(b"oops"))
    with tarfile.open(source / "oversized.tar", "w") as archive:
        entry = tarfile.TarInfo("oversized")
        entry.size = 1024 ** 3 + 1
        archive.addfile(entry)
    (source / "not-archive.zip").write_text("not an archive")
    with zipfile.ZipFile(source / "escape.zip", "w") as archive:
        archive.writestr("../escaped.txt", "escaped")
    with zipfile.ZipFile(source / "corrupt.zip", "w", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr("data", b"unique-payload-to-break-crc")
    payload = (source / "corrupt.zip").read_bytes().replace(b"unique-payload", b"broken-payload")
    (source / "corrupt.zip").write_bytes(payload)
    # A streaming gzip keeps the worker busy long enough to exercise running cancellation.
    with gzip.open(source / "slow.gz", "wb", compresslevel=1) as archive:
        chunk = b"x" * (1024 * 1024)
        for unused in range(384):
            archive.write(chunk)
    (home / ".cupidfmrc").write_text("keymap=vim\n")
    plugins = home / ".cupidfm/plugins"; plugins.mkdir(parents=True)
    (plugins / "ready.cs").write_text(r'''
let checks = 0;
fn on_load() { write_file(getenv("EXTRACT_ROOT") + "/ready", "ready"); }
fn on_key(key) {
    if (key == "!") {
        checks = checks + 1;
        write_file(getenv("EXTRACT_ROOT") + "/" + to_str(checks), fm.selected_name());
        return true;
    }
    return false;
}
''')
    primary, secondary = pty.openpty()
    fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
    environment = dict(os.environ, HOME=str(home), TERM="xterm-256color", EXTRACT_ROOT=str(root))
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen([str(binary), str(source)], env=environment, stdin=secondary,
                                   stdout=secondary, stderr=errors, start_new_session=True)
        os.close(secondary); output = bytearray(); checks = 0
        def wait(predicate, timeout=10):
            wait_for_terminal(process, primary, predicate, output, errors, timeout)
        def send(keys):
            os.write(primary, keys)
        def snapshot(keys=b""):
            global checks
            checks += 1
            send(keys + b"!")
            wait((root / str(checks)).exists)
            return (root / str(checks)).read_text()
        def select_archive(name):
            # Filtering keeps the cursor on the requested archive across watcher reloads.
            result = snapshot(b"f\x15^" + name.encode() + b"$\n")
            assert result == name, (name, result)
        def done(path):
            wait(path.exists)
            wait(lambda: not list(source.glob(".cupidfm-extract-*")) and
                         not list(target.glob(".cupidfm-extract-*")))
        try:
            wait((root / "ready").exists)
            select_archive("bundle.zip")
            send(b"o")
            done(source / "bundle" / literal)
            assert (source / "bundle" / literal).read_text() == "literal"
            assert (source / "bundle/nested/data.txt").read_text() == "nested"
            assert not (source / "OOPS").exists()
            # Existing extracted data remains untouched; a second run gets a new folder.
            (source / "bundle" / literal).write_text("keep")
            send(b"e")
            done(source / "bundle_1" / literal)
            assert (source / "bundle" / literal).read_text() == "keep"
            select_archive("tree.tar.gz")
            send(b"E" + b"\x15" + os.fsencode(target) + b"\n")
            done(target / "tree/child/link")
            assert (target / "tree/child/link").is_symlink()
            assert (target / "tree/child/link").read_text() == "data"
            assert (target / "tree/child/data.txt").stat().st_mode & 0o7777 == 0o751
            assert (target / "tree/child/data.txt").stat().st_ino == (target / "tree/child/hard").stat().st_ino
            select_archive("plain.gz")
            send(b"e")
            done(source / "plain/plain")
            assert (source / "plain/plain").read_bytes() == b"plain bytes"
            select_archive("seven.7z")
            send(b"o")
            done(source / "seven/sample.txt")
            assert (source / "seven/sample.txt").read_bytes() == b"hello CupidArchive\n" * 20
            select_archive("empty.zip")
            send(b"o")
            wait((source / "empty").is_dir)
            assert not list((source / "empty").iterdir())
            for name in ("not-archive.zip", "escape.zip", "corrupt.zip", "locked-bad.tar", "link-bad.tar", "oversized.tar"):
                select_archive(name)
                previous = len(output)
                send(b"ew")
                wait(lambda: b"failed" in output[previous:])
                snapshot(b"w")
                assert not (source / name.removesuffix(".zip").removesuffix(".tar")).exists()
                wait(lambda: not list(source.glob(".cupidfm-extract-*")))
            assert not (source / "escaped.txt").exists()
            assert (outside / "protected").read_text() == "keep"
            assert outside.stat().st_mode & 0o777 == 0o751
            select_archive("slow.gz")
            send(b"e")
            def worker_running():
                for thread in Path(f"/proc/{process.pid}/task").iterdir():
                    try:
                        children = (thread / "children").read_text().split()
                    except FileNotFoundError:
                        continue
                    for child in children:
                        try:
                            arguments = Path(f"/proc/{child}/cmdline").read_bytes().split(b"\0")
                        except FileNotFoundError:
                            continue
                        if os.fsencode(source / "slow.gz") in arguments:
                            return True
                return False
            wait(worker_running)
            send(b"wGx")
            wait(lambda: not list(source.glob(".cupidfm-extract-*")))
            snapshot(b"q")
            assert not (source / "slow").exists(), "cancelled staging must not be published"
            # A later extraction still succeeds after cancellation.
            select_archive("bundle.zip")
            send(b"e")
            done(source / "bundle_2" / literal)
            for bad in ("not-archive", "escape", "corrupt", "locked-bad", "link-bad", "oversized"):
                assert not (source / bad).exists(), bad
            settle = time.monotonic() + 0.2
            wait(lambda: time.monotonic() >= settle)
            send(b"q")
            wait(lambda: process.poll() is not None, timeout=5)
            errors.seek(0); error = errors.read()
            assert process.returncode == 0 and b"Sanitizer" not in error and b"runtime error:" not in error, error
        finally:
            if process.poll() is None: process.kill(); process.wait()
            os.close(primary)
    # The original profile gets extraction too; its configured keys retain priority.
    for key, destination in ((b"e", source / "bundle_3"), (b"E", target / "bundle"), (b"help", None)):
        (root / "ready").unlink()
        if destination is None:
            (home / ".cupidfmrc").write_text("keymap=cupidfm\nkey_help=e\n")
        primary, secondary = pty.openpty()
        fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
        with tempfile.TemporaryFile() as errors:
            process = subprocess.Popen([str(binary), "--keymap=cupidfm", str(source / "bundle.zip")],
                                       env=environment, stdin=secondary, stdout=secondary,
                                       stderr=errors, start_new_session=True)
            os.close(secondary)
            try:
                wait((root / "ready").exists)
                if destination is None:
                    offset = len(output)
                    send(b"E")
                    wait(lambda: b"Help Menu" in output[offset:])
                    send(b"\x1b")
                else:
                    send(key + (b"\x15" + os.fsencode(target) + b"\n" if key == b"E" else b""))
                    wait((destination / literal).exists)
                    wait(lambda: not list(destination.parent.glob(".cupidfm-extract-*")))
                    offset = len(output)
                    send(b"wG")
                    wait(lambda: b"done" in output[offset:])
                    send(b"w")
                send(b"\x1bOP")
                wait(lambda: process.poll() is not None, timeout=5)
                assert process.returncode == 0
                errors.seek(0); error = errors.read()
                assert b"Sanitizer" not in error and b"runtime error:" not in error, error
            finally:
                if process.poll() is None: process.kill(); process.wait()
                os.close(primary)
    for extension, compress in (("bz2", bz2.compress), ("xz", lzma.compress)):
        archive = root / ("payload." + extension)
        archive.write_bytes(compress(b"payload"))
        destination = root / ("decoded-" + extension)
        result = subprocess.run([str(binary), "--extract-worker", str(archive), "--into", str(destination)],
                                capture_output=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert (destination / "payload").read_bytes() == b"payload"
    # Nonregular sources fail promptly without opening a terminal or creating output.
    fifo = root / "archive-fifo"; os.mkfifo(fifo)
    result = subprocess.run([str(binary), "--extract-worker", str(fifo), "--into", str(root / "fifo-out")],
                            capture_output=True, timeout=3)
    assert result.returncode == 1 and not (root / "fifo-out").exists()
print("Archive opening, destination choice, literal names, conflicts, traversal/CRC failure, cleanup, cancellation, and recovery passed")
