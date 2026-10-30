#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Inspect an Eden-style decrypted dump or prepare a private NX2 workspace."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

# A packaged Mac app is code-signed. Imports must not add __pycache__ files to it.
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nx2.formats import DumpError
from nx2.pipeline import inspect_dump, prepare


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("inspect", "prepare"):
        sub = commands.add_parser(name)
        sub.add_argument("dump", type=Path, help="Game root, exefs/ folder, or exefs/main")
        sub.add_argument("--cpu-census", action="store_true", help="Count selected ARM64 instruction patterns")
        sub.add_argument("--json", action="store_true", help="Emit one JSON result, including errors")
        if name == "prepare":
            sub.add_argument("--output", required=True, type=Path, help="New private workspace, outside source and dump")
            sub.add_argument("--platform", choices=("macOS", "iOS"), default="macOS")
    args = parser.parse_args(argv)
    try:
        if args.command == "inspect":
            result = inspect_dump(args.dump, census=args.cpu_census)
        else:
            result = prepare(args.dump, args.output, platform=args.platform, census=args.cpu_census)
        if args.json:
            print(json.dumps(result))
        else:
            report = result.get("report", result)
            print(f"{report['target']['name']} ({report['metadata']['program_id']})")
            print(f"Validated {len(report['modules'])} modules. Keys: not needed. Firmware: not needed for inspection.")
            if "workspace" in result:
                print(f"Prepared: {result['workspace']}")
            print("Runtime: not ready. Native game export requires the remaining CPU, NVN2 and shader work.")
        return 0
    except (DumpError, OSError) as error:
        if args.json:
            print(json.dumps({"schema_version": 1, "stage": "error", "error": str(error)}))
        else:
            print(f"[RED] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
