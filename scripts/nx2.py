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
from nx2.pipeline import inspect_dump, prepare, verify_workspace


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("inspect", "prepare"):
        sub = commands.add_parser(name)
        sub.add_argument("dump", type=Path, help="Game root, exefs/ folder, or exefs/main")
        sub.add_argument("--cpu-census", action="store_true", help="Count selected ARM64 instruction patterns")
        sub.add_argument("--json", action="store_true", help="Emit one JSON result, including errors")
        sub.add_argument("--symbols", action="store_true", help="Read declared dynamic symbol tables and SDK imports")
        if name == "prepare":
            sub.add_argument("--output", required=True, type=Path, help="New private workspace, outside source and dump")
            sub.add_argument("--platform", choices=("macOS", "iOS"), default="macOS")
            sub.add_argument("--resources", type=Path, help="Optional registered local resource library")
    verify = commands.add_parser("verify", help="Check that a workspace still refers to the same executable bytes")
    verify.add_argument("workspace", type=Path)
    verify.add_argument("--json", action="store_true")
    resource = commands.add_parser("resources", help="Inspect or import optional Mii and font resources")
    resource.add_argument("action", choices=("inspect", "import", "registered"))
    resource.add_argument("catalog", type=Path)
    resource.add_argument("--source-platform", choices=("switch1", "switch2"))
    resource.add_argument("--output", type=Path, help="New private resource library, for import")
    resource.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.command == "inspect":
            result = inspect_dump(args.dump, census=args.cpu_census, symbols=args.symbols)
        elif args.command == "prepare":
            result = prepare(args.dump, args.output, platform=args.platform, census=args.cpu_census,
                             symbols=args.symbols, resources=args.resources)
        elif args.command == "verify":
            result = verify_workspace(args.workspace)
        else:
            from nx2.resources import inspect_catalog, import_catalog, inspect_registered
            if args.action == "import" and args.output is None:
                raise DumpError("Resource import requires --output")
            if args.action == "registered":
                result = inspect_registered(args.catalog)
            else:
                result = (import_catalog(args.catalog, args.output, source_platform=args.source_platform)
                          if args.action == "import" else inspect_catalog(args.catalog, source_platform=args.source_platform))
        if args.json:
            print(json.dumps(result))
        elif args.command in ("inspect", "prepare"):
            report = result.get("report", result)
            print(f"{report['target']['name']} ({report['metadata']['program_id']})")
            print(f"Validated {len(report['modules'])} modules. Keys: not needed. Firmware: not needed for inspection.")
            if "workspace" in result:
                print(f"Prepared: {result['workspace']}")
            print("Runtime: not ready. Native game export requires the remaining CPU, NVN2 and shader work.")
        else:
            print(json.dumps(result, indent=2))
        return 0
    except (DumpError, OSError) as error:
        if args.json:
            print(json.dumps({"schema_version": 1, "stage": "error", "error": str(error),
                              "code": getattr(error, "code", "io-error"), "hint": getattr(error, "hint", None)}))
        else:
            print(f"[RED] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
