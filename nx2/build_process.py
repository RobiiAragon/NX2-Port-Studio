# SPDX-License-Identifier: GPL-3.0-or-later
"""Own a build's process group until every worker has been stopped."""
from __future__ import annotations

import os
import select
import signal
import subprocess
import sys
import time


class BuildProcess:
    def __init__(self, command: list[str], **options):
        self.process = subprocess.Popen(command, start_new_session=True, **options)
        # setsid in the child establishes this exact PID as its group leader.
        # Keep it unreaped until the last group signal, so the identifier cannot
        # be recycled for an unrelated process group during cleanup.
        self.group_id = self.process.pid
        self._watch = None
        self._exited = False
        self._retired = False
        try:
            if sys.platform == "darwin":
                self._watch = select.kqueue()
                event = select.kevent(self.group_id, filter=select.KQ_FILTER_PROC,
                                     flags=select.KQ_EV_ADD | select.KQ_EV_ONESHOT,
                                     fflags=select.KQ_NOTE_EXIT)
                try:
                    self._watch.control([event], 0, 0)
                except ProcessLookupError:
                    self._exited = True  # Already exited, still our unreaped child.
        except BaseException:
            self._retire()
            raise

    def _signal(self, sig: int) -> None:
        try:
            os.killpg(self.group_id, sig)
        except ProcessLookupError:
            pass
        except PermissionError:
            # Darwin reports EPERM for a group containing only zombies. Keep
            # the leader unreaped and confirm there is no live worker before
            # accepting that result; a genuine permission failure still fails.
            if sys.platform != "darwin" or not self._exited:
                raise
            states = subprocess.run(["/bin/ps", "-axo", "pgid=,stat="],
                                    capture_output=True, text=True, check=True)
            if any(int(group) == self.group_id and not state.startswith("Z")
                   for group, state in (line.split() for line in states.stdout.splitlines())):
                raise

    def _wait_for_exit(self, timeout: float | None = None) -> None:
        if self._exited:
            return
        if self._watch is not None:
            self._exited = bool(self._watch.control(None, 1, timeout))
        elif timeout is None:
            os.waitid(os.P_PID, self.group_id, os.WEXITED | os.WNOWAIT)
            self._exited = True
        else:
            deadline = time.monotonic() + timeout
            while not self._exited:
                self._exited = os.waitid(os.P_PID, self.group_id,
                                         os.WEXITED | os.WNOWAIT | os.WNOHANG) is not None
                remaining = deadline - time.monotonic()
                if self._exited or remaining <= 0:
                    break
                time.sleep(min(.01, remaining))

    def _retire(self) -> int:
        if not self._retired:
            if self.process.returncode is None:
                self._signal(signal.SIGKILL)
                self.process.wait()
            self._retired = True
            if self._watch is not None:
                self._watch.close()
        return self.process.returncode

    def wait(self) -> int:
        if self.process.returncode is not None:
            return self._retire()
        if not self._retired:
            self._wait_for_exit()
        return self._retire()

    def cancel(self) -> int:
        if not self._retired:
            if self.process.returncode is not None:
                return self._retire()
            try:
                self._signal(signal.SIGTERM)
                self._wait_for_exit(timeout=1)
            finally:
                self._retire()
        return self.process.returncode
