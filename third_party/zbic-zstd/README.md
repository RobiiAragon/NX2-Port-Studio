# ZBIC-enabled Zstandard

The three Zstandard C files come unchanged from `kinnay/zbic`, commit
`11b08f2712264bbed731545085cbd9702096ceb7`, which credits Atmosphère for the
amalgamation. File hashes are pinned in [upstreams.lock.json](../../upstreams.lock.json).
Their own headers offer the BSD-style or GPLv2 license; this derivative uses
the BSD option for those C files. [LICENSE.BSD](LICENSE.BSD) retains the upstream
Zstandard BSD text and [COPYING.GPL2](COPYING.GPL2) retains the other text.
The Python extension from that repository is not copied or imported.

The NX2 wrapper passes a buffer bounded by the NSO's validated size, checks
the exact decoded length and rejects trailing or concatenated frames. The
frame's own claimed size does not determine the output allocation.
