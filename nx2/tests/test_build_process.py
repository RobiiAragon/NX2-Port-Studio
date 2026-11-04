"""Real owned process groups with original synthetic workers, on Mac and Linux."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from nx2.build_process import BuildProcess


WORKER = """import json,os,pathlib,signal,sys,time
signal.signal(signal.SIGTERM,signal.SIG_IGN)
p=pathlib.Path(sys.argv[1]); temporary=p.with_suffix('.tmp')
temporary.write_text(json.dumps({'pid':os.getpid(),'group':os.getpgrp(),'leader':os.getppid()}))
os.replace(temporary,p)
time.sleep(60)
"""
PARENT = """import pathlib,signal,subprocess,sys,time
if sys.argv[2]=='wait':signal.signal(signal.SIGTERM,signal.SIG_IGN)
subprocess.Popen([sys.executable,'-c',sys.argv[3],sys.argv[1]],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
while not pathlib.Path(sys.argv[1]).is_file():time.sleep(.01)
if sys.argv[2]=='wait':time.sleep(60)
else:sys.exit(int(sys.argv[2]))
"""


def running(pid):
    result = subprocess.run(["ps", "-p", str(pid), "-o", "stat="], capture_output=True, text=True)
    return bool(result.stdout.strip() and not result.stdout.strip().startswith("Z"))


@unittest.skipUnless(sys.platform in {"darwin", "linux"}, "native POSIX build supervision")
class BuildProcessTests(unittest.TestCase):
    def scenario(self, mode, *, waitid=False):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "worker.json"
            unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
            command = [sys.executable, "-c", PARENT, str(marker), mode, WORKER]
            if waitid:
                # Exercise Linux's non-reaping path with actual POSIX children
                # on hosts that also provide waitid; signaling uses the real OS.
                with patch("nx2.build_process.sys.platform", "linux"):
                    build = BuildProcess(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                build = BuildProcess(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            worker = None
            try:
                deadline = time.monotonic() + 5
                while not marker.is_file() and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertTrue(marker.is_file(), "the owned worker must start before cancellation")
                worker = json.loads(marker.read_text())
                self.assertEqual(worker["leader"], build.group_id)
                self.assertEqual(worker["group"], build.group_id)
                self.assertNotEqual(build.group_id, os.getpgrp())
                self.assertTrue(running(worker["pid"]))
                if mode == "wait":
                    self.assertEqual(build.cancel(), -signal.SIGKILL)
                else:
                    self.assertEqual(build.wait(), int(mode))
                self.assertFalse(running(worker["pid"]), "build workers must stop before retirement returns")
                self.assertIsNotNone(build.process.returncode, "the group leader must be reaped")
                with self.assertRaises(ChildProcessError):
                    os.waitpid(build.group_id, os.WNOHANG)
                self.assertIsNone(unrelated.poll())
                self.assertEqual(build.cancel(), build.process.returncode)  # No repeated group signal after reap.
                self.assertEqual(build.wait(), build.process.returncode)
                self.assertIsNone(unrelated.poll())
            finally:
                build.cancel()
                if worker is not None:
                    try:
                        if os.getpgid(worker["pid"]) == worker["group"]:
                            os.kill(worker["pid"], signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                unrelated.terminate(); unrelated.wait(timeout=5)

    def test_cancel_stops_sigterm_resistant_parent_and_worker_but_preserves_unrelated_process(self):
        self.scenario("wait")

    def test_success_retires_worker_before_reaping_the_exited_leader(self):
        self.scenario("0")

    def test_failure_retires_worker_and_preserves_exit_status(self):
        self.scenario("9")

    @unittest.skipUnless(hasattr(os, "waitid"), "waitid path also runs natively on Linux CI")
    def test_waitid_cancellation_retires_group_and_preserves_unrelated_process(self):
        self.scenario("wait", waitid=True)

    @unittest.skipUnless(hasattr(os, "waitid"), "waitid path also runs natively on Linux CI")
    def test_waitid_success_keeps_leader_unreaped_until_workers_stop(self):
        self.scenario("0", waitid=True)

    @unittest.skipUnless(hasattr(os, "waitid"), "waitid path also runs natively on Linux CI")
    def test_waitid_failure_preserves_exit_status_and_reaps_leader(self):
        self.scenario("9", waitid=True)
