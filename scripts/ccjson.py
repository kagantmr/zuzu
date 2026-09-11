#!/usr/bin/env python3
"""Build compile_commands.json from the .cmd files the compile rules emit.

    ccjson.py <build-dir> [out.json]

Each object has a sibling <obj>.cmd holding the exact command make ran, written
by $(file) so no shell quoting was ever applied or lost. shlex.split turns that
back into the argument vector clangd wants -- nothing here re-derives flags, so
it cannot drift from the real build.
"""

import json
import os
import pathlib
import shlex
import sys


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    build = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "compile_commands.json")

    if not build.is_dir():
        sys.exit(f"ccjson: {build} does not exist; build first")

    root = os.getcwd()
    entries = []
    for cmd in build.rglob("*.o.cmd"):
        text = cmd.read_text().strip()
        if not text:
            continue
        args = shlex.split(text)
        src = next((args[i + 1] for i, a in enumerate(args)
                    if a == "-c" and i + 1 < len(args)), None)
        if src is None:
            continue
        entries.append({
            "directory": root,
            "arguments": args,
            "file": src,
            "output": str(cmd)[:-len(".cmd")],
        })

    if not entries:
        sys.exit(f"ccjson: no .cmd files under {build}; build first")

    entries.sort(key=lambda e: (e["file"], e["output"]))
    out.write_text(json.dumps(entries, indent=2) + "\n")
    print(f"  CCJSON  {out} ({len(entries)} entries)")


if __name__ == "__main__":
    main()
