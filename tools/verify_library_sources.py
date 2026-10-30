#!/usr/bin/env python3
"""Compare committed library snapshots with the pinned trees in local clones."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


def git(repository, *arguments):
    return subprocess.check_output(["git", "-C", str(repository), *arguments])


def tree(repository, revision, *paths):
    entries = {}
    for record in git(repository, "ls-tree", "-rz", revision, "--", *paths).split(b"\0"):
        if record:
            metadata, name = record.split(b"\t", 1)
            entries[name] = tuple(metadata.split())
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream-root", type=Path,
                        help="directory containing a local clone named for each library")
    parser.add_argument("--ref", default="HEAD", help="CupidFM commit to check (default: HEAD)")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    pins = json.loads(git(root, "show", arguments.ref + ":lib/sources.json"))
    actual = tree(root, arguments.ref, "lib")
    covered = {b"lib/README.md", b"lib/sources.json"}
    problems = []
    for name, source in pins.items():
        commit = source["commit"]
        if not re.fullmatch(r"[A-Za-z0-9_-]+", name) or not re.fullmatch(r"[a-f0-9]{40,64}", commit):
            raise ValueError("invalid library name or commit in source pins")
        prefix = ("lib/" + name + "/").encode()
        present = {path: metadata for path, metadata in actual.items() if path.startswith(prefix)}
        covered.update(present)
        recorded_tree = source.get("tree", "")
        if not re.fullmatch(r"[a-f0-9]{40,64}", recorded_tree):
            raise ValueError(f"{name}: missing or invalid pinned tree hash")
        stored_tree = git(root, "rev-parse", arguments.ref + ":lib/" + name).strip().decode()
        if stored_tree != recorded_tree:
            problems.append((prefix.rstrip(b"/"), "committed tree differs from the pinned tree"))
        if not arguments.upstream_root:
            if stored_tree == recorded_tree:
                print(f"{name}: committed tree matches {recorded_tree[:12]}")
            continue
        upstream = arguments.upstream_root / name
        source_tree = git(upstream, "rev-parse", commit + "^{tree}").strip().decode()
        if source_tree != recorded_tree:
            problems.append((prefix.rstrip(b"/"), "recorded tree differs from the upstream commit"))
        expected = {prefix + path: metadata for path, metadata in tree(upstream, commit).items()}
        issues = []
        for path in sorted(expected.keys() | present.keys()):
            if path not in present:
                issues.append((path, "missing"))
            elif path not in expected:
                issues.append((path, "extra tracked file"))
            elif present[path] != expected[path]:
                issues.append((path, "different blob, mode, or object type"))
        if issues:
            problems.extend(issues)
            print(f"{name}: {len(issues)} snapshot mismatches", file=sys.stderr)
        else:
            print(f"{name}: {len(expected)} files match {commit[:12]}")
    problems.extend((path, "unlisted library file") for path in sorted(actual.keys() - covered))
    for path, reason in problems[:30]:
        print(f"{path.decode(errors='backslashreplace')}: {reason}", file=sys.stderr)
    if problems:
        print(f"{len(problems)} mismatches; committed files must match the upstream trees", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Source verification failed: {error}", file=sys.stderr)
        sys.exit(1)
