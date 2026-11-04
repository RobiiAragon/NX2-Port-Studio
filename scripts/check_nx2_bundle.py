#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the signed app and pinned export descriptor survive synthetic imports."""
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
    engine = app / "Contents/Resources/nx2-engine"
    descriptor = json.loads((engine / "nx2/native_runtime.json").read_text())
    assert len(descriptor["commit"]) == 40 and descriptor["platform"] == "macOS"
    layer = descriptor.get("overlay")
    if layer:
        overlays = engine / "nx2/engine_overlays"
        content = (overlays / layer["manifest"]).read_bytes()
        assert hashlib.sha256(content).hexdigest() == layer["sha256"]
        manifest = json.loads(content)
        assert hashlib.sha256((overlays / manifest["patch_file"]).read_bytes()).hexdigest() == manifest["patch_sha256"]
    assert (engine / "requirements-nx2-runtime.txt").is_file()
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
