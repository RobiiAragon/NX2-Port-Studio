# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only input inspection and staged, private bring-up workspaces."""
from __future__ import annotations

import json
import os
import shutil
import tempfile
from pathlib import Path

from . import __version__
from .formats import DumpError, MODULE_NAME, read_npdm, read_nso

REPOSITORY = Path(__file__).resolve().parents[1]
MKW_ID = "0400C3F00006E000"
BLOCKERS = [
    {"id": "native-cpu", "detail": "Validate Switch 2 ARM64 rewriting, PAC and ABI on Apple hardware."},
    {"id": "horizon-nx2", "detail": "Implement the SDK, service and memory differences used by the selected build."},
    {"id": "nvn2-metal", "detail": "Implement and validate NVN2 API interception on Metal."},
    {"id": "ampere-shaders", "detail": "Integrate the standalone Ampere lowerer with shader extraction, reflection and Metal translation."},
    {"id": "mesh-task", "detail": "Implement task/mesh geometry and validate it with Mario Kart World."},
    {"id": "gameplay", "detail": "Verify boot, rendering, races, audio, saves and memory on Mac; then on iPhone."},
]


def child(directory: Path, name: str) -> Path | None:
    matches = [entry for entry in directory.iterdir() if entry.name.casefold() == name.casefold()]
    if len(matches) > 1:
        raise DumpError(f"Ambiguous {name} entries in {directory}")
    return matches[0] if matches else None


def local_file(path: Path, root: Path) -> None:
    if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(root):
        raise DumpError(f"Expected a regular file inside the dump: {path.name}")


def inspect_dump(input_path: Path, *, census: bool = False) -> dict:
    if input_path.is_symlink():
        raise DumpError("Select the actual dump folder, not a symbolic link")
    selected = input_path.expanduser().resolve(strict=True)
    if selected.is_file():
        if selected.name != "main":
            raise DumpError("Select the game folder, exefs folder, or exefs/main")
        selected = selected.parent
    if not selected.is_dir():
        raise DumpError("Select an extracted game folder")
    nested = child(selected, "exefs")
    exefs = nested if nested is not None else selected
    if exefs.is_symlink() or not exefs.is_dir():
        raise DumpError("ExeFS must be a real directory")
    root = exefs.parent
    for name in ("main", "main.npdm", "rtld", "sdk"):
        path = exefs / name
        if not path.exists():
            raise DumpError(f"Incomplete ExeFS: missing {name}; finish the dump before importing")
        local_file(path, root)
    metadata = read_npdm(exefs / "main.npdm")
    if metadata["platform"] != "switch2" or not metadata["is_64_bit"]:
        raise DumpError("This input is not a 64-bit Switch 2 NPDM (ACID +0x300)")
    romfs = child(root, "romfs")
    if romfs is None:
        images = sorted(p for parent in (root, exefs) for p in parent.iterdir()
                        if p.suffix.casefold() == ".romfs" or p.name.casefold() == "romfs.img")
        if len(images) != 1:
            raise DumpError("Expected a sibling romfs/ directory or one RomFS image")
        romfs = images[0]
    if romfs.is_symlink() or not romfs.resolve().is_relative_to(root):
        raise DumpError("RomFS must be inside the dump, without a symbolic link")
    if romfs.is_dir():
        if next(romfs.iterdir(), None) is None:
            raise DumpError("RomFS is empty; finish the dump before importing")
        romfs_kind = "directory"
    elif romfs.is_file():
        if romfs.stat().st_size < 0x50:
            raise DumpError("RomFS image is truncated")
        with romfs.open("rb") as source:
            if int.from_bytes(source.read(8), "little") != 0x50:
                raise DumpError("RomFS image has no supported decrypted RomFS header")
        romfs_kind = "image"
    else:
        raise DumpError("RomFS must be a directory or regular image")
    modules = []
    for path in sorted(exefs.iterdir(), key=lambda p: (p.name != "rtld", p.name != "main", p.name)):
        if MODULE_NAME.fullmatch(path.name):
            local_file(path, root)
            modules.append(read_nso(path, census=census))
    return {"schema_version": 1, "tool_version": __version__, "stage": "validated-input",
            "input": {"root": str(root), "exefs": str(exefs), "romfs": str(romfs), "romfs_kind": romfs_kind},
            "metadata": metadata, "modules": modules,
            "target": {"name": "Mario Kart World" if metadata["program_id"] == MKW_ID else "Unregistered Switch 2 title",
                       "initial_target": metadata["program_id"] == MKW_ID, "gameplay_verified": False},
            "requires_keys": False, "requires_firmware_for_inspection": False,
            "validation_scope": "NSO segment bounds, decompression and declared hashes; NPDM layout; RomFS presence/header. No full RomFS integrity or signature verification.",
            "runtime_ready": False, "blockers": BLOCKERS}


def prepare(input_path: Path, output: Path, *, platform: str, census: bool = False) -> dict:
    report = inspect_dump(input_path, census=census)
    output = output.expanduser().absolute()
    parent = output.parent.resolve(strict=True)
    destination = parent / output.name
    source = Path(report["input"]["root"])
    if destination.is_relative_to(REPOSITORY) or destination.is_relative_to(source):
        raise DumpError("Choose a private workspace outside the source repository and dump folder")
    if os.path.lexists(destination):
        raise DumpError("The output already exists; choose a new workspace name")
    staging = Path(tempfile.mkdtemp(prefix=".nx2-prepare-", dir=parent))
    try:
        (staging / "inspection.json").write_text(json.dumps(report, indent=2) + "\n")
        (staging / "build-plan.json").write_text(json.dumps({
            "schema_version": 1, "platform": platform, "architecture": "arm64",
            "program_id": report["metadata"]["program_id"], "status": "awaiting-runtime-support",
            "artifact": None, "gameplay_verified": False, "blockers": BLOCKERS,
            "module_build_ids": {m["name"]: m["build_id"] for m in report["modules"]},
        }, indent=2) + "\n")
        shutil.copyfile(REPOSITORY / "upstreams.lock.json", staging / "upstreams.lock.json")
        (staging / "README.txt").write_text(
            "NX2 Port Studio bring-up workspace\n\n"
            "The game remains in its original folder. No keys or firmware were requested.\n"
            "This workspace contains local paths and build metadata; keep it private.\n"
            "No game executable or IPA was produced. Runtime blockers are in build-plan.json.\n")
        # rename must not replace a directory another process created meanwhile.
        destination.mkdir()
        try:
            for path in staging.iterdir():
                shutil.move(str(path), str(destination / path.name))
        except BaseException:
            shutil.rmtree(destination)
            raise
    finally:
        shutil.rmtree(staging, ignore_errors=True)
    return {"schema_version": 1, "stage": "prepared", "workspace": str(destination),
            "platform": platform, "runtime_ready": False, "artifact": None,
            "report": report}
