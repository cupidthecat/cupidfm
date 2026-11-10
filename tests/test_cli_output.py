#!/usr/bin/env python3
"""CLI output paths resolve against the launch directory and follow chooser/quit semantics."""
import fcntl
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import tempfile
import termios
import time

binary = Path(os.environ.get("CUPIDFM_TEST_BINARY", Path(__file__).resolve().parents[1] / "cupidfm"))
help_text = subprocess.run([str(binary), "--help"], capture_output=True, text=True, check=True).stdout
for text in ("--keymap", "--cwd-file", "--chooser-file", "Mouse", "Tabs",
             "Q", "syntax", "examples/syntax", "key", "Editor", "t then t", "t then r", "fuzzy"):
    assert text in help_text, text
with tempfile.TemporaryDirectory(prefix="cupidfm-cli-output-") as temporary:
    root = Path(temporary)
    home = root / "home"; home.mkdir()
    (home / ".cupidfmrc").write_text("# default keymap\n")
    source = root / "source"; source.mkdir()
    (source / "selected.txt").write_text("data")
    plugins = home / ".cupidfm/plugins"; plugins.mkdir(parents=True)
    (plugins / "ready.cs").write_text(r'''
let checks = 0;
fn snapshot(base) {
    write_file(base + "-cwd", fm.cwd());
    write_file(base + "-name", fm.selected_name());
}
fn on_load() {
    let ready = getenv("CUPIDFM_READY");
    snapshot(ready);
    write_file(ready, "ready");
}
fn on_key(key) {
    if (key == "!") {
        checks = checks + 1;
        snapshot(getenv("CUPIDFM_READY") + "-" + to_str(checks));
        return true;
    }
    return false;
}
''')
    def launch(options, keys, entries=None):
        ready_path = root / "ready"
        ready_path.unlink(missing_ok=True)
        primary, secondary = pty.openpty()
        fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
        env = dict(os.environ, TERM="xterm-256color", HOME=str(home), CUPIDFM_READY=str(ready_path))
        with tempfile.TemporaryFile() as errors:
            process = subprocess.Popen([str(binary), "--keymap=vim", *options,
                                        *(entries if entries is not None else ["source"])], cwd=root,
                                       env=env, stdin=secondary, stdout=secondary, stderr=errors,
                                       start_new_session=True)
            os.close(secondary)
            try:
                sent = False
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline and process.poll() is None:
                    readable, _, _ = select.select([primary], [], [], 0.02)
                    if readable:
                        try: os.read(primary, 65536)
                        except OSError: break
                    if ready_path.exists() and not sent:
                        os.write(primary, keys); sent = True
                process.wait(timeout=2)
                errors.seek(0); error = errors.read().decode(errors="replace")
                assert process.returncode == 0, error
                assert "AddressSanitizer" not in error and "runtime error:" not in error, error
            finally:
                if process.poll() is None:
                    process.kill(); process.wait()
                os.close(primary)
    launch(["--cwd-file=cwd.txt"], b"hq")
    assert (root / "cwd.txt").read_text() == str(root)
    launch(["--cwd-file=skipped.txt"], b"Q")
    assert not (root / "skipped.txt").exists()
    launch(["--chooser-file", "chosen.txt"], b"\n")
    assert (root / "chosen.txt").read_text() == str(source / "selected.txt") + "\n"
    empty = root / "empty"; empty.mkdir()
    launch(["--chooser-file=across-directories.txt"], b" g " + str(empty).encode() + b"\no")
    assert (root / "across-directories.txt").read_text() == str(source / "selected.txt") + "\n"
    # File arguments keep their hover across independent startup tabs.
    (source / "broken").symlink_to(root / "missing")
    hidden = ".hidden\nname"
    (source / hidden).write_text("hidden")
    launch([], b"!2!3!4!1!q", ["source/selected.txt", "empty/", "source/broken", "source/" + hidden])
    assert (root / "ready-cwd").read_text() == str(source)
    assert (root / "ready-name").read_text() == "selected.txt", "startup hook sees the hovered file"
    for index, (directory, name) in enumerate(((source, "selected.txt"), (empty, ""),
                                               (source, "broken"), (source, hidden),
                                               (source, "selected.txt")), start=1):
        assert (root / f"ready-{index}-cwd").read_text() == str(directory)
        assert (root / f"ready-{index}-name").read_text() == name, (index, name, (root / f"ready-{index}-name").read_text())
    launch(["--chooser-file=file-argument.txt"], b"\n", ["source/selected.txt"])
    assert (root / "file-argument.txt").read_text() == str(source / "selected.txt") + "\n"
    for index in range(250): (source / f"item-{index:03}").write_text("")
    launch([], b"!q", ["source/selected.txt"])
    assert (root / "ready-1-name").read_text() == "selected.txt", "reveal beyond initial batch"
    launch([], b"9!q", ["empty"] * 9)
    assert (root / "ready-1-cwd").read_text() == str(empty)
    dashed = root / "-directory"; dashed.mkdir()
    launch(["--"], b"!q", ["-directory"])
    assert (root / "ready-1-cwd").read_text() == str(dashed)
    for arguments in (["missing"], ["empty", "missing"], ["source/selected.txt/"]):
        result = subprocess.run([str(binary), *arguments], cwd=root, capture_output=True, timeout=5)
        assert result.returncode == 1 and b"cannot open entry" in result.stderr, result
    for arguments in (["--keymap=unknown"], ["--cwd-file"], ["--chooser-file="], ["empty"] * 10):
        result = subprocess.run([str(binary), *arguments], cwd=root, capture_output=True, timeout=5)
        assert result.returncode == 2, (arguments, result.returncode)
print("CLI entry tabs, literal file reveal, limits, keymap, cwd/chooser output, and validation passed")
