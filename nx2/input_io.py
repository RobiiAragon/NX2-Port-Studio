# SPDX-License-Identifier: GPL-3.0-or-later
"""Read regular files without following final symlinks; detect concurrent changes."""
import os
import stat
from contextlib import contextmanager

from .errors import DumpError


def identity(info):
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns


@contextmanager
def stable_file(path):
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0))
    with os.fdopen(descriptor, "rb") as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise DumpError(f"{path.name}: expected a regular file")
        yield source, before
        try:
            unchanged = (identity(before) == identity(os.fstat(source.fileno())) ==
                         identity(os.stat(path, follow_symlinks=False)))
        except OSError:
            unchanged = False
        if not unchanged:
            raise DumpError(f"{path.name}: input changed while it was being inspected",
                            code="input-changed", hint="Finish copying or dumping the game, then inspect it again.")
