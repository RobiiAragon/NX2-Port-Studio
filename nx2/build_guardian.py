# SPDX-License-Identifier: GPL-3.0-or-later
"""A build owner that survives the GUI command group's immediate termination."""
from __future__ import annotations

import codecs
from contextlib import ExitStack, contextmanager
import io
import os
from pathlib import Path
import select
import selectors
import signal
import struct
import subprocess
import sys
import threading

# This file is also a standalone guardian entry point, outside the engine cwd.
if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nx2.build_process import BuildProcess

_RESULT = struct.Struct("!i?")


@contextmanager
def _defer_signals():
    pending = []
    previous = ({sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
                if threading.current_thread() is threading.main_thread() else {})
    def capture(sig, frame):
        pending.append((sig, frame))
    try:
        for sig in previous:
            signal.signal(sig, capture)
        yield
    finally:
        with ExitStack() as restore:
            for sig, handler in previous.items():
                restore.callback(signal.signal, sig, handler)
    # Result/descriptor ownership is committed before an original handler can
    # raise. Do not cache KeyboardInterrupt as a permanent protocol failure.
    for sig, frame in pending:
        handler = previous[sig]
        if callable(handler):
            handler(sig, frame)
        elif handler == signal.SIG_DFL:
            os.kill(os.getpid(), sig)


class GuardianBuildProcess:
    def __init__(self, command: list[str], **options):
        self.process = None
        self._lease_writer = self._result_reader = None
        self._exit_code = None
        self._error = None
        self._cancel_requested = False
        lease_reader, self._lease_writer = os.pipe()
        result_writer = None
        try:
            self._result_reader, result_writer = os.pipe()
            os.set_inheritable(self._lease_writer, False)
            os.set_inheritable(self._result_reader, False)
            invocation = [sys.executable, str(Path(__file__).resolve()),
                          str(lease_reader), str(result_writer), "--", *command]
            self.process = subprocess.Popen(invocation, start_new_session=True, close_fds=True,
                                            pass_fds=(lease_reader, result_writer), **options)
            self.group_id = self.process.pid
        except BaseException:
            self._close_lease()
            if self.process is not None:
                try:
                    self.process.wait()
                finally:
                    for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
                        if stream is not None:
                            stream.close()
            self._close_result()
            raise
        finally:
            # Neither end retained here may keep a guardian channel alive.
            try:
                os.close(lease_reader)
            finally:
                if result_writer is not None:
                    os.close(result_writer)

    def _close_lease(self) -> None:
        with _defer_signals():
            descriptor, self._lease_writer = self._lease_writer, None
            if descriptor is not None:
                os.close(descriptor)

    def _close_result(self) -> None:
        with _defer_signals():
            descriptor, self._result_reader = self._result_reader, None
            if descriptor is not None:
                os.close(descriptor)

    def output_lines(self):
        output = self.process.stdout
        decoder = io.IncrementalNewlineDecoder(
            codecs.getincrementaldecoder(output.encoding)(errors=output.errors), translate=True)
        pending = ""
        with selectors.DefaultSelector() as ready:
            ready.register(output, selectors.EVENT_READ, "output")
            ready.register(self._result_reader, selectors.EVENT_READ, "result")
            while True:
                for event, _ in ready.select():
                    if event.data == "result":
                        ready.unregister(self._result_reader)
                        self._read_result()
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

    def _read_result(self) -> None:
        if self._exit_code is None and self._error is None:
            with _defer_signals():
                try:
                    data = os.read(self._result_reader, _RESULT.size + 1)
                    if len(data) != _RESULT.size:
                        raise OSError("Build supervision ended without a verified retirement result; workspace preserved")
                    code, retired = _RESULT.unpack(data)
                    if not retired:
                        raise OSError("Build workers could not be retired; workspace preserved")
                    self._exit_code = code
                except Exception as error:
                    self._error = error
        if self._error is not None:
            raise self._error

    def wait(self) -> int:
        self.process.wait()
        try:
            self._read_result()
        finally:
            try:
                self._close_result()
            finally:
                self._close_lease()
        if self._error is not None:
            raise self._error
        return self._exit_code

    def cancel(self) -> int:
        # Never kill the guardian: the GUI may kill this caller at any instant.
        # Keep the writer open after C so death during grace still produces EOF.
        with _defer_signals():
            if self._lease_writer is not None and not self._cancel_requested:
                try:
                    os.write(self._lease_writer, b"C")
                except BrokenPipeError:
                    pass
                self._cancel_requested = True
        return self.wait()


def _guard(lease: int, result: int, command: list[str]) -> int:
    os.set_inheritable(lease, False)
    os.set_inheritable(result, False)
    stop_reader, stop_writer = os.pipe()
    cancelled, forced = threading.Event(), threading.Event()
    active_lock = threading.Lock()
    active = None
    cancel_thread = None

    def cooperative_cancel(owned):
        try:
            owned.cancel()
        except BaseException as error:
            owned.report_error(error)

    def owner_lifetime():
        nonlocal active, cancel_thread
        try:
            while True:
                ready, _, _ = select.select([lease, stop_reader], [], [])
                if stop_reader in ready:
                    return
                message = os.read(lease, 1)
                cancelled.set()
                with active_lock:
                    owned = active
                if message == b"C":
                    if owned is not None and cancel_thread is None:
                        cancel_thread = threading.Thread(target=cooperative_cancel, args=(owned,), daemon=True)
                        cancel_thread.start()
                    continue
                forced.set()  # EOF: owner died, even during cooperative grace.
                if owned is not None:
                    try:
                        owned.kill_immediately()
                    except BaseException as error:
                        owned.report_error(error)
                return
        except BaseException as error:
            cancelled.set(); forced.set()
            with active_lock:
                owned = active
            if owned is not None:
                owned.report_error(error)

    observer = threading.Thread(target=owner_lifetime, daemon=True)
    job = None
    code, retired = 1, False
    try:
        observer.start()
        if cancelled.is_set():
            code, retired = 143, True
        else:
            # Workers inherit the guardian's output. The job supervisor observes
            # leader exit without reading that pipe and retires its writers.
            job = BuildProcess(command, close_fds=True)
            with active_lock:
                active = job
            if cancelled.is_set():
                job.kill_immediately() if forced.is_set() else job.cancel()
            code = job.wait()
    except BaseException:
        code = 1
    finally:
        try:
            if job is not None:
                if job.failure is None:
                    job.kill_immediately() if forced.is_set() else job.cancel()
                retired = job.retired
            else:
                retired = True
        except BaseException:
            retired = False
        try:
            os.write(stop_writer, b"x")
            observer.join()
            if cancel_thread is not None:
                cancel_thread.join()
        finally:
            for descriptor in (stop_reader, stop_writer, lease):
                os.close(descriptor)
        try:
            # Five bytes, one atomic pipe write, no environment or log payload.
            os.write(result, _RESULT.pack(code, retired))
        except BrokenPipeError:
            pass  # Owner death is the reason the guardian exists.
        finally:
            os.close(result)
    return 0 if retired else 1


if __name__ == "__main__":
    if len(sys.argv) < 5 or sys.argv[3] != "--":
        raise SystemExit(1)
    raise SystemExit(_guard(int(sys.argv[1]), int(sys.argv[2]), sys.argv[4:]))
