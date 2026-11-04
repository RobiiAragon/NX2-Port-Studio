"""Real owner-death leases, including the GUI's immediate outer-group kill."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from nx2.build_guardian import GuardianBuildProcess
from nx2.tests.test_build_process import PARENT, WORKER, running

REPOSITORY = Path(__file__).resolve().parents[2]


def denied_guardian(path):
    path.write_text(f'''import signal,sys
sys.path.insert(0,{str(REPOSITORY)!r})
import nx2.build_guardian as guardian
class Denied(guardian.BuildProcess):
 def _signal(self,sig):
  if sig==signal.SIGKILL:raise PermissionError('synthetic retirement denial')
  return super()._signal(sig)
guardian.BuildProcess=Denied
raise SystemExit(guardian._guard(int(sys.argv[1]),int(sys.argv[2]),sys.argv[4:]))
''')
    return path


def until(predicate):
    deadline = time.monotonic() + 5
    while not predicate() and time.monotonic() < deadline:
        time.sleep(.01)
    return predicate()


def kill_owned_group(pid):
    try:
        if os.getpgid(pid) == pid:
            os.killpg(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


@unittest.skipUnless(sys.platform in {"darwin", "linux"}, "native POSIX guardian")
class GuardianTests(unittest.TestCase):
    def test_retirement_failure_arrives_before_inherited_stdout_eof(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            marker = root / "worker.json"
            wrapper = denied_guardian(root / "denied.py")
            with patch("nx2.build_guardian.__file__", str(wrapper)):
                build = GuardianBuildProcess([sys.executable, "-c", PARENT, str(marker), "0", WORKER, "inherit"],
                                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            finished, errors = threading.Event(), []
            def consume():
                try:
                    list(build.output_lines())
                except BaseException as error:
                    errors.append(error)
                finally:
                    finished.set()
            reader = threading.Thread(target=consume, daemon=True)
            reader.start()
            try:
                self.assertTrue(finished.wait(5), "retirement failure must not wait for worker stdout EOF")
                self.assertEqual(len(errors), 1)
                self.assertIsInstance(errors[0], OSError)
                self.assertIn("could not be retired", str(errors[0]))
                self.assertTrue(running(json.loads(marker.read_text())["pid"]))
                with self.assertRaisesRegex(OSError, "could not be retired"):
                    build.cancel()
            finally:
                if marker.is_file():
                    worker = json.loads(marker.read_text())
                    try:
                        if os.getpgid(worker["pid"]) == worker["group"]:
                            os.killpg(worker["group"], signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                reader.join(timeout=5)
                build.process.stdout.close()

    def scenario(self, mode):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "worker.json"
            unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
            build = GuardianBuildProcess([sys.executable, "-c", PARENT, str(marker), mode, WORKER, "inherit"],
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            reader = None
            try:
                self.assertTrue(until(marker.is_file))
                worker = json.loads(marker.read_text())
                self.assertNotEqual(worker["group"], build.group_id)
                self.assertNotEqual(worker["group"], os.getpgrp())
                self.assertEqual(worker["group"], worker["leader"])
                self.assertNotIn(int(signal.SIGINT), worker["blocked"])
                self.assertNotIn(int(signal.SIGTERM), worker["blocked"])
                if mode == "wait":
                    self.assertEqual(build.cancel(), -signal.SIGKILL)
                else:
                    ended, errors = threading.Event(), []
                    def consume():
                        try:
                            list(build.output_lines())
                        except BaseException as error:
                            errors.append(error)
                        finally:
                            ended.set()
                    reader = threading.Thread(target=consume, daemon=True)
                    reader.start()
                    self.assertTrue(ended.wait(5), "inherited stdout must end after leader exit")
                    self.assertFalse(errors)
                    self.assertEqual(build.wait(), int(mode))
                self.assertFalse(running(worker["pid"]))
                self.assertFalse(running(worker["leader"]))
                self.assertIsNone(unrelated.poll())
                self.assertEqual(build.cancel(), -signal.SIGKILL if mode == "wait" else int(mode))
            finally:
                build.cancel()
                if reader is not None:
                    reader.join(timeout=5)
                build.process.stdout.close()
                unrelated.terminate(); unrelated.wait(timeout=5)

    def test_success_retire_workers_that_inherit_guardian_stdout(self):
        self.scenario("0")

    def test_failure_preserves_code_and_retires_inherited_stdout_workers(self):
        self.scenario("9")

    def test_cooperative_cancel_uses_existing_grace_and_preserves_unrelated_process(self):
        self.scenario("wait")

    def owner_death(self, *, during_spawn=False, cooperative=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            marker, owner = root / "worker.json", root / "owner.json"
            worker = WORKER.replace("signal.signal(signal.SIGTERM,signal.SIG_IGN)",
                                    "signal.signal(signal.SIGTERM,lambda *_:p.with_suffix('.term').write_text('term'))")
            driver = root / "owner.py"
            driver.write_text(f'''import json,os,pathlib,signal,subprocess,sys
+sys.path.insert(0,{str(REPOSITORY)!r})
+from nx2.build_guardian import GuardianBuildProcess
+original=subprocess.Popen
+def launch(*args,**options):
+ process=original(*args,**options)
+ target=pathlib.Path({str(owner)!r});temporary=target.with_suffix('.tmp')
+ temporary.write_text(json.dumps({{'guardian':process.pid}}));os.replace(temporary,target)
+ if {during_spawn!r}:os.kill(os.getpid(),signal.SIGKILL)
+ return process
+subprocess.Popen=launch
+build=GuardianBuildProcess([sys.executable,'-c',{PARENT!r},{str(marker)!r},'wait',{worker!r},'inherit'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
+signal.signal(signal.SIGUSR1,lambda *_:build.cancel())
+list(build.output_lines())
+build.wait()
+'''.replace('\n+', '\n'))
            unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
            outer = subprocess.Popen([sys.executable, str(driver)], start_new_session=True,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            guardian_pid = None
            job = None
            try:
                self.assertTrue(until(owner.is_file))
                guardian_pid = json.loads(owner.read_text())["guardian"]
                if not during_spawn:
                    self.assertTrue(until(marker.is_file))
                    job = json.loads(marker.read_text())
                    self.assertEqual(os.getpgid(guardian_pid), guardian_pid)
                    self.assertNotEqual(guardian_pid, outer.pid)
                    self.assertNotEqual(job["group"], guardian_pid)
                    if cooperative:
                        outer.send_signal(signal.SIGUSR1)
                        self.assertTrue(until(marker.with_suffix('.term').is_file))
                    # Exact ProcessRunner.cancelImmediately policy: kill the
                    # exporter/CLI group, not a discovered list of descendants.
                    os.killpg(outer.pid, signal.SIGKILL)
                _, error = outer.communicate(timeout=5)
                self.assertEqual(outer.returncode, -signal.SIGKILL, error.decode())
                self.assertTrue(until(lambda: not running(guardian_pid)), "guardian must retire after owner lease EOF")
                if marker.is_file():
                    job = json.loads(marker.read_text())
                    self.assertTrue(until(lambda: not running(job["pid"])))
                    self.assertFalse(running(job["leader"]))
                self.assertIsNone(unrelated.poll())
            finally:
                if outer.poll() is None:
                    kill_owned_group(outer.pid); outer.communicate(timeout=5)
                if job is not None:
                    kill_owned_group(job["leader"])
                if guardian_pid is not None:
                    kill_owned_group(guardian_pid)
                unrelated.terminate(); unrelated.wait(timeout=5)

    def test_outer_group_sigkill_retires_guardian_job_and_workers(self):
        self.owner_death()

    def test_owner_death_during_guardian_spawn_does_not_orphan_a_job(self):
        self.owner_death(during_spawn=True)

    def test_owner_death_during_cooperative_grace_still_forces_retirement(self):
        self.owner_death(cooperative=True)
