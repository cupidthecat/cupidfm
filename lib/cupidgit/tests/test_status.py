"""Exercise real Git indexes, malformed indexes, and refresh on a live handle."""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "tests/status_driver"
UNTRACKED, CLEAN, MODIFIED, ERROR = range(4)

def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args], stderr=subprocess.STDOUT)

class Status(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cupidgit-")
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name) / "repo"
        self.repo.mkdir()
        git(self.repo, "init", "-q")
        git(self.repo, "config", "user.name", "Fixture")
        git(self.repo, "config", "user.email", "fixture@example.invalid")

    def start(self, repo=None, expected="open"):
        p = subprocess.Popen([str(DRIVER), str(repo or self.repo)], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, text=True)
        def close():
            p.stdin.close()
            p.stdout.close()
            self.assertEqual(p.wait(timeout=10), 0)
        self.addCleanup(close)
        self.assertEqual(p.stdout.readline().strip(), expected)
        return p

    def status(self, p, path):
        p.stdin.write(str(path) + "\n")
        p.stdin.flush()
        return int(p.stdout.readline())

    def add(self, name, data=b"content"):
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        git(self.repo, "add", "--", name)
        return path

    def test_unborn_empty_and_refresh(self):
        path = self.repo / "new"
        path.write_text("hello")
        p = self.start()
        self.assertEqual(self.status(p, path), UNTRACKED)
        git(self.repo, "add", "new")
        self.assertEqual(self.status(p, path), CLEAN)
        path.write_text("world")
        self.assertEqual(self.status(p, path), MODIFIED)
        git(self.repo, "add", "new")
        self.assertEqual(self.status(p, path), CLEAN)
        (self.repo / ".git/index").unlink()
        self.assertEqual(self.status(p, path), UNTRACKED)

    def test_empty_file_same_timestamp_and_mode(self):
        empty = self.add("empty", b"")
        file = self.add("file", b"first")
        p = self.start()
        self.assertEqual(self.status(p, empty), CLEAN)
        stamp = file.stat().st_mtime_ns
        file.write_bytes(b"other")
        os.utime(file, ns=(stamp, stamp))
        self.assertEqual(self.status(p, file), MODIFIED)
        file.write_bytes(b"first")
        self.assertEqual(self.status(p, file), CLEAN)
        file.chmod(0o755)
        self.assertEqual(self.status(p, file), MODIFIED)
        file.unlink()
        self.assertEqual(self.status(p, file), MODIFIED)

    def test_symlinks_and_deleted_parents(self):
        link = self.repo / "link"
        link.symlink_to("/missing/outside/target")
        git(self.repo, "add", "link")
        file = self.add("nested/dir/file")
        p = self.start()
        self.assertEqual(self.status(p, link), CLEAN)
        link.unlink()
        link.symlink_to("changed-target")
        self.assertEqual(self.status(p, link), MODIFIED)
        shutil.rmtree(file.parent.parent)
        self.assertEqual(self.status(p, file), MODIFIED)
        outside = self.repo.with_name("repo-other")
        outside.mkdir()
        (outside / "file").write_text("outside")
        self.assertEqual(self.status(p, outside / "file"), ERROR)

    def test_versions_extended_flags_and_long_paths(self):
        paths = ["prefix/" + "segment/" * n + "a" for n in range(1, 12)]
        paths += ["long/" + "/".join(["x" * 180] * 20)]
        for name in paths:
            self.add(name)
        # The index permits names longer than the OS absolute pathname limit.
        oid = git(self.repo, "rev-parse", ":" + paths[0]).decode().strip()
        git(self.repo, "update-index", "--add", "--cacheinfo", "100644", oid,
            "longer/" + "/".join(["y" * 180] * 23))
        for version in (2, 3, 4):
            git(self.repo, "update-index", "--index-version", str(version))
            p = self.start()
            for name in paths:
                self.assertEqual(self.status(p, self.repo / name), CLEAN, (version, name))
        git(self.repo, "update-index", "--skip-worktree", paths[0])
        p = self.start()
        self.assertEqual(self.status(p, self.repo / paths[0]), CLEAN)

    def test_intent_to_add_and_unmerged(self):
        file = self.repo / "file"
        file.write_text("hello")
        git(self.repo, "add", "-N", "file")
        self.assertEqual(self.status(self.start(), file), MODIFIED)
        git(self.repo, "add", "file")
        oid = git(self.repo, "rev-parse", ":file").decode().strip()
        git(self.repo, "update-index", "--force-remove", "file")
        subprocess.run(["git", "-C", str(self.repo), "update-index", "--index-info"],
                       input=f"100644 {oid} 1\tfile\n100644 {oid} 2\tfile\n".encode(), check=True)
        self.assertEqual(self.status(self.start(), file), MODIFIED)

    def test_linked_worktree(self):
        file = self.add("file")
        git(self.repo, "commit", "-qm", "fixture")
        wt = self.repo.with_name("worktree")
        git(self.repo, "worktree", "add", "-q", "-b", "fixture", str(wt))
        p = self.start(wt)
        self.assertEqual(self.status(p, wt / "file"), CLEAN)
        (wt / "file").write_bytes(b"changed")
        self.assertEqual(self.status(p, wt / "file"), MODIFIED)
        self.assertEqual(self.status(self.start(), file), CLEAN)

    def test_index_corruption_and_mandatory_extensions(self):
        file = self.add("file")
        index = self.repo / ".git/index"
        original = index.read_bytes()
        for data in (b"", b"DIRC", original[:-1], original[:16] + b"x" + original[17:]):
            index.write_bytes(data)
            self.start(expected="error")
        # Correct checksum, invalid mandatory extension or truncated entry list.
        for payload in (original[:-20] + b"link" + struct.pack(">I", 0),
                        original[:8] + struct.pack(">I", 2) + original[12:-20]):
            index.write_bytes(payload + hashlib.sha1(payload).digest())
            self.start(expected="error")
        index.write_bytes(original)
        p = self.start()
        index.write_bytes(b"broken")
        self.assertEqual(self.status(p, file), ERROR)
        index.write_bytes(original)
        self.assertEqual(self.status(p, file), CLEAN)

if __name__ == "__main__":
    unittest.main()
