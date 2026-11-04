"""Pinned-source and exact-build admission using original synthetic modules."""
import copy
import json
import os
import platform
import signal
import sys
import time
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from nx2.errors import DumpError
from nx2.native_runtime import inspect_native_dump, verify_engine, annotate_inspection, export_mac
from nx2.pipeline import inspect_dump
from nx2.tests.fixtures import dump
from nx2.tests.test_build_guardian import denied_guardian, until, running


class NativeRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.game = self.root / "game"
        dump(self.game / "base")
        dump(self.game / "updates/458752")
        self.title = "0400C3F00006E000"
        self.spec = {"profiles": {self.title: {"version": 458752, "name": "Synthetic game",
                     "upstream_note": "docs/synthetic.md", "modules": {"main": bytes(range(32)).hex()}}},
                     "commit": "synthetic"}

    def assert_error(self, code, operation):
        with self.assertRaises(DumpError) as context:
            operation()
        self.assertEqual(context.exception.code, code)

    def test_exact_update_admission_preserves_inputs_and_does_not_claim_gameplay(self):
        before = {p: p.read_bytes() for p in self.game.rglob("*") if p.is_file()}
        result = inspect_native_dump(self.game, self.spec)
        self.assertTrue(result["native_export"]["available"])
        self.assertFalse(result["native_export"]["gameplay_verified"])
        self.assertFalse(result["runtime_ready"])
        self.assertEqual(result["native_export"]["version"], 458752)
        self.assertEqual(before, {p: p.read_bytes() for p in self.game.rglob("*") if p.is_file()})

    def test_unknown_title_and_old_update_are_not_silently_accepted(self):
        bad = copy.deepcopy(self.spec)
        bad["profiles"] = {}
        self.assert_error("native-title-unregistered", lambda: inspect_native_dump(self.game, bad))
        (self.game / "updates/458752").rename(self.game / "updates/1")
        self.assert_error("native-version-mismatch", lambda: inspect_native_dump(self.game, self.spec))

    def test_newer_unprofiled_update_never_falls_back_to_old_compatible_one(self):
        dump(self.game / "updates/524288")
        self.assert_error("native-version-mismatch", lambda: inspect_native_dump(self.game, self.spec))

    def test_same_version_with_different_executable_build_is_rejected(self):
        main = self.game / "updates/458752/exefs/main"
        data = bytearray(main.read_bytes()); data[0x40] ^= 1; main.write_bytes(data)
        self.assert_error("native-build-mismatch", lambda: inspect_native_dump(self.game, self.spec))

    def test_mixed_title_update_is_rejected(self):
        from nx2.tests.fixtures import npdm
        (self.game / "updates/458752/exefs/main.npdm").write_bytes(npdm(program_id=0x0400000000001000))
        self.assert_error("native-title-mismatch", lambda: inspect_native_dump(self.game, self.spec))

    def test_layout_and_symlink_versions_fail(self):
        self.assert_error("native-dump-layout", lambda: inspect_native_dump(self.game / "base", self.spec))
        updates = self.game / "updates"
        updates.rename(self.root / "outside")
        updates.symlink_to(self.root / "outside", target_is_directory=True)
        self.assert_error("native-dump-layout", lambda: inspect_native_dump(self.game, self.spec))
        with self.assertRaises(DumpError):
            inspect_dump(self.game)

    def test_container_inspection_selects_latest_coherent_update(self):
        report = inspect_dump(self.game)
        self.assertEqual(report["input"]["dump_root"], str(self.game.resolve()))
        self.assertIn("updates/458752/exefs", report["input"]["exefs"])
        with patch("nx2.native_runtime.specification", return_value=self.spec):
            self.assertTrue(annotate_inspection(report)["native_export"]["available"])

    def test_inspection_is_available_when_export_version_is_not(self):
        dump(self.game / "updates/524288")
        with patch("nx2.native_runtime.specification", return_value=self.spec):
            report = annotate_inspection(inspect_dump(self.game))
        self.assertFalse(report["native_export"]["available"])
        self.assertEqual(report["stage"], "validated-input")

    def engine(self):
        engine = self.root / "engine"
        (engine / "scripts").mkdir(parents=True)
        for name in ("build_ipa_from_nsp.py", "pack_dump.py"):
            (engine / "scripts" / name).write_text("# Synthetic reviewed source\n")
        (engine / "LICENSE").write_text("Original synthetic fixture\n")
        for args in (["init", "--quiet"], ["add", "."], ["-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "--quiet", "-m", "Synthetic source"]):
            subprocess.run(["git", "-C", str(engine), *args], check=True, stdout=subprocess.DEVNULL)
        commit = subprocess.check_output(["git", "-C", str(engine), "rev-parse", "HEAD"], text=True).strip()
        return engine, {"commit": commit}

    def test_clean_pinned_checkout_is_accepted_and_generated_cache_is_harmless(self):
        engine, spec = self.engine()
        (engine / "generated").mkdir()
        (engine / "generated/cache.json").write_text("{}")
        self.assertEqual(verify_engine(engine, spec)["commit"], spec["commit"])

    def test_wrong_revision_modified_source_and_import_shadow_are_rejected(self):
        engine, spec = self.engine()
        self.assert_error("native-engine-revision", lambda: verify_engine(engine, {"commit": "0" * 40}))
        tool = engine / "scripts/pack_dump.py"
        old = tool.read_bytes(); tool.write_text("# Modified\n")
        self.assert_error("native-engine-dirty", lambda: verify_engine(engine, spec))
        tool.write_bytes(old)
        (engine / "scripts/shutil.py").write_text("raise RuntimeError('unreviewed')")
        self.assert_error("native-engine-dirty", lambda: verify_engine(engine, spec))

    def test_checkout_subdirectory_is_not_a_complete_engine(self):
        engine, spec = self.engine()
        self.assert_error("native-engine-invalid", lambda: verify_engine(engine / "scripts", spec))

    def export_engine(self):
        engine, _ = self.engine()
        script = engine / "scripts/build_ipa_from_nsp.py"
        script.write_text('''import sys, pathlib, shutil, plistlib, subprocess, time, signal
a=sys.argv
out=pathlib.Path(a[a.index('--out')+1]);work=pathlib.Path(a[a.index('--work-dir')+1])
(work/'owned-build-data').write_text('synthetic scratch')
out.with_name(out.name+'.key').write_text('synthetic app inventory')
out.with_name(out.name+'.partial').mkdir()
mode=a[a.index('--language')+1]
if mode=='failure':sys.exit(9)
if mode in ('cancel','orphan-success','orphan-failure'):
 if mode=='cancel':signal.signal(signal.SIGTERM,signal.SIG_IGN)
 worker="import json,os,pathlib,signal,sys,time; p=pathlib.Path(sys.argv[1]); signal.signal(signal.SIGTERM,lambda *_:p.with_suffix('.term').write_text('ignored')); t=p.with_suffix('.tmp'); t.write_text(json.dumps({'pid':os.getpid(),'group':os.getpgrp(),'leader':os.getppid()})); os.replace(t,p); time.sleep(60)"
 marker=pathlib.Path(__file__).resolve().parents[2]/'worker.json'
 subprocess.Popen([sys.executable,'-c',worker,str(marker)],stdout=subprocess.DEVNULL if mode=='cancel' else None,stderr=subprocess.DEVNULL)
 while not marker.is_file():time.sleep(.01)
 if mode=='cancel':
  print('fixture ready',flush=True)
  time.sleep(60)
 if mode=='orphan-failure':sys.exit(9)
out.joinpath('Contents/MacOS').mkdir(parents=True)
out.joinpath('Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier':'org.nxnative.0400c3f00006e000','CFBundleExecutable':'Fixture','CFBundlePackageType':'APPL'}))
shutil.copyfile('/usr/bin/true',out/'Contents/MacOS/Fixture')
(out/'Contents/MacOS/Fixture').chmod(0o755)
subprocess.run(['/usr/bin/codesign','--force','--sign','-',str(out)],check=True)
''')
        subprocess.run(["git", "-C", str(engine), "add", "."], check=True)
        subprocess.run(["git", "-C", str(engine), "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                        "commit", "--quiet", "-m", "Synthetic exporter"], check=True)
        spec = copy.deepcopy(self.spec)
        spec["commit"] = subprocess.check_output(["git", "-C", str(engine), "rev-parse", "HEAD"], text=True).strip()
        return engine, spec

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac signature fixture")
    def test_verified_app_and_failure_both_retire_only_their_owned_scratch(self):
        engine, spec = self.export_engine()
        output = self.root / "Synthetic.app"
        sibling = output.with_name(output.name + ".key")
        sibling.write_text("preserve unrelated existing inventory")
        with patch("nx2.native_runtime.specification", return_value=spec):
            result = export_mac(self.game, engine, output)
            self.assertTrue(result["signature_verified"])
            self.assertFalse(result["gameplay_verified"])
            self.assertTrue(output.is_dir())
            self.assert_error("native-output-exists", lambda: export_mac(self.game, engine, output))
            self.assert_error("native-build-failed", lambda: export_mac(self.game, engine, self.root / "Failure.app", language="failure"))
        self.assertFalse(any(p.is_dir() for p in (self.game / "nx2-work").iterdir()))
        self.assertTrue((self.game / "updates/458752/exefs/main").is_file())
        self.assertTrue(engine.is_dir())
        self.assertEqual(sibling.read_text(), "preserve unrelated existing inventory")
        self.assertFalse(list(self.root.glob(".nx2-export-*")))
        self.assertFalse((self.root / "Failure.app.key").exists())
        self.assertFalse((self.root / "Failure.app.partial").exists())

    def cancelled_export(self, requested_signal):
        engine, spec = self.export_engine()
        driver = self.root / "driver.py"
        restored = self.root / "handlers-restored.txt"
        driver.write_text(f'''import signal,sys
from pathlib import Path
sys.path.insert(0,{str(Path(__file__).resolve().parents[2])!r})
import nx2.native_runtime as runtime
runtime.specification=lambda:{spec!r}
previous={{s:signal.getsignal(s) for s in (signal.SIGTERM,signal.SIGINT)}}
try:
 try:
  runtime.export_mac(Path({str(self.game)!r}),Path({str(engine)!r}),Path({str(self.root / 'Cancelled.app')!r}),language='cancel')
 except KeyboardInterrupt:
  sys.exit(130)
finally:
 Path({str(restored)!r}).write_text(str(all(signal.getsignal(s)==h for s,h in previous.items())))
''')
        process = subprocess.Popen([sys.executable, str(driver)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        worker = None
        try:
            deadline = time.monotonic() + 5
            private = self.game / "nx2-work"
            marker = self.root / "worker.json"
            while time.monotonic() < deadline and not marker.is_file():
                time.sleep(.01)
            self.assertTrue(marker.is_file())
            worker = json.loads(marker.read_text())
            self.assertTrue(list(private.glob(".native-export-*/owned-build-data")))
            process.send_signal(requested_signal)
            deadline = time.monotonic() + 2
            while not marker.with_suffix(".term").exists() and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertTrue(marker.with_suffix(".term").exists(), "the worker must receive group termination")
            # Cancellation remains in its existing one-second grace interval:
            # a second request must not interrupt owned-worker cleanup.
            process.send_signal(signal.SIGTERM)
            process.send_signal(signal.SIGINT)
            _, error = process.communicate(timeout=5)
            self.assertEqual(process.returncode, 128 + requested_signal, error.decode())
            self.assertEqual(restored.read_text(), "True")
            state = subprocess.run(["ps", "-p", str(worker["pid"]), "-o", "stat="], capture_output=True, text=True)
            self.assertFalse(state.stdout.strip() and not state.stdout.strip().startswith("Z"),
                             "the cancelled export left its build worker running")
            self.assertEqual(worker["group"], worker["leader"])
            self.assertIsNone(unrelated.poll(), "cancelling one export must preserve unrelated processes")
            self.assertFalse(any(p.is_dir() for p in private.iterdir()))
            self.assertFalse(list(private.glob("*.receipt.json")))
            self.assertFalse(list(self.root.glob(".nx2-export-*")))
            self.assertFalse((self.root / "Cancelled.app.key").exists())
        finally:
            if worker is not None:
                try:
                    if os.getpgid(worker["pid"]) == worker["group"]:
                        os.kill(worker["pid"], signal.SIGKILL)
                except ProcessLookupError:
                    pass
            unrelated.terminate(); unrelated.wait(timeout=5)
            if process.poll() is None:
                process.kill(); process.communicate(timeout=5)

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac process fixture")
    def test_termination_retires_child_and_scratch_without_a_success_receipt(self):
        self.cancelled_export(signal.SIGTERM)

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac process fixture")
    def test_keyboard_interrupt_retires_group_and_restores_handlers(self):
        self.cancelled_export(signal.SIGINT)

    def export_with_inherited_output(self, failure, *, interrupt_close=False, interrupt_result=False):
        engine, spec = self.export_engine()
        output = self.root / "Inherited.app"
        driver = self.root / "driver.py"
        restored = self.root / "handlers-restored.txt"
        foreign = self.root / "foreign-fd.txt"
        mode = "orphan-failure" if failure else "orphan-success"
        hook = f'''foreign_fd=None
fired=False
real_close=os.close
real_read=os.read
original=runtime.BuildProcess
def register(*args,**options):
 build=original(*args,**options)
 target=build._result_reader
 def close(fd):
  global foreign_fd,fired
  real_close(fd)
  if fd==target and not fired:
   fired=True
   opened=os.open({str(foreign)!r},os.O_CREAT|os.O_WRONLY,0o600)
   if opened!=target:
    os.dup2(opened,target)
    real_close(opened)
   foreign_fd=target
   os.kill(os.getpid(),signal.SIGINT)
 def read(fd,size):
  global fired
  data=real_read(fd,size)
  if fd==target and not fired:
   fired=True
   os.kill(os.getpid(),signal.SIGINT)
  return data
 if {interrupt_close!r}:os.close=close
 else:os.read=read
 return build
runtime.BuildProcess=register
''' if interrupt_close or interrupt_result else "foreign_fd=None\nfired=False\n"
        driver.write_text(f'''import os,signal,sys
from pathlib import Path
sys.path.insert(0,{str(Path(__file__).resolve().parents[2])!r})
import nx2.native_runtime as runtime
runtime.specification=lambda:{spec!r}
{hook}
previous={{s:signal.getsignal(s) for s in (signal.SIGTERM,signal.SIGINT)}}
try:
 try:
  runtime.export_mac(Path({str(self.game)!r}),Path({str(engine)!r}),Path({str(output)!r}),language={mode!r})
 except runtime.DumpError as error:
  print(error.code,flush=True)
  sys.exit(9)
 except KeyboardInterrupt:
  if foreign_fd is not None:os.write(foreign_fd,b'alive')
  sys.exit(130)
finally:
 Path({str(restored)!r}).write_text(str(all(signal.getsignal(s)==h for s,h in previous.items())))
 if foreign_fd is not None:os.close(foreign_fd)
''')
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        process = subprocess.Popen([sys.executable, str(driver)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            stdout, error = process.communicate(timeout=5)
            interrupted = interrupt_close or interrupt_result
            self.assertEqual(process.returncode, 130 if interrupted else 9 if failure else 0, error.decode())
            self.assertEqual(restored.read_text(), "True")
            if interrupt_close:
                self.assertEqual(foreign.read_text(), "alive", "retry must not close a recycled descriptor")
            if failure:
                self.assertIn(b"native-build-failed", stdout)
            worker = json.loads((self.root / "worker.json").read_text())
            state = subprocess.run(["ps", "-p", str(worker["pid"]), "-o", "stat="], capture_output=True, text=True)
            self.assertFalse(state.stdout.strip() and not state.stdout.strip().startswith("Z"))
            self.assertEqual(worker["group"], worker["leader"])
            self.assertIsNone(unrelated.poll())
            self.assertEqual(output.is_dir(), not failure and not interrupted)
            private = self.game / "nx2-work"
            self.assertFalse(any(p.is_dir() for p in private.iterdir()))
            self.assertEqual(bool(list(private.glob("*.receipt.json"))), not failure and not interrupted)
            self.assertFalse(list(self.root.glob(".nx2-export-*")))
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                process.communicate(timeout=5)
            unrelated.terminate(); unrelated.wait(timeout=5)

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac signature fixture")
    def test_successful_packer_with_inherited_stdout_worker_completes_and_retires_scratch(self):
        self.export_with_inherited_output(False)

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac process fixture")
    def test_failed_packer_with_inherited_stdout_worker_reports_failure_and_retires_scratch(self):
        self.export_with_inherited_output(True)

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac process fixture")
    def test_interrupt_after_reader_close_preserves_recycled_fd_and_cleans_workspace(self):
        self.export_with_inherited_output(False, interrupt_close=True)

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac process fixture")
    def test_interrupt_after_reading_guardian_result_preserves_interrupt_and_cleanup(self):
        self.export_with_inherited_output(False, interrupt_result=True)

    def cancelled_registration(self, requested_signal):
        engine, spec = self.export_engine()
        owner = self.root / "owner.json"
        driver = self.root / "driver.py"
        restored = self.root / "handlers-restored.txt"
        driver.write_text(f'''import json,os,signal,sys
from pathlib import Path
sys.path.insert(0,{str(Path(__file__).resolve().parents[2])!r})
import nx2.native_runtime as runtime
runtime.specification=lambda:{spec!r}
original=runtime.BuildProcess
def register(*args,**options):
 build=original(*args,**options)
 Path({str(owner)!r}).write_text(json.dumps({{'group':build.group_id}}))
 os.kill(os.getpid(),{int(requested_signal)})
 os.kill(os.getpid(),{int(requested_signal)})
 return build
runtime.BuildProcess=register
previous={{s:signal.getsignal(s) for s in (signal.SIGTERM,signal.SIGINT)}}
try:
 try:
  runtime.export_mac(Path({str(self.game)!r}),Path({str(engine)!r}),Path({str(self.root / 'Registration.app')!r}),language='cancel')
 except KeyboardInterrupt:
  sys.exit(130)
finally:
 Path({str(restored)!r}).write_text(str(all(signal.getsignal(s)==h for s,h in previous.items())))
''')
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        process = subprocess.Popen([sys.executable, str(driver)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            _, error = process.communicate(timeout=5)
            self.assertEqual(process.returncode, 128 + requested_signal, error.decode())
            self.assertEqual(restored.read_text(), "True")
            group = json.loads(owner.read_text())["group"]
            states = subprocess.run(["ps", "-axo", "pgid=,stat="], capture_output=True, text=True, check=True)
            self.assertFalse(any(int(pgid) == group and not state.startswith("Z")
                                 for pgid, state in (line.split() for line in states.stdout.splitlines())))
            self.assertIsNone(unrelated.poll())
            self.assertFalse(any(p.is_dir() for p in (self.game / "nx2-work").iterdir()))
            self.assertFalse(list(self.root.glob(".nx2-export-*")))
            self.assertFalse((self.root / "Registration.app").exists())
        finally:
            if owner.is_file():
                group = json.loads(owner.read_text())["group"]
                try:
                    if os.getpgid(group) == group:
                        os.killpg(group, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            if process.poll() is None:
                process.kill(); process.communicate(timeout=5)
            unrelated.terminate(); unrelated.wait(timeout=5)

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac process fixture")
    def test_cancellation_during_group_registration_cannot_lose_build_ownership(self):
        self.cancelled_registration(signal.SIGTERM)

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac process fixture")
    def test_keyboard_interrupt_during_group_registration_cannot_lose_build_ownership(self):
        self.cancelled_registration(signal.SIGINT)

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac signature fixture")
    def test_unverified_worker_retirement_preserves_staging_and_private_workspace(self):
        engine, spec = self.export_engine()
        wrapper = denied_guardian(self.root / "denied.py")
        with patch("nx2.native_runtime.specification", return_value=spec), \
             patch("nx2.build_guardian.__file__", str(wrapper)):
            with self.assertRaisesRegex(OSError, "could not be retired"):
                export_mac(self.game, engine, self.root / "Unverified.app")
        self.assertFalse((self.root / "Unverified.app").exists())
        self.assertTrue(list(self.root.glob(".nx2-export-*")))
        private = self.game / "nx2-work"
        self.assertTrue(any(path.is_dir() for path in private.iterdir()))
        self.assertFalse(list(private.glob("*.receipt.json")))

    @unittest.skipUnless(sys.platform == "darwin" and platform.machine() == "arm64", "Mac process fixture")
    def test_gui_immediate_group_kill_does_not_orphan_native_export_workers(self):
        engine, spec = self.export_engine()
        driver = self.root / "owner.py"
        driver.write_text(f'''import sys
from pathlib import Path
sys.path.insert(0,{str(Path(__file__).resolve().parents[2])!r})
import nx2.native_runtime as runtime
runtime.specification=lambda:{spec!r}
runtime.export_mac(Path({str(self.game)!r}),Path({str(engine)!r}),Path({str(self.root / 'Killed.app')!r}),language='cancel')
''')
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        outer = subprocess.Popen([sys.executable, str(driver)], start_new_session=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        worker = None
        try:
            marker = self.root / "worker.json"
            self.assertTrue(until(marker.is_file))
            worker = json.loads(marker.read_text())
            guardian = int(subprocess.check_output(["ps", "-p", str(worker["leader"]), "-o", "ppid="], text=True))
            self.assertNotEqual(guardian, outer.pid)
            self.assertNotEqual(worker["group"], guardian)
            self.assertEqual(os.getpgid(outer.pid), outer.pid)
            os.killpg(outer.pid, signal.SIGKILL)
            _, error = outer.communicate(timeout=5)
            self.assertEqual(outer.returncode, -signal.SIGKILL, error.decode())
            self.assertTrue(until(lambda: not running(worker["pid"])))
            self.assertFalse(running(worker["leader"]))
            self.assertTrue(until(lambda: not running(guardian)))
            self.assertIsNone(unrelated.poll())
            self.assertFalse((self.root / "Killed.app").exists())
            self.assertFalse(list((self.game / "nx2-work").glob("*.receipt.json")))
        finally:
            if worker is not None:
                try:
                    if os.getpgid(worker["pid"]) == worker["group"]:
                        os.killpg(worker["group"], signal.SIGKILL)
                except ProcessLookupError:
                    pass
            if outer.poll() is None:
                os.killpg(outer.pid, signal.SIGKILL); outer.communicate(timeout=5)
            unrelated.terminate(); unrelated.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
