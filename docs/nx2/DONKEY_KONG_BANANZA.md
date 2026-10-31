# Donkey Kong Bananza diagnostics

Local testing on 2026-10-30, on Apple Silicon macOS. Program ID
`040074A01BF12000`. The user supplied extracted `base/` and `updates/458752/`
directories. They were tested separately; the update number is not treated as
a verified display version. Mario Kart World has not been supplied.

## Observed input

- Both variants have `main`, `rtld`, `sdk` and `main.npdm`.
- All six NSOs use LZ4. All eighteen declared segment hashes matched.
- Required dynamic import names were found across the modules' own export
  tables. Eight unresolved main imports are weak. This is name coverage,
  not calling-convention, relocation or runtime compatibility validation.
- Base RomFS has 75,238 files; the update has 77,342, excluding `.listing`
  metadata. Their FTP listings had no missing files or mismatched declared
  sizes. This is not source authentication or complete payload hashing.
- Main imports `nvnBootstrapLoader` and `nvnPrivate0`. NVN2 shader archives and
  the sead primitive renderer's NVN2 shader binary are present.
- The update declares additional `chat:u`, `mnpp:app` and `stpl:u` services
  and imports stream-play SDK functions. A declaration does not prove it is
  reached during offline gameplay.

## CPU probe and root cause

The inherited ARM64 rewriter processed all six modules without game-specific
cuts. A private source copy of the inherited host used an external diagnostic
profile, fresh save/temp/cache directories, read-only RomFS access and a bounded
run. Its window launch was replaced with direct SDK bootstrap; a guard exits
before answering any graphics API lookup. Neither installed apps nor existing
game saves were modified. The diagnostic executable is not a game export.

The first run aborted during module RELRO initialization: the SDK queried
`GetInfo` type 33 and the inherited host returned zero. The SDK checks memory
alignment against `value - 1`, so zero rejects the module's range.
[Switchbrew identifies type 33 as ProcessPageSize](https://switchbrew.org/wiki/SVC#InfoType).
The host now reports its guest memory model's 4 KiB page size, independently
of the Apple host's native page size. Memory dispatcher tests cover this on
16 KiB host pages with sanitizers.

After that fix, **both variants reached `nnMain`**, mounted their RomFS and
opened their heap configuration. The diagnostic guard then exited at the
request for `nvnDeviceInitialize`, as intended. This verifies execution of that
CPU startup path, not complete Switch 2 boot semantics. SDK initialization still
uses inherited service takeovers and memory accounting; the new SDK version
marker is not recognized. No title screen, game image, audio or gameplay was
produced. No iPhone execution was attempted.

## Offline shader probe

The primitive renderer binary contains two distinct shaders, present in both
variants. Its validated control/code ranges and 128-byte graphics headers were
used to isolate the Ampere instruction streams. Eden's pinned lowerer accepted
both streams (four invocations across base/update), producing 89 and 11
intermediate Maxwell words. The newer, pinned Eglantine lowerer also accepts
them. The graphics adapter converts their 128-byte native headers to the
80-byte Maxwell header plus scheduling layout expected by the locked shader
translator. Both programs translated to SPIR-V/MSL and compiled into macOS
Metal libraries. Synthetic tests check a branch crossing a scheduling slot.
No shader bytes or generated libraries are included here.

The first graphics packer compiled but omitted an entry instruction in the
locked translator: Ryujinx counts scheduling relative to the post-header code
start, while Eden's lowerer uses absolute positions. The adapter now prefixes
one scheduling word and a NOP, preserving the body's relative branch distances.
An authored regression through the actual locked translator verifies the entry
write, private spill and a uniform branch. Earlier private Metal products were
marked invalid and rebuilt.

A separate BRA `.U` hint was also being counted as displacement. An original
wrapper normalizes only that audited hint, preserving the vendored source.
Across 4,609 unique BNSH shader identities, **4,543 graphics intermediates are
accepted**. Sixty-five fail explicitly (gradient texture modes, CAL/RET
continuations, an opcode and conversion modes); one geometry stage is not
admitted. This is intermediate coverage, not Metal or rendering correctness.
Production controls omit the optional source SPIR-V found in the primitive
programs; that source is not a general shortcut for the game's shader corpus.

Only the primitive pair has been built through the full Metal chain and loaded
at runtime. Title-wide shader selection, reflection, resource bindings and GPU
draw validation remain open. Computed branches, non-VS/PS
graphics stages and task/mesh programs are explicitly rejected by this adapter.

## Experimental Metal boundary

A separate private host copy now creates an actual Metal device and queue on
Apple M5 Max, after reaching `nnMain`. The NVN2 build admits an explicit subset
of audited API signatures and object layouts; every other entry point aborts
with its name. It does not turn on the inherited NVN 1 no-op table.

The base run initialized the device, configured its origin/depth mode, created
a Metal-backed memory pool and the game's sampler/texture descriptor pools,
then passed its initial sampler configuration and initialized its logical
command queue. Subsequent diagnostics initialize three 2880 by 1620 display
textures, their swapchain/acquire path, command/control memory and buffers,
compiler-managed scratch state and GPU-owned render targets. The two real
primitive programs load matching Metal functions from a stamped diagnostic
package. Audited vertex and raster-state adapters are connected; initialization
now reaches the first virtual-pool mapping request. The physical source has
CPU backing but no GPU address, and the destination reserves 200 MiB of aligned,
inaccessible virtual memory. Correct mapping, backing lifetime and visibility
must be proved before admitting the request; a reservation is not an alias.

An authored fixture independently copied and read back 128 pattern bytes
through the Metal texture bridge. That is real GPU validation of the resource
path, not a game-generated draw. There is still **no game image, title screen, audio or
gameplay**. The update has not been exercised through this new boundary.

[NVN2/Metal bring-up notes](NVN2_METAL.md) record the ABI boundary and its
asset-free CPU fixture. The stable CPU-only probe above remains unchanged and
separately reproducible. The experimental runner is not included in Studio
exports; the public app remains an input/workspace preparation tool.

## HID and transfer-memory initialization

After render-target setup the base game reached game-linked `InitializeJcms`,
which uses a 16 KiB transfer object and direct HID HIPC requests. The inherited
service layer refused `hid`; the game then dereferenced a null proxy. The new
audited service path and owned memory objects complete the reached setup,
including the actual read-only 256 KiB applet-resource mapping. A fresh native
diagnostic then reached program initialization.

The [service mechanism and input limits](../reference/services.md#switch-2-hid-applet-resources-and-jcms-initialization)
and [memory ownership/mapping rules](../reference/memory.md#owned-transfer-and-shared-memory-handles)
record the general fixes. No Joy-Con optical input, general raw IPC or complete
Switch 2 memory isolation is established by this checkpoint.

## Found on this game

- [Guest page-size query during RELRO initialization](../reference/memory.md#guest-page-size-query-during-relro-initialization).
- [HID applet resources and JCMS setup](../reference/services.md#switch-2-hid-applet-resources-and-jcms-initialization).
- [Owned transfer and shared memory](../reference/memory.md#owned-transfer-and-shared-memory-handles).

## Next work

Keep exact module fingerprints in private workspaces. Continue auditing queue,
texture/program/command layouts and NVN2 shader reflection.
Bring up one correct Metal frame before extending rendering coverage. Validate
the new memory/service semantics and actual game behavior instead of replacing
the graphics guard with the Switch 1 API implementation.

Raw reports, build IDs, rewritten modules, shader bytes and logs remain beside
the user's dump. None are included in this repository or the app bundle.
