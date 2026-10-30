# SPDX-License-Identifier: GPL-3.0-or-later
class DumpError(ValueError):
    """An actionable input or tooling error, distinct from runtime compatibility."""

    def __init__(self, message, *, code="invalid-dump", hint=None):
        super().__init__(message)
        self.code = code
        self.hint = hint
