# SPDX-License-Identifier: GPL-3.0-or-later
"""Own a build's process group independently of its inherited output pipes."""
from __future__ import annotations

import codecs
import io
import os
import select
import selectors
import signal
import subprocess
import sys
import threading


class BuildProcess:
    def __init__(self, command: list[str], **options):
        self.process = None
        self._watch = None
        self._exited = False
        self._retired = False
        self._ownership = threading.Lock()
        self._done = threading.Event()
        self._error = None
        self._wake_reader, self._wake_writer = os.pipe()
        try:
            self.process = subprocess.Popen(command, start_new_session=True, **options)
            # setsid establishes this PID as the group leader. Keep it unreaped
            # until the last group signal, preventing reuse for an unrelated group.
            self.group_id = self.process.pid
            if sys.platform == "darwin":
                self._watch = select.kqueue()
                event = select.kevent(self.group_id, filter=select.KQ_FILTER_PROC,
                                     flags=select.KQ_EV_ADD | select.KQ_EV_ONESHOT,
                                     fflags=select.KQ_NOTE_EXIT)
                try:
                    self._watch.control([event], 0, 0)
                except ProcessLookupError:
                    self._exited = True  # Already exited, still our unreaped child.
            # A compiler may inherit stdout after its packer exits. Observe the
            # leader in parallel with output consumption, without waiting for EOF.
            self._supervisor = threading.Thread(target=self._supervise, daemon=True)
            self._supervisor.start()
        except BaseException:
            try:
                if self.process is not None:
                    self.group_id = self.process.pid
                    with self._ownership:
                        self._retire_locked()
            finally:
                if self.process is not None:
                    for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
                        if stream is not None:
                            stream.close()
                if self._watch is not None:
                    self._watch.close()
                self._close_wakeup()
            raise

    def _close_wakeup(self) -> None:
        with self._ownership:
            if self._wake_reader is not None:
                os.close(self._wake_reader)
                os.close(self._wake_writer)
                self._wake_reader = self._wake_writer = None

    def _signal(self, sig: int) -> None:
        try:
            os.killpg(self.group_id, sig)
        except ProcessLookupError:
            pass
        except PermissionError:
            # Darwin reports EPERM for zombie-only groups. Confirm our pinned
            # leader is a zombie and no worker is live; real permission errors fail.
            if sys.platform != "darwin":
                raise
            result = subprocess.run(["/bin/ps", "-axo", "pid=,pgid=,stat="],
                                    capture_output=True, text=True, check=True)
            members = [(int(pid), state) for pid, group, state in
                       (line.split() for line in result.stdout.splitlines()) if int(group) == self.group_id]
            if not any(pid == self.group_id and state.startswith("Z") for pid, state in members):
                raise
            if any(not state.startswith("Z") for _, state in members):
                raise

    def _wait_for_exit(self) -> None:
        if self._exited:
            return
        if self._watch is not None:
            self._watch.control(None, 1, None)
        else:
            os.waitid(os.P_PID, self.group_id, os.WEXITED | os.WNOWAIT)
        self._exited = True

    def _retire_locked(self) -> int:
        if not self._retired:
            if self.process.returncode is None:
                self._signal(signal.SIGKILL)
                self.process.wait()
            self._retired = True
        return self.process.returncode

    def _supervise(self) -> None:
        try:
            self._wait_for_exit()
            with self._ownership:
                self._retire_locked()
        except BaseException as error:
            with self._ownership:
                # Cancellation may have collected the leader while waitid was
                # observing it. Retirement is already complete in that case.
                if not self._retired:
                    self._error = error
                    try:
                        self._retire_locked()
                    except BaseException as cleanup_error:
                        self._error = cleanup_error
        finally:
            # Only this observer touches/closes the exit watcher. A cancelling
            # caller wakes it by stopping the process, never by closing its FD.
            if self._watch is not None:
                self._watch.close()
            self._done.set()
            with self._ownership:
                if self._wake_writer is not None:
                    try:
                        os.write(self._wake_writer, b"x")
                    except OSError:
                        pass  # Output may already be exhausted.

    def output_lines(self):
        """Stream output while also observing supervision errors, without EOF waits."""
        output = self.process.stdout
        decoder = io.IncrementalNewlineDecoder(
            codecs.getincrementaldecoder(output.encoding)(errors=output.errors), translate=True)
        pending = ""
        try:
            with selectors.DefaultSelector() as ready:
                ready.register(output, selectors.EVENT_READ, "output")
                ready.register(self._wake_reader, selectors.EVENT_READ, "supervisor")
                while True:
                    for event, _ in ready.select():
                        if event.data == "supervisor":
                            os.read(self._wake_reader, 1)
                            ready.unregister(self._wake_reader)
                            if self._error is not None:
                                raise self._error
                            continue
                        chunk = os.read(output.fileno(), 65536)
                        pending += decoder.decode(chunk, final=not chunk)
                        while "\n" in pending:
                            line, pending = pending.split("\n", 1)
                            yield line + "\n"
                        if not chunk:
                            if pending:
                                yield pending
                            return
        finally:
            self._close_wakeup()

    def wait(self) -> int:
        self._supervisor.join()
        self._close_wakeup()
        if self._error is not None:
            raise self._error
        return self.process.returncode

    def cancel(self) -> int:
        with self._ownership:
            if not self._retired:
                self._signal(signal.SIGTERM)
        self._done.wait(timeout=1)
        with self._ownership:
            if not self._retired:
                self._retire_locked()
        return self.wait()
