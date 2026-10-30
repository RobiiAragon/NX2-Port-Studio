# NX2 native execution architecture

## Scope

NX2 Port Studio targets already decrypted, extracted Switch 2 titles. Initial
local diagnostics use Donkey Kong Bananza; Mario Kart World (`0400C3F00006E000`)
remains a planned target. The input is the game's own ARM64
binary modules and resources, not decompiled C/C++ source. This input stage
does not accept encrypted NSP/XCI packages, request keys, or boot firmware.
Game execution may still need particular system resources; that has not been
established for Mario Kart World on this host.

## Current path

`scripts/nx2.py` → `nx2.formats` → `nx2.pipeline` → a private inspection report
and build plan. The macOS NX2Studio target calls the same command through
Studio's process runner, with process-group cancellation and no shell interpolation.

- NPDM: bounds-checked META/ACI0/ACID, 64-bit flag, Switch 2 ACID placement,
  program ID, services, core capability and SVC declarations.
- NSO: header, page locations, segment bounds, LZ4 and bounded native ZBIC
  decompression, declared segment hashes and complete file SHA-256. Optional
  dynamic symbols are read from declared rodata tables; absent tables are not
  presented as proof of zero imports. Uncompressed segments use their actual
  size instead of trusting the compressed-size field.
- RomFS: nonempty directory or image table ranges/overlaps and root entry;
  file payload integrity is not verified. Game resources are not copied.
- Inputs changing while being read are rejected; workspaces record fingerprints
  and `verify` rechecks executable/NPDM content and optional resource hashes.
- Optional CPU census: exact word-pattern counts in text. Embedded data may
  match instructions; this is not disassembly, CFG validation or ABI support.
- Workspaces: output is private and outside the repository and dump folder.
  Existing paths, including symlinks, are refused. Inspection never writes to
  input. Directory permissions are owner-only, independent of umask. A failed
  preparation removes its own staged output.

## Optional system resources

`nx2.resources` verifies an existing decrypted Studio catalog, then imports only
the supported Mii and shared-font candidates into a separate private NX2 library.
Its source platform, firmware version and per-file hashes are retained. The
Switch 1 local resource review found six such archives containing thirteen files;
their presence/integrity is verified, not compatibility with Mario Kart World.
System executables, account data, certificates and local-play keyrings are not
part of this candidate import. A prepared build plan can reference the library;
`runtime_enabled` remains false until the matching SDK use is implemented/tested.

No executable is returned, and both the CLI JSON and desktop screen state that
runtime support is pending. The legacy `build_ipa_from_nsp.py` remains inherited
Switch 1 code and is not used as an NX2 exporter.

## First real CPU diagnostic

[Bananza's base and update](DONKEY_KONG_BANANZA.md) passed module inspection and
offline rewriting. A private headless copy of the inherited host reached
`nnMain` after correcting `GetInfo` ProcessPageSize, then exited deliberately
at the first graphics lookup. This demonstrates that CPU startup path; inherited
initialization order, service takeovers and memory accounting still require
Switch 2 validation. The diagnostic host is not shipped by the desktop app.

## Native runtime path to implement

1. Validate ARM64 rewriting on each real module build: x18, thread-local state,
   SVCs, timers, stack alignment, PAC/authenticated branches, and instruction
   availability on the selected Apple hardware.
2. Implement the new SDK/Horizon semantics actually reached by that build.
   Eden's gsv/vi2/gpu/syncpt work is a reference; guessed empty-success responses
   do not constitute correct semantics.
3. Intercept NVN2 at the API boundary and implement it on Metal. The inherited
   NVN layer intercepts `nvnBootstrapLoader`; its ABI/object layouts and command
   recording cannot be assumed to match NVN2.
4. Read Switch 2 shader containers and reflection, translate Ampere SASS to an
   intermediate representation, and integrate it with the Metal compilation chain.
5. Implement task/mesh geometry, texture formats, sparse mappings, resource
   lifetimes and synchronization before judging rendering correctness.
6. Validate Mac boot and races first, then iOS memory budgets, packaging,
   provisioning and actual device execution. iOS export must be signed and verified.

Both the console and Apple use ARM64, so much of the CPU work can run directly
after rewriting. The GPU and OS interfaces remain different. This is a native
execution project; Eden's x86 CPU JIT is not copied into the runtime.

## Standalone Ampere work

The original Eden NX2 lowerer is pinned in `third_party/eden-nx2`. Its portable
C++ wrapper builds natively on Apple Silicon and translates raw 16-byte Ampere
instructions into intermediate 8-byte Maxwell words. It rejects unsupported
instructions and computed branches that need Eden's native-address-map ABI.
The output has no shader header, scheduling words, reflection or Metal bindings.
It must not be fed to the inherited shader compiler without those adapters.

The synthetic suite verifies exact EXIT output, malformed input, unsupported
opcodes, computed-branch refusal and preservation of existing output. It does
not establish Mario Kart World shader coverage, mesh support, shader semantics
for the entire instruction set, or image correctness.

## Provenance

[upstreams.lock.json](../../upstreams.lock.json) records the Studio base commit,
the separately cloned nxnative reference revision, and Eden's branch/commit
plus the SHA-256 of the one imported source file. nxnative main has not been
wholesale merged over Studio's custom changes. Original license notices remain.
The bounded ZBIC helper additionally pins the dual-licensed Zstandard C sources
and uses their BSD option. [Research notes](RESEARCH.md) and the revision catalog
separate implemented imports from references reserved for future integration.
