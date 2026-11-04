#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply and check NX2's host layer in a disposable exact-engine checkout.

This source-only check takes no game inputs. The caller owns the checkout and
its generated build directory. Never point it at an installed game or a custom
engine: application requires every source fingerprint to match the clean pin.
"""
import argparse
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from nx2.engine_overlay import apply_overlay
from nx2.native_runtime import specification, verify_engine


def check_packer(engine):
    # Run the actual packer's plist function with an authored environment file.
    import importlib.util
    import tempfile
    module = importlib.util.spec_from_file_location("nx2_engine_packer", engine / "dm/mac/pack_app.py")
    packer = importlib.util.module_from_spec(module)
    module.loader.exec_module(packer)
    args = SimpleNamespace(name="Original network fixture", bundle_id="org.example.fixture",
                           version="1.0", title_id="0400000000000000", category=None, marker=None,
                           lang="es", settings=None, env_file=None)
    assert "NSLocalNetworkUsageDescription" not in packer.info_plist(args, "26.6", None)
    with tempfile.TemporaryDirectory(prefix="nx2-network-plist-") as temporary:
        env = Path(temporary) / "env.txt"
        env.write_text("# Authored fixture\nDM_NETWORK=auto\n", encoding="utf-8")
        args.env_file = str(env)
        assert packer.info_plist(args, "26.6", None)["NSLocalNetworkUsageDescription"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path, help="disposable clean checkout at nx2/native_runtime.json's exact commit")
    options = parser.parse_args()
    engine = options.engine.resolve(strict=True)
    chain = engine / "dm/gfx/nvn/dmsh_chain.h"
    before = chain.read_bytes()
    apply_overlay(engine, specification())
    receipt = verify_engine(engine)
    assert receipt["local_services"] == ["lan", "ldn"] and not receipt["gameplay_verified"]
    assert chain.read_bytes() == before
    check_packer(engine)
    # The upstream sanitizer helper concatenates SAN_DIR into compiler flags
    # and cat commands. A relative path keeps the normal Application Support
    # engine cache usable without splitting its spaces into shell arguments.
    commands = [
        ["make", "-C", "dm/host/tests", "-j1", "GAME=0400C3F00006E000", "SAN_DIR=../../sanitizers", "local-network-tests", "net-write", "net-write-sanitize"],
        ["make", "-C", "dm/host/tests", "-j1", "GAME=0400C3F00006E000", "SAN_DIR=../../sanitizers", "local-network-sanitize"],
        ["make", "-C", "dm/host", "-j2", "GAME=0400C3F00006E000"],
    ]
    for command in commands:
        subprocess.run(command, cwd=engine, check=True)
    verify_engine(engine)
    assert chain.read_bytes() == before
    print("PASS: exact native engine layer, unchanged shader chain, real LAN/LDN/EventFd fixtures, sanitizers and complete host link. Gameplay remains unqualified.")


if __name__ == "__main__":
    main()
