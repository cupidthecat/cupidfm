#!/usr/bin/env python3
"""Convert ordered syntax definitions into commented CupidConf files."""
import argparse
import hashlib
import json
from pathlib import Path

import yaml


def quote(value):
    return "'" + str(value).replace("\n", r"\n").replace("\r", r"\r").replace("'", "''") + "'"


def convert(source, output, license_file, style_definition=None):
    paths = sorted(source.glob("*.yaml"))
    sources = {path.stem: yaml.safe_load(path.read_text()) for path in paths}
    by_type = {definition["filetype"]: key for key, definition in sources.items()}
    output.mkdir(parents=True, exist_ok=True)
    names = {}
    for name, definition in sources.items():
        adapted = style_definition is not None and name == style_definition.stem
        target = "colorscheme" if adapted else name
        language = "colorscheme" if adapted else definition["filetype"]
        names[name] = target
        lines = [
            "# Syntax: " + language,
            "# Regex values are literal: backslashes, #, and ; are preserved.",
            "# Double a single quote inside a quoted value to write one quote.",
            "# Rule numbers preserve precedence; later patterns override earlier ones.",
            "",
            "format=2",
            "name=" + quote(language),
            "",
            "# Detection: filename first, then header and content signature.",
        ]
        for key, value in (definition.get("detect") or {}).items():
            if adapted and key == "filename":
                value = r"\.theme$"
            lines.append("detect." + key + "=" + quote(value))
        counter = [0]

        def rules(items, parent=-1, stack=(), expand=True):
            includes = []
            for item in items:
                for group, value in item.items():
                    if group == "include":
                        includes.append(value)
                        continue
                    index = counter[0]
                    counter[0] += 1
                    scope = "root" if parent < 0 else f"inside region {parent}"
                    kind = "Pattern" if isinstance(value, str) else "Region"
                    lines.extend(["", f"# {kind} {index}: {group} ({scope}).",
                                  f"rule.{index}.parent={parent}",
                                  f"rule.{index}.group=" + quote(group)])
                    if isinstance(value, str):
                        lines.append(f"rule.{index}.match=" + quote(value))
                    else:
                        lines.append("# Start/end delimiters carry state between physical lines.")
                        for key in ("start", "end", "skip", "limit-group"):
                            if key in value:
                                if key == "skip":
                                    lines.append("# Mask escaped text before matching delimiters.")
                                elif key == "limit-group":
                                    lines.append("# Delimiters use this group instead of the region body.")
                                lines.append(f"rule.{index}.{key}=" + quote(value[key]))
                        rules(value.get("rules", []), index, stack)
            if expand:
                for include in includes:
                    target = by_type[include]
                    if target in stack:
                        raise ValueError("Cyclic inclusion: " + include)
                    lines.extend(["", "# Included language: " + include + ". Local rules come first."])
                    rules(sources[target].get("rules", []), parent, stack + (target,), False)

        rules(definition.get("rules", []), stack=(name,))
        lines.extend(["", "# Total flattened rules; update this when adding or removing rules.",
                      "rules=" + str(counter[0])])
        (output / (target + ".cupidconf")).write_text("\n".join(lines) + "\n")
    (output / "LICENSE").write_text(license_file.read_text())
    manifest = {"format": 2, "count": len(paths),
                "inputs": {names[p.stem] + ".yaml": hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Converted", len(paths), "definitions")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--license-file", type=Path, required=True)
    parser.add_argument("--style-definition", type=Path,
                        help="Adapt a theme-format definition to the neutral colorscheme catalog name.")
    args = parser.parse_args()
    convert(args.source, args.output, args.license_file, args.style_definition)
