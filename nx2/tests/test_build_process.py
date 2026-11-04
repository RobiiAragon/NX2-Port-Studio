"""Real owned process groups with original synthetic workers, on Mac and Linux."""
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

from nx2.build_process import BuildProcess


WORKER = """import json,os,pathlib,signal,sys,time
signal.signal(signal.SIGTERM,signal.SIG_IGN)
p=pathlib.Path(sys.argv[1]); temporary=p.with_suffix('.tmp')
temporary.write_text(json.dumps({'pid':os.getpid(),'group':os.getpgrp(),'leader':os.getppid(),'blocked':[int(s) for s in signal.pthread_sigmask(signal.SIG_BLOCK,[])]}))
os.replace(temporary,p)
time.sleep(60)
"""
PARENT = """import pathlib,signal,subprocess,sys,time
if sys.argv[2]=='wait':signal.signal(signal.SIGTERM,signal.SIG_IGN)
subprocess.Popen([sys.executable,'-c',sys.argv[3],sys.argv[1]],stdout=None if sys.argv[4]=='inherit' else subprocess.DEVNULL,stderr=subprocess.DEVNULL)
while not pathlib.Path(sys.argv[1]).is_file():time.sleep(.01)
if sys.argv[2]=='wait':time.sleep(60)
else:sys.exit(int(sys.argv[2]))
"""


def running(pid):
    result = subprocess.run(["ps", "-p", str(pid), "-o", "stat="], capture_output=True, text=True)
    return bool(result.stdout.strip() and not result.stdout.strip().startswith("Z"))


@unittest.skipUnless(sys.platform in {"darwin", "linux"}, "native POSIX build supervision")
class BuildProcessTests(unittest.TestCase):
    def test_supervisor_start_failure_reaps_owned_process_and_closes_captured_streams(self):
        created = []
        original = subprocess.Popen
        def capture(*args, **options):
            process = original(*args, **options)
            created.append(process)
            return process
        with patch("nx2.build_process.subprocess.Popen", side_effect=capture), \
             patch("nx2.build_process.threading.Thread.start", side_effect=RuntimeError("synthetic start failure")):
            with self.assertRaisesRegex(RuntimeError, "synthetic start failure"):
                BuildProcess([sys.executable, "-c", "import time; time.sleep(60)"],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(len(created), 1)
        self.assertIsNotNone(created[0].returncode)
        self.assertTrue(created[0].stdout.closed)
        self.assertTrue(created[0].stderr.closed)
        with self.assertRaises(ChildProcessError):
            os.waitpid(created[0].pid, os.WNOHANG)

    def test_output_lines_preserves_utf8_across_reads_and_the_last_fragment(self):
        source = "import os; os.write(1,b'A'*65535+'é'.encode()+b'\\r\\nlast')"
        build = BuildProcess([sys.executable, "-c", source], stdout=subprocess.PIPE, text=True, encoding="utf-8")
        try:
            self.assertEqual(list(build.output_lines()), ["A" * 65535 + "é\n", "last"])
            self.assertEqual(build.wait(), 0)
        finally:
            build.cancel(); build.process.stdout.close()

    def test_supervision_error_wakes_stream_consumer_before_inherited_stdout_closes(self):
        class DeniedRetirement(BuildProcess):
            allow_retirement = False
            def _signal(self, sig):
                if sig == signal.SIGKILL and not self.allow_retirement:
                    raise PermissionError("synthetic group-signal denial")
                return super()._signal(sig)
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "worker.json"
            build = DeniedRetirement([sys.executable, "-c", PARENT, str(marker), "0", WORKER, "inherit"],
                                     stdout=subprocess.PIPE, text=True)
            finished = threading.Event()
            errors = []
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
                self.assertTrue(finished.wait(5), "a supervisor failure must wake the blocked stream")
                self.assertEqual(len(errors), 1)
                self.assertIsInstance(errors[0], PermissionError)
            finally:
                build.allow_retirement = True
                with self.assertRaises(PermissionError):
                    build.cancel()  # Retire the owned group while preserving the original error.
                reader.join(timeout=5)
                self.assertFalse(reader.is_alive())
                build.process.stdout.close()

    def scenario(self, mode, *, waitid=False, inherited_stdout=False):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "worker.json"
            unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
            command = [sys.executable, "-c", PARENT, str(marker), mode, WORKER,
                       "inherit" if inherited_stdout else "private"]
            output = subprocess.PIPE if inherited_stdout else subprocess.DEVNULL
            if waitid:
                # Exercise Linux's non-reaping path with actual POSIX children
                # on hosts that also provide waitid; signaling uses the real OS.
                with patch("nx2.build_process.sys.platform", "linux"):
                    build = BuildProcess(command, stdout=output, stderr=subprocess.DEVNULL)
            else:
                build = BuildProcess(command, stdout=output, stderr=subprocess.DEVNULL)
            worker = None
            reader = None
            try:
                deadline = time.monotonic() + 5
                while not marker.is_file() and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertTrue(marker.is_file(), "the owned worker must start before cancellation")
                worker = json.loads(marker.read_text())
                self.assertEqual(worker["leader"], build.group_id)
                self.assertEqual(worker["group"], build.group_id)
                self.assertNotEqual(build.group_id, os.getpgrp())
                self.assertNotIn(int(signal.SIGINT), worker["blocked"])
                self.assertNotIn(int(signal.SIGTERM), worker["blocked"])
                if mode == "wait":
                    self.assertTrue(running(worker["pid"]))
                if inherited_stdout:
                    ended = threading.Event()
                    def read_output():
                        build.process.stdout.read()
                        ended.set()
                    reader = threading.Thread(target=read_output, daemon=True)
                    reader.start()
                    self.assertTrue(ended.wait(5), "an exited packer's worker must not hold stdout open")
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
                if reader is not None:
                    reader.join(timeout=5)
                    self.assertFalse(reader.is_alive())
                    build.process.stdout.close()
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

    def test_inherited_stdout_closes_after_success_before_caller_waits(self):
        self.scenario("0", inherited_stdout=True)

    def test_inherited_stdout_closes_after_failure_before_caller_waits(self):
        self.scenario("9", inherited_stdout=True)

    @unittest.skipUnless(hasattr(os, "waitid"), "waitid path also runs natively on Linux CI")
    def test_waitid_cancellation_retires_group_and_preserves_unrelated_process(self):
        self.scenario("wait", waitid=True)

    @unittest.skipUnless(hasattr(os, "waitid"), "waitid path also runs natively on Linux CI")
    def test_waitid_success_keeps_leader_unreaped_until_workers_stop(self):
        self.scenario("0", waitid=True)

    @unittest.skipUnless(hasattr(os, "waitid"), "waitid path also runs natively on Linux CI")
    def test_waitid_failure_preserves_exit_status_and_reaps_leader(self):
        self.scenario("9", waitid=True)
