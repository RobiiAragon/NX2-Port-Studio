#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the signed input-stage app remains sealed after importing synthetic data."""
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nx2.tests.fixtures import dump


def hashes(root):
    return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in root.rglob("*") if path.is_file() and not path.is_symlink()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()
    app = args.bundle.resolve(strict=True)
    subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(app)], check=True)
    binary = (app / "Contents/MacOS/NX2PortStudio").read_bytes()
    if any(prefix in binary for prefix in (b"/Users/", b"/home/")):
        raise SystemExit("Release executable contains a local development path")
    before = hashes(app)
    with tempfile.TemporaryDirectory(prefix="nx2-bundle-test-") as temporary:
        source = dump(Path(temporary) / "Synthetic input", compressed="zbic")
        script = app / "Contents/Resources/nx2-engine/scripts/nx2.py"
        # Deliberately no -B or environment override: the entry point itself
        # must protect signed bundle resources from Python bytecode writes.
        output = subprocess.check_output([sys.executable, str(script), "inspect", str(source), "--json"], text=True)
        result = json.loads(output)
        assert result["stage"] == "validated-input" and result["runtime_ready"] is False
    after = hashes(app)
    if before != after:
        raise SystemExit("Bundle resources changed after inspection")
    subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(app)], check=True)
    print("Signed bundle verified before and after synthetic import; resources unchanged.")


if __name__ == "__main__":
    main()
