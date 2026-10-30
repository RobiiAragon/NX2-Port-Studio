# SPDX-License-Identifier: GPL-3.0-or-later
"""Versioned, optional Mii/font resources from an existing decrypted Studio catalog."""
import hashlib
import json
import os
import re
import shutil
import tempfile
from pathlib import Path, PurePosixPath

from .errors import DumpError
from .input_io import stable_file

CANDIDATES = {"0100000000000802": "MiiModel", **{f"010000000000081{x}": "SharedFont" for x in range(5)}}
MII_FILES = {"NXTextureLowLinear.dat", "NXTextureLowSRGB.dat", "NXTextureMidLinear.dat",
             "NXTextureMidSRGB.dat", "ShapeHigh.dat", "ShapeMid.dat"}
MAX_FILE_BYTES = 128 * 1024 * 1024


def load_manifest(path):
    with stable_file(path) as (source, info):
        if info.st_size > 8 * 1024 * 1024:
            raise DumpError("Resource catalog is too large")
        try:
            data = json.load(source)
        except (ValueError, UnicodeDecodeError) as error:
            raise DumpError("Resource catalog is invalid JSON") from error
    if not isinstance(data, dict):
        raise DumpError("Resource catalog must be an object")
    return data


def safe_file(root, relative):
    if not isinstance(relative, str) or "\\" in relative:
        raise DumpError("Invalid resource path")
    parts = PurePosixPath(relative)
    if parts.is_absolute() or any(part in (".", "..") for part in relative.split("/")):
        raise DumpError("Resource path escapes its catalog")
    path = root
    for part in parts.parts:
        path = path / part
        if path.is_symlink():
            raise DumpError("Resource catalog contains a symbolic link")
    if not path.is_file() or not path.resolve().is_relative_to(root):
        raise DumpError("Resource file is unavailable inside its catalog")
    return path


def file_hash(path, expected_size, expected_hash):
    if (type(expected_size) is not int or not 0 < expected_size <= MAX_FILE_BYTES or
        not isinstance(expected_hash, str) or not re.fullmatch(r"[a-fA-F0-9]{64}", expected_hash)):
        raise DumpError("Resource catalog has invalid file metadata")
    digest = hashlib.sha256()
    with stable_file(path) as (source, info):
        if info.st_size != expected_size:
            raise DumpError("Resource size differs from its catalog")
        remaining = expected_size
        while remaining:
            block = source.read(min(1024 * 1024, remaining))
            if not block:
                raise DumpError("Resource became truncated")
            digest.update(block)
            remaining -= len(block)
    if digest.hexdigest() != expected_hash.lower():
        raise DumpError("Resource SHA-256 differs from its catalog")
    return digest.hexdigest()


def inspect_catalog(catalog, *, source_platform):
    if source_platform not in ("switch1", "switch2"):
        raise DumpError("Declare the resource catalog's source platform")
    if catalog.is_symlink():
        raise DumpError("Select the actual resource catalog")
    root = catalog.expanduser().resolve(strict=True)
    data = load_manifest(root / "manifest.json")
    version = data.get("firmwareVersion")
    if data.get("formatVersion") != 1 or not isinstance(version, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise DumpError("Unrecognized Studio resource catalog or firmware version")
    resources = data.get("resources")
    if not isinstance(resources, list) or len(resources) > 64:
        raise DumpError("Resource catalog has an invalid archive list")
    selected, seen = [], set()
    for resource in resources:
        if not isinstance(resource, dict):
            raise DumpError("Resource catalog has an invalid archive")
        title = resource.get("titleId")
        if title not in CANDIDATES:
            continue  # account/local-play material and system executables are not candidates
        if title in seen:
            raise DumpError("Resource catalog repeats an archive")
        seen.add(title)
        relative = f"system_data/{title}/files"
        if resource.get("relativePath") != relative:
            raise DumpError("Resource archive path does not match its title ID")
        files = resource.get("files")
        if not isinstance(files, list) or not files or len(files) > 128:
            raise DumpError("Resource archive has an invalid file list")
        checked, names = [], set()
        for item in files:
            if not isinstance(item, dict) or not isinstance(item.get("path"), str):
                raise DumpError("Resource archive has an invalid file")
            name = item["path"]
            if name in names or "/" in name or "\\" in name:
                raise DumpError("Candidate resource file name is duplicated or unsupported")
            if title == "0100000000000802" and name not in MII_FILES:
                raise DumpError("Mii archive contains an unexpected resource")
            if title != "0100000000000802" and not name.endswith(".bfttf"):
                raise DumpError("Font archive contains an unexpected resource")
            path = safe_file(root, relative + "/" + name)
            digest = file_hash(path, item.get("bytes"), item.get("sha256"))
            names.add(name)
            checked.append({"path": name, "bytes": item["bytes"], "sha256": digest})
        if title == "0100000000000802" and names != MII_FILES:
            raise DumpError("Mii archive lacks one of the six expected resource files")
        selected.append({"titleId": title, "name": CANDIDATES[title], "relativePath": relative, "files": checked})
    if not selected:
        raise DumpError("No supported Mii or font candidates in this catalog")
    selected.sort(key=lambda entry: entry["titleId"])
    digest = hashlib.sha256(json.dumps(selected, sort_keys=True).encode()).hexdigest()
    return {"schema_version": 1, "stage": "resources-inspected", "catalog": str(root),
            "firmware_version": version, "source_platform": source_platform,
            "resources": selected, "content_digest": digest,
            "file_count": sum(len(entry["files"]) for entry in selected),
            "bytes": sum(item["bytes"] for entry in selected for item in entry["files"]),
            "switch2_compatibility": "unverified", "runtime_enabled": False,
            "requires_keys": False}


def import_catalog(catalog, output, *, source_platform):
    report = inspect_catalog(catalog, source_platform=source_platform)
    root = Path(report["catalog"])
    output = output.expanduser().absolute()
    output = output.parent.resolve(strict=True) / output.name
    from .pipeline import REPOSITORY
    if output.is_relative_to(root) or output.is_relative_to(REPOSITORY):
        raise DumpError("Import resources outside the source catalog and code repository")
    if os.path.lexists(output):
        raise DumpError("Resource destination already exists; choose a new versioned library")
    temporary = Path(tempfile.mkdtemp(prefix=".nx2-resources-", dir=output.parent))
    try:
        for resource in report["resources"]:
            for item in resource["files"]:
                relative = resource["relativePath"] + "/" + item["path"]
                source_path = safe_file(root, relative)
                target = temporary / relative
                target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                with stable_file(source_path) as (source, info), target.open("xb") as sink:
                    if info.st_size != item["bytes"]:
                        raise DumpError("Resource changed before copying", code="input-changed")
                    remaining = item["bytes"]
                    while remaining:
                        chunk = source.read(min(1024 * 1024, remaining))
                        if not chunk:
                            raise DumpError("Resource became truncated while copying")
                        sink.write(chunk)
                        remaining -= len(chunk)
                target.chmod(0o600)
                file_hash(target, item["bytes"], item["sha256"])
        manifest = {"formatVersion": 1, "firmwareVersion": report["firmware_version"],
                    "sourcePlatform": source_platform, "switch2Compatibility": "unverified",
                    "resources": report["resources"]}
        (temporary / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        output.mkdir(mode=0o700)
        try:
            for entry in temporary.iterdir():
                shutil.move(str(entry), str(output / entry.name))
        except BaseException:
            shutil.rmtree(output)
            raise
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
    return {**report, "stage": "resources-imported", "library": str(output)}


def inspect_registered(library):
    metadata = load_manifest(library / "manifest.json")
    platform = metadata.get("sourcePlatform")
    if platform not in ("switch1", "switch2"):
        raise DumpError("Select a resource library registered with NX2, with its source platform")
    report = inspect_catalog(library, source_platform=platform)
    return {"schema_version": 1, "stage": "resources-registered",
            **{key: report[key] for key in ("catalog", "firmware_version", "source_platform",
                                           "content_digest", "file_count", "switch2_compatibility", "runtime_enabled")}}
