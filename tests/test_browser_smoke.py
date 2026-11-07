#!/usr/bin/env python3
"""Exercise the Vim keymap, tabs, selection, filters, background copies, and persistent trash."""
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time

binary = Path(os.environ.get("CUPIDFM_TEST_BINARY", Path(__file__).resolve().parents[1] / "cupidfm"))
with tempfile.TemporaryDirectory(prefix="cupidfm-vim-") as temporary:
    root = Path(temporary)
    home = root / "home"; home.mkdir()
    source = root / "source"; source.mkdir()
    target = root / "target"; target.mkdir()
    child = source / "child folder"; child.mkdir()
    (home / ".config").mkdir()
    data = home / ".local/share"; data.mkdir(parents=True)
    (home / ".cupidfmrc").write_text("keymap=vim\n")
    for name in ("file2.txt", "file10.txt", ".hidden"):
        (source / name).write_text(name + "\n")
    plugins = home / ".cupidfm/plugins"; plugins.mkdir(parents=True)
    (plugins / "inspect.cs").write_text(r'''
let checks = 0;
fn on_load() { write_file(getenv("CUPIDFM_RECEIPTS") + "/ready", "ready"); }
fn on_key(key) {
    if (key == "!") {
        checks = checks + 1;
        let snapshot = fm.selected_name() + "\n" + to_str(fm.cursor()) + "\n"
            + to_str(len(fm.entries())) + "\n" + to_str(len(fm.selected_paths())) + "\n" + fm.cwd();
        assert(write_file(getenv("CUPIDFM_RECEIPTS") + "/" + to_str(checks), snapshot), "snapshot");
        return true;
    }
    return false;
}
''')
    primary, secondary = pty.openpty()
    fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", HOME=str(home), XDG_CONFIG_HOME=str(home / ".config"),
               XDG_DATA_HOME=str(data), CUPIDFM_RECEIPTS=str(root))
    # The internal yank must work when no display server is available.
    env.pop("DISPLAY", None); env.pop("WAYLAND_DISPLAY", None)
    output = bytearray()
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen([str(binary), str(source)], env=env, stdin=secondary,
                                   stdout=secondary, stderr=errors, start_new_session=True)
        os.close(secondary)
        def wait_for(predicate, timeout=10):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if predicate():
                    return
                if process.poll() is not None:
                    break
                ready, _, _ = select.select([primary], [], [], 0.02)
                if ready:
                    try: output.extend(os.read(primary, 65536))
                    except OSError: break
            errors.seek(0)
            raise AssertionError((process.poll(), errors.read().decode(errors="replace"),
                                  bytes(output[-3000:])))
        checks = 0
        def snapshot(keys=b""):
            global checks
            checks += 1
            receipt = root / str(checks)
            os.write(primary, keys + b"!")
            wait_for(receipt.exists)
            return receipt.read_text().splitlines()
        try:
            wait_for((root / "ready").exists)
            assert snapshot()[:3] == ["child folder", "0", "3"]
            clicked = snapshot(b"\x1b[<0;20;4M\x1b[<0;20;4m")
            assert clicked[:2] == ["file2.txt", "2"], clicked
            assert snapshot(b"\x1b[<64;20;4M")[:2] == ["child folder", "0"]
            assert snapshot(b"\x1b[<65;20;4M")[:2] == ["file2.txt", "2"]
            snapshot(b"\x1b[<0;20;2M")
            snapshot(b"\x1b[<32;20;4M")
            assert snapshot(b"\x1b[<0;20;4m")[3] == "3"
            snapshot(b"\x1b")
            snapshot(b"gg")

            assert snapshot(b"k")[:2] == ["file2.txt", "2"], "single-step previous wraps"
            assert snapshot(b"j")[:2] == ["child folder", "0"]
            assert snapshot(b"  ")[3] == "2", "individual selections reach plugins"
            snapshot(b"\x1b")
            assert snapshot(b",nG")[0] == "file10.txt", "natural sort includes the full directory"
            assert snapshot(b"\x1b[6~")[0] == "file10.txt", "page movement clamps"
            assert snapshot(b"ggl")[4] == str(child)
            assert snapshot(b"H")[4] == str(source)
            assert snapshot(b"L")[4] == str(child)
            assert snapshot(b"h")[0] == "child folder", "leaving restores the child's hover"
            assert snapshot(b"tt")[4] == str(source)
            assert snapshot(b"trsecond tab\n")[4] == str(source)
            assert snapshot(b"1")[4] == str(source)
            assert snapshot(b"f^file2\n")[:3] == ["file2.txt", "0", "1"]
            assert snapshot(b"\x1b")[2] == "3"
            assert snapshot(b".")[2] == "4"
            assert snapshot(b".")[2] == "3"
            (source / "watch-added.txt").write_text("external change")
            wait_for(lambda: snapshot()[2] == "4")
            (source / "watch-added.txt").unlink()
            wait_for(lambda: snapshot()[2] == "3")
            # A yank must remain usable after changing directories, with no X clipboard.
            snapshot(b",aggjy")
            snapshot(b"g " + str(target).encode() + b"\n")
            os.write(primary, b"p")
            wait_for((target / "file10.txt").exists)
            assert (target / "file10.txt").read_text() == "file10.txt\n"
            wait_for(lambda: snapshot()[2] == "1")
            os.write(primary, b"dy\n")
            wait_for(lambda: not (target / "file10.txt").exists())
            trash = data / "Trash"
            wait_for(lambda: (trash / "files/file10.txt").exists())
            assert (trash / "info/file10.txt.trashinfo").exists()
            wait_for(lambda: snapshot()[2] == "0")
            # Selections remain active in an empty directory, before anything is yanked.
            snapshot(b"g " + str(source).encode() + b"\n")
            selected = snapshot(b"ggl") # Enter the child directory.
            (child / "nested.txt").write_text("nested\n")
            wait_for(lambda: snapshot()[2] == "1")
            assert snapshot(b" ")[3] == "1"
            snapshot(b"h")
            assert snapshot(b"ggjj ")[3] == "2" # Add file2 from the parent.
            snapshot(b"g " + str(target).encode() + b"\n")
            assert snapshot()[2:4] == ["0", "2"]
            snapshot(b"y")
            os.write(primary, b"p")
            wait_for(lambda: (target / "nested.txt").exists() and (target / "file2.txt").exists())
            assert (target / "nested.txt").read_text() == "nested\n"
            assert (target / "file2.txt").read_text() == "file2.txt\n"
            # Rename acts on an explicit selection even after leaving its directory.
            empty = root / "rename-empty"; empty.mkdir()
            snapshot(b"gg ")
            snapshot(b"g " + str(empty).encode() + b"\n")
            assert snapshot()[2:4] == ["0", "1"]
            snapshot(b"rrenamed.txt\n")
            wait_for((target / "renamed.txt").exists)
            assert snapshot()[3] == "1"
            snapshot(b"y")
            os.write(primary, b"p")
            wait_for((empty / "renamed.txt").exists)
            wait_for(lambda: snapshot()[2] == "1")
            # Small terminals keep input safe while waiting for a usable layout.
            fcntl.ioctl(primary, termios.TIOCSWINSZ, struct.pack("HHHH", 3, 8, 0, 0))
            process.send_signal(signal.SIGWINCH)
            snapshot()
            snapshot(b"~aw")
            fcntl.ioctl(primary, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
            process.send_signal(signal.SIGWINCH)
            assert snapshot()[2] == "1"
            os.write(primary, b"q")
            process.wait(timeout=10)
            errors.seek(0); error = errors.read().decode(errors="replace")
            assert process.returncode == 0, error
            assert "AddressSanitizer" not in error and "runtime error:" not in error, error
            assert (trash / "files/file10.txt").exists(), "trash survives normal shutdown"
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            os.close(primary)
print("Vim keys, natural sort, selection, tabs, history, filter, background paste, and trash passed")
