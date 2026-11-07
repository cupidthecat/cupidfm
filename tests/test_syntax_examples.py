#!/usr/bin/env python3
"""Compare C highlighting with checked-in token-color expectations."""
import json
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
driver = root / "tests/syntax_snapshot"
examples = root / "examples/syntax"
cases = {"c": "c.c", "markdown": "markdown.md", "python3": "python.py",
         "javascript": "javascript.js", "html": "html.html", "sh": "shell.sh",
         "json": "json.json", "yaml": "yaml.yaml"}


def check(language, example, expected):
    result = subprocess.run([str(driver), str(root / "syntax" / (language + ".cupidconf")),
                             str(examples / example)], check=True, capture_output=True, text=True)
    actual = json.loads(result.stdout)
    assert len(actual) == len(expected), (language, example, "line count")
    for line, (want, got) in enumerate(zip(expected, actual), 1):
        assert want == got, (language, example, line, want, got)


for language, example in cases.items():
    check(language, example, json.loads((examples / (example + ".expected.json")).read_text()))
all_languages = json.loads((examples / "corpus.expected.json").read_text())
assert len(all_languages) == 158
for language, expected in all_languages.items():
    check(language, "corpus.txt", expected)
print("158 definitions match the token-color corpus; 8 language examples passed")
