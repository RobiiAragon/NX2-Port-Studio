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
intermediate Maxwell words. This is a small sample, not title-wide coverage or
evidence of correct generated images. Headers, reflection, scheduling, bindings
and the Metal translation path remain to be integrated.

## Found on this game

- [Guest page-size query during RELRO initialization](../reference/memory.md#guest-page-size-query-during-relro-initialization).

## Next work

Keep exact module fingerprints in private workspaces. Inspect NVN2 object sizes,
API definitions and shader reflection before implementing device creation.
Bring up one correct Metal frame before extending rendering coverage. Validate
the new memory/service semantics and actual game behavior instead of replacing
the graphics guard with the Switch 1 API implementation.

Raw reports, build IDs, rewritten modules, shader bytes and logs remain beside
the user's dump. None are included in this repository or the app bundle.
