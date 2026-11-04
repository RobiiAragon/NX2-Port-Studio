# SPDX-License-Identifier: GPL-3.0-or-later
"""Attest a reviewed source patch over an exact upstream engine revision.

The patch is project source, never a downloaded runtime payload. Its manifest
pins every input/output file and its bytes. Only a new owned checkout is patched;
verification of an existing engine never repairs, overwrites or ignores changes.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
from pathlib import Path, PurePosixPath

from .errors import DumpError

OVERLAY_ROOT = Path(__file__).with_name("engine_overlays")
HASH = re.compile(r"[0-9a-f]{64}")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def local_path(root: Path, relative: str) -> Path:
    path = PurePosixPath(relative)
    if path.is_absolute() or not path.parts or any(p in {"", ".", "..", ".git"} for p in path.parts):
        raise DumpError("The engine overlay contains an invalid source path", code="native-engine-overlay")
    result = root
    for component in path.parts:
        result = result / component
        if result.is_symlink():
            raise DumpError("The engine overlay source must not use symbolic links", code="native-engine-overlay")
    return result


def load_overlay(spec: dict) -> dict | None:
    descriptor = spec.get("overlay")
    if descriptor is None:
        return None
    try:
        filename, expected = descriptor["manifest"], descriptor["sha256"]
        if not isinstance(filename, str) or PurePosixPath(filename).name != filename or not HASH.fullmatch(expected):
            raise ValueError("manifest identity")
        data = local_path(OVERLAY_ROOT, filename).read_bytes()
        if len(data) > 1024 * 1024 or digest(data) != expected:
            raise ValueError("manifest fingerprint")
        manifest = json.loads(data)
        services = manifest["local_services"]
        if not isinstance(services, list) or len(set(services)) != len(services) or any(s not in {"lan", "ldn"} for s in services):
            raise ValueError("local service inventory")
        patch_name = manifest["patch_file"]
        if manifest["schema_version"] != 1 or manifest["base_commit"] != spec["commit"]:
            raise ValueError("base revision")
        if not isinstance(patch_name, str) or PurePosixPath(patch_name).name != patch_name:
            raise ValueError("patch path")
        patch = local_path(OVERLAY_ROOT, patch_name).read_bytes()
        if len(patch) > 2 * 1024 * 1024 or digest(patch) != manifest["patch_sha256"]:
            raise ValueError("patch fingerprint")
        files = manifest["files"]
        if not isinstance(files, dict) or not 0 < len(files) <= 128:
            raise ValueError("source inventory")
        for relative, hashes in files.items():
            local_path(OVERLAY_ROOT, relative)
            if not (relative.startswith("dm/host/") or relative == "dm/mac/pack_app.py"):
                raise ValueError("source scope")
            if not HASH.fullmatch(hashes["after"]) or (hashes["before"] is not None and not HASH.fullmatch(hashes["before"])):
                raise ValueError("source fingerprint")
    except (KeyError, TypeError, ValueError, OSError) as error:
        raise DumpError("The reviewed engine overlay is incomplete or changed", code="native-engine-overlay") from error
    manifest["identity"] = expected
    manifest["patch"] = patch
    return manifest


def check_files(root: Path, overlay: dict, *, applied: bool) -> None:
    key = "after" if applied else "before"
    for relative, hashes in overlay["files"].items():
        path = local_path(root, relative)
        expected = hashes[key]
        if expected is None:
            matches = not os.path.lexists(path)
        else:
            matches = path.is_file() and digest(path.read_bytes()) == expected
        if not matches:
            raise DumpError("The native engine does not match its reviewed source overlay",
                            code="native-engine-dirty" if applied else "native-engine-overlay-base")


def apply_overlay(root: Path, spec: dict) -> None:
    overlay = load_overlay(spec)
    if overlay is None:
        return
    check_files(root, overlay, applied=False)
    for arguments in (["apply", "--check", "--whitespace=error"], ["apply", "--whitespace=error"]):
        result = subprocess.run(["git", "-C", str(root), *arguments], input=overlay["patch"], capture_output=True)
        if result.returncode:
            raise DumpError("The reviewed source overlay could not be applied", code="native-engine-overlay-base")
    check_files(root, overlay, applied=True)


def verify_overlay(root: Path, spec: dict) -> dict | None:
    overlay = load_overlay(spec)
    if overlay is not None:
        check_files(root, overlay, applied=True)
    return overlay


def cache_identity(spec: dict) -> str:
    overlay = load_overlay(spec)
    expected = spec["commit"] + ("-" + overlay["identity"][:16] if overlay else "")
    if spec.get("cache_id", expected) != expected:
        raise DumpError("The native engine cache identity does not match its source", code="native-engine-overlay")
    return expected
