# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise source admission using original tiny Git repositories and patches."""
import copy
import hashlib
import json
import platform
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from nx2.errors import DumpError
from nx2.engine_overlay import apply_overlay, cache_identity, load_overlay
from nx2.native_runtime import export_mac, prepare_engine, verify_engine


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def sha(data):
    return hashlib.sha256(data).hexdigest()


class EngineOverlayTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.layers = self.root / "layers"
        self.layers.mkdir()
        location = patch("nx2.engine_overlay.OVERLAY_ROOT", self.layers)
        location.start()
        self.addCleanup(location.stop)
        self.engine = self.root / "source"
        for relative, text in {"LICENSE": "Original synthetic fixture\n",
                               "scripts/build_ipa_from_nsp.py": "# Original exporter\n",
                               "scripts/pack_dump.py": "# Original packer\n",
                               "dm/host/hle_net.c": "int transport(void) { return -1; }\n"}.items():
            path = self.engine / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        git(self.engine, "init", "--quiet")
        self.commit()
        self.spec = self.layer(self.engine)

    def commit(self):
        git(self.engine, "add", ".")
        git(self.engine, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
            "commit", "--quiet", "-m", "Original synthetic source")

    def layer(self, engine):
        before = b"int transport(void) { return -1; }\n"
        after = b"int transport(void) { return 42; }\n"
        new = b"int new_transport(void);\n"
        target = "dm/host/hle_net.c"
        added = "dm/host/net_socket.h"
        changes = (f"diff --git a/{target} b/{target}\n--- a/{target}\n+++ b/{target}\n@@ -1 +1 @@\n-".encode()
                   + before + b"+" + after +
                   f"diff --git a/{added} b/{added}\nnew file mode 100644\n--- /dev/null\n+++ b/{added}\n@@ -0,0 +1 @@\n+".encode() + new)
        (self.layers / "test.patch").write_bytes(changes)
        self.manifest = {"schema_version": 1, "base_commit": git(engine, "rev-parse", "HEAD"), "local_services": ["lan", "ldn"],
                         "patch_file": "test.patch", "patch_sha256": sha(changes),
                         "files": {target: {"before": sha(before), "after": sha(after)},
                                   added: {"before": None, "after": sha(new)}}}
        spec = {"commit": self.manifest["base_commit"], "repository": str(engine)}
        self.reseal(spec)
        return spec

    def reseal(self, spec):
        content = json.dumps(self.manifest).encode()
        (self.layers / "test.json").write_bytes(content)
        spec["overlay"] = {"manifest": "test.json", "sha256": sha(content)}

    def error(self, code, call):
        with self.assertRaises(DumpError) as caught:
            call()
        self.assertEqual(caught.exception.code, code)

    def test_only_the_exact_layer_is_accepted_and_it_keeps_a_distinct_cache(self):
        self.error("native-engine-dirty", lambda: verify_engine(self.engine, self.spec))
        apply_overlay(self.engine, self.spec)
        ready = verify_engine(self.engine, self.spec)
        self.assertEqual(ready["engine_overlay"], self.spec["overlay"]["sha256"])
        self.assertEqual(ready["local_services"], ["lan", "ldn"])
        self.assertFalse(ready["gameplay_verified"])
        self.assertNotEqual(cache_identity(self.spec), cache_identity({"commit": self.spec["commit"]}))
        self.spec["cache_id"] = self.spec["commit"]
        self.error("native-engine-overlay", lambda: cache_identity(self.spec))

    def test_patch_manifest_and_base_tampering_fail_before_mutation(self):
        original = (self.engine / "dm/host/hle_net.c").read_bytes()
        content = (self.layers / "test.patch").read_bytes()
        (self.layers / "test.patch").write_bytes(content + b"\n")
        self.error("native-engine-overlay", lambda: apply_overlay(self.engine, self.spec))
        (self.layers / "test.patch").write_bytes(content)
        (self.layers / "test.json").write_text("{}")
        self.error("native-engine-overlay", lambda: apply_overlay(self.engine, self.spec))
        self.reseal(self.spec)
        source = self.engine / "dm/host/hle_net.c"
        source.write_bytes(original + b"// local experiment\n")
        self.error("native-engine-overlay-base", lambda: apply_overlay(self.engine, self.spec))
        self.assertEqual(source.read_bytes(), original + b"// local experiment\n")
        self.assertFalse((self.engine / "dm/host/net_socket.h").exists())

    def test_source_symlinks_and_scope_escapes_preserve_the_outside_canary(self):
        canary = self.root / "outside.c"
        canary.write_text("preserve\n")
        source = self.engine / "dm/host/hle_net.c"
        source.unlink()
        source.symlink_to(canary)
        self.error("native-engine-overlay", lambda: apply_overlay(self.engine, self.spec))
        self.assertEqual(canary.read_text(), "preserve\n")
        for name in ("../outside.c", "dm/host/../../../outside.c", "dm/gfx/tools/translator.py"):
            old = self.manifest["files"]
            self.manifest["files"] = {name: {"before": None, "after": "0" * 64}}
            self.reseal(self.spec)
            self.error("native-engine-overlay", lambda: load_overlay(self.spec))
            self.manifest["files"] = old

    def test_post_apply_changes_and_new_globbed_sources_are_rejected(self):
        apply_overlay(self.engine, self.spec)
        target = self.engine / "dm/host/net_socket.h"
        original = target.read_bytes()
        target.write_bytes(original + b"// changed\n")
        self.error("native-engine-dirty", lambda: verify_engine(self.engine, self.spec))
        target.write_bytes(original)
        for relative in ("dm/host/hle_injected.c", "dm/gfx/nvn/injected.inc", "dm/host/GNUmakefile", "dm/mac/shutil.py"):
            path = self.engine / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("unreviewed\n")
            self.error("native-engine-dirty", lambda: verify_engine(self.engine, self.spec))
            path.unlink()
        (self.engine / "LICENSE").write_text("changed\n")
        self.error("native-engine-dirty", lambda: verify_engine(self.engine, self.spec))

    def test_prepare_applies_only_to_a_new_owned_checkout_and_never_repairs_existing_source(self):
        destination = self.root / "prepared"
        with patch("nx2.native_runtime.specification", return_value=self.spec):
            ready = prepare_engine(destination)
            self.assertEqual(ready["engine_overlay"], self.spec["overlay"]["sha256"])
            source = destination / "dm/host/hle_net.c"
            source.write_text("preserve user experiment\n")
            self.error("native-engine-dirty", lambda: prepare_engine(destination))
            self.assertEqual(source.read_text(), "preserve user experiment\n")
        self.assertFalse(list(self.root.glob(".nx2-engine-*")))
        self.assertEqual((self.engine / "dm/host/hle_net.c").read_text(), "int transport(void) { return -1; }\n")

    def test_failed_apply_retires_only_owned_staging(self):
        self.manifest["files"]["dm/host/hle_net.c"]["before"] = "0" * 64
        self.reseal(self.spec)
        destination = self.root / "failed"
        with patch("nx2.native_runtime.specification", return_value=self.spec):
            self.error("native-engine-overlay-base", lambda: prepare_engine(destination))
        self.assertFalse(destination.exists())
        self.assertFalse(list(self.root.glob(".nx2-engine-*")))
        self.assertTrue(self.engine.is_dir())

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac signature fixture")
    def test_network_export_carries_owned_settings_and_receipt_without_claiming_multiplayer(self):
        from nx2.tests import test_native_runtime as fixtures
        case = fixtures.NativeRuntimeTests()
        case.setUp()
        self.addCleanup(case.doCleanups)
        engine, spec = case.export_engine()
        path = engine / "dm/host/hle_net.c"
        path.parent.mkdir(parents=True)
        path.write_text("int transport(void) { return -1; }\n")
        script = engine / "scripts/build_ipa_from_nsp.py"
        script.write_text(script.read_text().replace("subprocess.run(['/usr/bin/codesign'", """if '--env-file' in a:
 settings=pathlib.Path(a[a.index('--env-file')+1]); data=settings.read_text()
 assert data in {'DM_NETWORK=auto\\n','DM_NETWORK=lan\\n'} and 'nx2-work' in str(settings)
 assert not any(k.startswith('DM_') for k in __import__('os').environ)
 env=out/'Contents/Resources/dm/env.txt';env.parent.mkdir(parents=True);env.write_text(data)
 info=out/'Contents/Info.plist';plist=plistlib.loads(info.read_bytes())
 plist['NSLocalNetworkUsageDescription']='Join local rooms';info.write_bytes(plistlib.dumps(plist))
subprocess.run(['/usr/bin/codesign'"""))
        git(engine, "add", ".")
        git(engine, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "--quiet", "-m", "Original network export fixture")
        spec.update(self.layer(engine))
        apply_overlay(engine, spec)
        for services, mode in [(["lan", "ldn"], "auto"), (["lan"], "lan")]:
            spec["profiles"][case.title]["local_services"] = services
            output = case.root / f"Network-{mode}.app"
            with patch("nx2.native_runtime.specification", return_value=spec), patch.dict("os.environ", {"DM_NETWORK": "offline", "DM_GAME": "wrong"}):
                receipt = export_mac(case.game, engine, output)
            self.assertEqual(receipt["local_services"], services)
            self.assertEqual((output / "Contents/Resources/dm/env.txt").read_text(), f"DM_NETWORK={mode}\n")
            self.assertEqual(receipt["engine_overlay"], spec["overlay"]["sha256"])
            self.assertFalse(receipt["multiplayer_verified"])
        self.assertFalse(any(p.is_dir() for p in (case.game / "nx2-work").iterdir()))


if __name__ == "__main__":
    unittest.main()
