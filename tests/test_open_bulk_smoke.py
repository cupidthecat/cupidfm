#!/usr/bin/env python3
"""Exercise opener dispatch, common choices, detached commands, and reviewed bulk renames."""
import fcntl
import json
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
with tempfile.TemporaryDirectory(prefix="cupidfm-open-bulk-") as temporary:
    root = Path(temporary)
    home = root / "home"; home.mkdir()
    source = root / "files"; source.mkdir()
    (home / ".cupidfmrc").write_text("keymap=vim\n")
    plugins = home / ".cupidfm/plugins"; plugins.mkdir(parents=True)
    (plugins / "ready.cs").write_text('''
fn on_load() { write_file(getenv("CUPIDFM_TEST_ROOT") + "/ready", "ready"); }
fn on_key(key) {
    if (key == "!") {
        write_file(getenv("CUPIDFM_TEST_ROOT") + "/selected", to_str(len(fm.selected_paths())));
        return true;
    }
    return false;
}
''')
    names = ["01 quoted ' file.foo", "02$(touch PWNED).foo", "03 third.bar"]
    for name in names:
        (source / name).write_text(name + "\n")
    helper = root / "helper with spaces.py"
    helper.write_text('''
import json, os, sys
from pathlib import Path
root = Path(os.environ["CUPIDFM_TEST_ROOT"])
mode, *paths = sys.argv[1:]
if mode == "edit" and len(paths) == 1 and Path(paths[0]).name.startswith("cupidfm-rename-"):
    before = Path(paths[0]).read_text().splitlines()
    replacements = json.loads((root / "rename-request.json").read_text())
    Path(paths[0]).write_text("\\n".join(replacements.get(name, name) for name in before)+"\\n")
    (root / "bulk-edited").write_text("done")
elif mode == "edit" and len(paths) == 1 and Path(paths[0]).name.startswith("cupidfm-create-"):
    Path(paths[0]).write_text((root / "create-request.txt").read_text())
else:
    record = {"paths": paths, "cwd": str(Path.cwd()), "session": os.getsid(0)}
    staging = root / (mode + ".tmp")
    staging.write_text(json.dumps(record))
    staging.replace(root / (mode + ".json"))
    if mode == "shared":
        import time
        deadline = time.monotonic() + 30
        while not (root / "release-detached").exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        (root / "detached-finished").write_text("done")
''')
    config = home / ".cupidfm/openers.cupidconf"
    config.write_text('''
# Blocking text opener also edits bulk rename lists.
opener.edit.command = 'python3 "$CUPIDFM_RECORDER" edit "$@"'
opener.edit.block = true
opener.viewer.command = 'python3 "$CUPIDFM_RECORDER" view "$@"'
opener.viewer.description = View files
opener.shared.command = 'python3 "$CUPIDFM_RECORDER" shared "$@"'
opener.shared.description = Preview all files
opener.shared.orphan = true
rule.foo.path = *.foo
rule.foo.use = viewer,shared
rule.bar.path = *.bar
rule.bar.use = edit,shared
''')
    primary, secondary = pty.openpty()
    fcntl.ioctl(secondary, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", HOME=str(home), CUPIDFM_TEST_ROOT=str(root),
               CUPIDFM_RECORDER=str(helper), CUPIDFM_OPENERS=str(config))
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen([str(binary), str(source)], env=env, stdin=secondary,
                                   stdout=secondary, stderr=errors, start_new_session=True)
        os.close(secondary)
        output = bytearray()
        def wait_for(predicate, timeout=15):
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
                                  bytes(output[-2500:])))
        def review():
            offset = len(output)
            os.write(primary, b"r")
            wait_for(lambda: b"Enter approves" in output[offset:])
        try:
            wait_for((root / "ready").exists)
            os.write(primary, b"\x01o")
            wait_for(lambda: (root / "edit.json").exists() and (root / "view.json").exists())
            edit = json.loads((root / "edit.json").read_text())
            view = json.loads((root / "view.json").read_text())
            assert edit["paths"] == [str(source / names[2])]
            assert view["paths"] == [str(source / name) for name in names[:2]]
            assert edit["cwd"] == view["cwd"] == str(source)
            assert not (source / "PWNED").exists(), "filenames reached the shell as literal arguments"
            os.write(primary, b"O/paf\n\n")
            wait_for((root / "shared.json").exists)
            shared = json.loads((root / "shared.json").read_text())
            assert shared["paths"] == [str(source / name) for name in names]
            assert shared["session"] != process.pid, "orphan opener has its own session"

            replacements = {names[0]: "renamed ' first.txt", names[2]: "renamed third.txt"}
            (root / "rename-request.json").write_text(json.dumps(replacements))
            review()
            os.write(primary, b"\x1b")
            time.sleep(0.1)
            assert all((source / name).exists() for name in names), "cancelling changes no files"
            review()
            os.write(primary, b"\n")
            wait_for(lambda: all((source / name).exists() for name in replacements.values()))
            assert not (source / names[0]).exists() and not (source / names[2]).exists()
            assert (source / replacements[names[0]]).read_text() == names[0] + "\n"
            assert (source / names[1]).exists()
            os.write(primary, b"!")
            wait_for((root / "selected").exists)
            assert (root / "selected").read_text() == "3", "selections follow renamed paths"
            (root / "create-request.txt").write_text("folder/one.txt\nfolder/empty/\n"
                                                     + replacements[names[0]] + "\nafter-existing.txt\n")
            offset = len(output)
            os.write(primary, b"A")
            wait_for(lambda: b"Enter approves" in output[offset:])
            os.write(primary, b"\x1b")
            time.sleep(0.1)
            assert not (source / "folder").exists()
            offset = len(output)
            os.write(primary, b"A")
            wait_for(lambda: b"Enter approves" in output[offset:])
            os.write(primary, b"\n")
            wait_for(lambda: (source / "folder/one.txt").exists() and
                            (source / "folder/empty").is_dir() and (source / "after-existing.txt").exists())
            assert (source / replacements[names[0]]).read_text() == names[0] + "\n"
            os.write(primary, b"wjw")
            time.sleep(0.1)
            os.write(primary, b"q")
            process.wait(timeout=10)
            assert not (root / "detached-finished").exists(), "detached command still outlives the UI"
            (root / "release-detached").write_text("release")
            deadline = time.monotonic() + 5
            while not (root / "detached-finished").exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            assert (root / "detached-finished").exists(), "detached command finishes after CupidFM exits"
            errors.seek(0); error_text = errors.read().decode(errors="replace")
            assert process.returncode == 0, error_text
            assert "AddressSanitizer" not in error_text and "runtime error:" not in error_text, error_text
        finally:
            (root / "release-detached").write_text("release")
            if process.poll() is None:
                process.kill(); process.wait()
            os.close(primary)
print("Grouped openers, literal paths, common fuzzy choices, detached launches, bulk rename, and reviewed background creation passed")
