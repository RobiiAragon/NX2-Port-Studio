# SPDX-License-Identifier: GPL-3.0-or-later
"""Pinned upstream Mac exporter, separate from NX2's experimental NVN2 backend.

No game data belongs in the source checkout. Builds use an owned private scratch
directory beside the dump and remove it after success or failure. Only a small
redacted build log and receipt remain; no shader or decrypted resource cache is
silently retained. The original dump is never modified.
"""
from __future__ import annotations

import json
import os
import platform
import plistlib
import re
import signal
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from .build_guardian import GuardianBuildProcess as BuildProcess
from .formats import DumpError
from .pipeline import inspect_dump

SPEC_PATH = Path(__file__).with_name("native_runtime.json")


def specification() -> dict:
    return json.loads(SPEC_PATH.read_text())


def _git(root: Path, *arguments: str) -> str:
    result = subprocess.run(["git", "-C", str(root), *arguments], capture_output=True, text=True)
    if result.returncode:
        raise DumpError("The pinned native engine checkout is unavailable or invalid",
                        code="native-engine-invalid", hint="Prepare the native engine again in a new folder.")
    return result.stdout.strip()


def verify_engine(root: Path, spec: dict | None = None) -> dict:
    spec = spec or specification()
    root = root.expanduser()
    if root.is_symlink() or not root.is_dir():
        raise DumpError("Choose the actual prepared native engine folder", code="native-engine-invalid")
    root = root.resolve(strict=True)
    if Path(_git(root, "rev-parse", "--show-toplevel")) != root:
        raise DumpError("The engine path is not a complete checkout", code="native-engine-invalid")
    if _git(root, "rev-parse", "HEAD") != spec["commit"]:
        raise DumpError("The native engine does not match NX2's reviewed revision", code="native-engine-revision")
    if _git(root, "status", "--porcelain", "--untracked-files=no"):
        raise DumpError("The pinned engine source was modified", code="native-engine-dirty",
                        hint="Keep custom experiments separate and prepare a fresh engine for export.")
    # Python adds its script directories to sys.path. An untracked Python module
    # there could shadow a reviewed import even when tracked files are clean.
    untracked = _git(root, "ls-files", "--others").splitlines()
    if any(p.endswith((".py", ".pyc", ".so", ".dylib")) and
           p.startswith(("scripts/", "dm/tools/", "dm/gfx/tools/")) for p in untracked):
        raise DumpError("The engine contains an unreviewed import beside its tools", code="native-engine-dirty")
    for relative in ("scripts/build_ipa_from_nsp.py", "scripts/pack_dump.py", "LICENSE"):
        path = root / relative
        if path.is_symlink() or not path.is_file():
            raise DumpError("The native engine is incomplete", code="native-engine-invalid")
    return {"schema_version": 1, "stage": "native-engine-ready", "engine": str(root),
            "commit": spec["commit"], "platform": "macOS", "gameplay_verified": False}


def prepare_engine(destination: Path) -> dict:
    """Fetch only the reviewed public source commit, never a moving branch."""
    destination = destination.expanduser().absolute()
    if os.path.lexists(destination):
        return verify_engine(destination)
    parent = destination.parent.resolve(strict=True)
    spec = specification()
    staging = Path(tempfile.mkdtemp(prefix=".nx2-engine-", dir=parent))
    try:
        for command in (["git", "init", "--quiet", str(staging)],
                        ["git", "-C", str(staging), "remote", "add", "origin", spec["repository"]],
                        ["git", "-C", str(staging), "fetch", "--quiet", "--depth=1", "origin", spec["commit"]],
                        ["git", "-C", str(staging), "checkout", "--quiet", "--detach", "FETCH_HEAD"]):
            result = subprocess.run(command, stdout=subprocess.DEVNULL)
            if result.returncode:
                raise DumpError("Could not prepare the pinned native engine", code="native-engine-download")
        verify_engine(staging)
        if os.path.lexists(destination):
            raise DumpError("The engine destination appeared during preparation; it was preserved")
        staging.rename(destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return verify_engine(destination)


def inspect_native_dump(dump: Path, spec: dict | None = None) -> dict:
    """Admit the exact profiled base/update, not another title or version."""
    spec = spec or specification()
    if dump.is_symlink():
        raise DumpError("Select the original dump folder", code="native-dump-layout")
    root = dump.expanduser().resolve(strict=True)
    if not (root / "base/exefs/main.npdm").is_file():
        raise DumpError("Native export needs the game root with base/exefs and base/romfs",
                        code="native-dump-layout", hint="Choose the parent that also contains updates/ and optional dlc/.")
    base = inspect_dump(root / "base")
    title = base["metadata"]["program_id"]
    profile = spec["profiles"].get(title)
    if profile is None:
        raise DumpError("This title has no qualified Switch 2 export profile in the pinned engine",
                        code="native-title-unregistered", hint="Inspection remains available; review the pinned compatibility list.")
    updates = root / "updates"
    if (root / "base").is_symlink() or updates.is_symlink():
        raise DumpError("Dump versions must not be symbolic links", code="native-dump-layout")
    versions = sorted(int(p.name) for p in updates.iterdir() if p.is_dir() and p.name.isdecimal()) if updates.is_dir() else []
    version = max(versions, default=0)
    if version != profile["version"]:
        raise DumpError(f"This engine profile requires update {profile['version']}; the dump selects {version}",
                        code="native-version-mismatch", hint="Use the exact profiled update, with its fully patched RomFS.")
    # Version zero names base/ in the upstream packer; it is not updates/0.
    selected = root / "updates" / str(version) if version else root / "base"
    report = inspect_dump(selected)
    if report["metadata"]["program_id"] != title:
        raise DumpError("The update and base identify different games", code="native-title-mismatch")
    builds = {m["name"]: m["build_id"] for m in report["modules"]}
    if any(builds.get(name) != build for name, build in profile["modules"].items()):
        raise DumpError("The dump's executable build IDs do not match the pinned native profile",
                        code="native-build-mismatch")
    report["native_export"] = {"available": True, "platform": "macOS", "commit": spec["commit"],
                               "version": version, "dump_root": str(root), "name": profile["name"],
                               "upstream_note": profile["upstream_note"], "gameplay_verified": False}
    return report


def annotate_inspection(report: dict) -> dict:
    container = report["input"].get("dump_root")
    if container:
        try:
            report["native_export"] = inspect_native_dump(Path(container))["native_export"]
        except DumpError as error:
            if error.code not in {"native-title-unregistered", "native-version-mismatch", "native-build-mismatch"}:
                raise
            report["native_export"] = {"available": False, "reason": str(error), "gameplay_verified": False}
    else:
        report["native_export"] = {"available": False, "reason": "Select the game root containing base/ and updates/ for native export.", "gameplay_verified": False}
    return report


def _redact(line: str) -> str:
    line = line.replace(str(Path.home()), "~")
    return re.sub(r"(?i)(?:nx2\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+|gh[pousr]_[A-Za-z0-9]+)", "[redacted]", line)


def export_mac(dump: Path, engine: Path, output: Path, *, language: str = "es") -> dict:
    if sys.platform != "darwin" or platform.machine() != "arm64":
        raise DumpError("Native export requires an Apple Silicon Mac", code="native-host-platform")
    verified = verify_engine(engine)
    engine = Path(verified["engine"])
    report = inspect_native_dump(dump)
    profile = report["native_export"]
    root = Path(profile["dump_root"])
    output = output.expanduser().absolute()
    output = output.parent.resolve(strict=True) / output.name
    if output.suffix.casefold() != ".app" or os.path.lexists(output):
        raise DumpError("Choose a new .app destination; existing apps are preserved", code="native-output-exists")
    if output.is_relative_to(root) or output.is_relative_to(engine.resolve()) or output.is_relative_to(Path(__file__).resolve().parents[1]):
        raise DumpError("Keep the app destination outside game inputs and source checkouts", code="native-output-layout")
    private = root / "nx2-work"
    if private.is_symlink():
        raise DumpError("The private build folder must not be a symbolic link")
    private.mkdir(mode=0o700, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix=".native-export-", dir=private))
    log_path = scratch.with_suffix(".log")
    # A parent shell's game/profile/debug overrides must not change admission.
    environment = {k: v for k, v in os.environ.items() if not k.startswith("DM_")}
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    title = report["metadata"]["program_id"]
    starting_build = False
    cancellation_requested = None
    cancellation_signals = (signal.SIGTERM, signal.SIGINT)
    def ignore_cancellation():
        for sig in cancellation_signals:
            signal.signal(sig, signal.SIG_IGN)
    def cancel_export(sig, *_):
        nonlocal cancellation_requested
        if starting_build:
            # Capture the child's group and supervisor before a signal may
            # unwind the call assigning that ownership to this export.
            if cancellation_requested is None:
                cancellation_requested = sig
        else:
            ignore_cancellation()
            if sig == signal.SIGINT:
                raise KeyboardInterrupt
            sys.exit(128 + sig)
    previous_signals = {sig: signal.getsignal(sig) for sig in cancellation_signals}
    build = None
    process = None
    staging = None
    try:
        for sig in cancellation_signals:
            signal.signal(sig, cancel_export)
        # Upstream writes app.key inventories and app.partial beside its output.
        # Own that entire staging folder on the destination volume, so cleanup
        # can remove those products without touching an existing sibling file.
        staging = Path(tempfile.mkdtemp(prefix=".nx2-export-", dir=output.parent))
        artifact = staging / output.name
        command = [sys.executable, str(engine / "scripts/build_ipa_from_nsp.py"), str(root),
                   "--platform", "mac-app", "--out", str(artifact), "--work-dir", str(scratch),
                   "--bundle-id", f"org.nxnative.{title.lower()}", "--language", language, "--progress", "json"]
        with log_path.open("x") as log:
            starting_build = True
            try:
                build = BuildProcess(command, cwd=engine, env=environment, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, bufsize=1)
            finally:
                starting_build = False
            process = build.process
            if cancellation_requested is not None:
                cancel_export(cancellation_requested)
            assert process.stdout is not None
            for line in build.output_lines():
                clean = _redact(line)
                log.write(clean)
                print(clean, end="", flush=True)
            status = build.wait()
        if status:
            raise DumpError(f"Native export failed (status {status}); see the private build log",
                            code="native-build-failed", hint=str(log_path))
        info = plistlib.loads((artifact / "Contents/Info.plist").read_bytes())
        if info.get("CFBundleIdentifier") != f"org.nxnative.{title.lower()}":
            raise DumpError("The exported app has an unexpected identity", code="native-output-invalid")
        name = info.get("CFBundleExecutable")
        if not isinstance(name, str) or not name or Path(name).name != name:
            raise DumpError("The native app has an invalid executable name", code="native-output-invalid")
        executable = artifact / "Contents/MacOS" / name
        if not executable.is_file() or executable.is_symlink():
            raise DumpError("The native app has no valid executable", code="native-output-invalid")
        if any(p.is_symlink() and not p.resolve().is_relative_to(artifact) for p in artifact.rglob("*")):
            raise DumpError("The exported app references resources outside its bundle", code="native-output-invalid")
        signature = subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(artifact)], capture_output=True)
        if signature.returncode:
            raise DumpError("The exported Mac app's signature did not verify", code="native-signature-invalid")
        if os.path.lexists(output):
            raise DumpError("The app destination appeared during export; it was preserved", code="native-output-exists")
        artifact.rename(output)
        result = {"schema_version": 1, "stage": "native-exported", "artifact": str(output),
                  "title_id": title, "version": profile["version"], "engine_commit": verified["commit"],
                  "platform": "macOS", "signature_verified": True, "gameplay_verified": False,
                  "log": str(log_path), "temporary_build_removed": True}
    finally:
        # A repeated cancellation must not interrupt retirement of our workers.
        ignore_cancellation()
        try:
            try:
                if build is not None:
                    build.cancel()
            finally:
                if process is not None and process.stdout is not None:
                    process.stdout.close()
            # Only this operation's staging and scratch are owned here. Retire
            # all build workers before removing paths they could still write.
            try:
                if staging is not None:
                    shutil.rmtree(staging)
            finally:
                shutil.rmtree(scratch)
        finally:
            for sig, handler in previous_signals.items():
                signal.signal(sig, handler)
    log_path.with_suffix(".receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    return result
