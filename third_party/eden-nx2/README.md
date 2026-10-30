# Eden NX2 source

`lower_to_maxwell.h` is copied unchanged from Takoroka's `eden-nx2` branch,
commit `50fa86bbbc403ad565eb44fcc281f16f77c69e15`, at
`src/shader_recompiler/frontend/ampere/lower_to_maxwell.h`.
The source URL and SHA-256 are recorded in [upstreams.lock.json](../../upstreams.lock.json).
Its original copyright, GPL-3.0-or-later license and Mesa NAK attribution are retained.

NX2 Port Studio's wrapper builds it on ARM64 without Eden's CPU emulator, Vulkan
renderer, game files or keys. It translates captured Ampere instructions into
intermediate Maxwell instructions, **not into Metal or a playable game**.
The native address map, shader reflection, NVN2 bindings, mesh stages and Metal
integration still need implementation. Computed branches are rejected by the
wrapper because the inherited lowerer's address-map ABI belongs to Eden.

NX2 additions and this derivative tool are GPL-3.0-or-later. Inherited nxnative
and Studio source retains its MIT notices; the original license is also saved
as [LICENSE.nxnative](../../LICENSE.nxnative). Other dependencies retain their licenses.
