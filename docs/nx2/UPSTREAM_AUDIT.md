# Upstream findings for native NX2

Reviewed on 2026-10-30 against NX2 `a584e6a`. This is a static source audit,
with no upstream executable, console exploit or firmware executed. It identifies
small, testable improvements for direct ARM64 execution with Horizon compatibility
and NVN2-to-Metal translation. It does not establish game or GPU correctness.
The [source catalog](research-sources.json) records revisions and review scope;
[RESEARCH.md](RESEARCH.md) covers the wider project survey.

## Reviewed revisions and reuse limits

| Source | Reviewed revision | Reuse boundary |
| --- | --- | --- |
| [Eglantine](https://github.com/Djihads80/Eglantine/tree/b40ad217ab829226841cb71f6e4c5cb05977a225) | `b40ad217ab829226841cb71f6e4c5cb05977a225` | New files generally carry GPL-3.0-or-later; inherited files also retain GPL-2.0-or-later and other notices. Check each file before any future import. This audit imports no code. |
| [Eglantine snapshot already used by NX2](https://github.com/Djihads80/Eglantine/tree/de83c3140e2ab6856d9a1ec1fec1cee96958d087) | `de83c3140e2ab6856d9a1ec1fec1cee96958d087` | The existing lowerer pin stays unchanged in [upstreams.lock.json](../../upstreams.lock.json). A newer audit revision does not update the vendored code. |
| [PokeDinner](https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork/src/commit/006e27f4f31f5ffe151f01a9fa3824f2d7c3c739) | `006e27f4f31f5ffe151f01a9fa3824f2d7c3c739` (`1.5.0`) | GPL-3.0-or-later project with inherited per-file notices; selected source deltas only. No release binary or code imported. |
| [Eglantine-MKW](https://github.com/Wael3rd/Eglantine-MKW/tree/efc1224674a20204e8d4064b887835626f553ff6) | `efc1224674a20204e8d4064b887835626f553ff6` (`mkw-2026-10-30-pre`) | GPL-3.0-or-later project with inherited per-file notices; selected cache, memory and service changes only. Maintainer performance claims were not reproduced. |
| [ns2jb](https://gitlab.com/pepsipu/ns2jb/-/tree/95f5525089273c5fce0b95047f3600f8442e96ae) | `95f5525089273c5fce0b95047f3600f8442e96ae` | No LICENSE/COPYING or license notice was identified in the selected texts/tree. Public interface facts are references only; no code, SDK fingerprints or payloads are imported. |

Eglantine's [agent instructions](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/AGENTS.md)
and [AI policy](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/docs/policies/AI.md#L89)
permit research and codebase understanding. No upstream contribution, PR or push
was made. Any later import still requires attribution, review and appropriate
testing; a repository-level license label does not replace per-file notices.

## What changed in Eglantine

Only four files differ between the reviewed Eglantine revision and the older
snapshot above. Historical entries in `NX2_NOTES.md` describe a wider development
history and must not be reported as this revision's delta.

| Changed files | Verified change | Native NX2 consequence |
| --- | --- | --- |
| [`device_memory_manager.h`](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/device_memory_manager.h), [`.inc`](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/device_memory_manager.inc#L209-L315) | Map/unmap cuts a preceding cached continuity run and clears changed pages before publishing the mapping generation. A stale run could otherwise span a remapped page or hole. | Preserve this invariant when native remap/unmap is deliberately implemented. Current fresh mappings do not make those operations available. |
| [`deconstructed_rom_directory.cpp`](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/loader/deconstructed_rom_directory.cpp#L136) | Bounded ancestor search for optional metadata and resource-based version hints. | Useful optional inspector metadata; hints cannot identify an executable build. |
| [`worker.cpp`](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/qt_common/game_list/worker.cpp#L470) | Shallow discovery of loose ExeFS/update candidates and fallback names. | A pure candidate enumerator could help input selection. It must preserve explicit variant choice and validation. Qt UI code is unnecessary. |

No `src/video_core` file changed in this comparison. The thread-local translation
cache and its generation check [already existed](https://github.com/Djihads80/Eglantine/blob/de83c3140e2ab6856d9a1ec1fec1cee96958d087/src/core/device_memory_manager.h#L130-L153).
Generation invalidation prevents stale lookup reuse; it does not retain backing
storage during an in-flight CPU or Metal access.

## Loader, memory and Horizon findings

| Area and source | Useful fact | NX2 coverage or next bounded unit |
| --- | --- | --- |
| [NSO loading](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/loader/nso.cpp#L71), [ZBIC](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/common/zbic_compression.cpp#L35) | Compression selection, module ordering, BSS and relocation provenance are separate concerns. | Keep NX2's stricter all-segment range/hash checks, actual uncompressed lengths, bounded single-frame ZBIC decode and explicit image provenance. No replacement parser is needed. |
| [NPDM](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/file_sys/program_metadata.cpp#L37), [capabilities](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/kernel/k_capabilities.cpp#L67) | Switch 2 metadata has a different ACID prefix; process capabilities constrain cores, priorities and handles. | Layout identification and bounded capability reads are already present. Extend one coherent process contract for thread/SVC behavior; keep logical guest cores distinct from Darwin scheduling. A historical emulator SDK constant is not the inspected title's SDK version. |
| [RomFS](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/file_sys/romfs.cpp#L31), [loose filesystem registration](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/loader/deconstructed_rom_directory.cpp#L395) | Table-relative entry graphs support lazy file access; loose input still needs an explicit filesystem/RomFS association. | NX2 validates image tables/root, not every file graph. A future reader needs cycle, name, link and payload bounds, plus deterministic ambiguity errors. Do not transplant the less bounded walker or rebuild a directory into an image unnecessarily. |
| [Memory accounting](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/kernel/k_process.cpp#L1264), [GetInfo](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/kernel/svc/svc_info.cpp#L35) | Heap, alias capacity and separately owned GPU allocations must agree with reported budgets. Several newer region/budget answers are explicitly guesses or stubs upstream. | Count each allocation once across aliases and retained work. Preserve NX2's guest page contract separately from Apple VM alignment. Audit a real caller before admitting further region queries; do not import guessed successful answers. |
| [Transfer memory](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/kernel/k_transfer_memory.cpp#L20), [typed handles](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/kernel/k_handle_table.h#L31) | Objects retain their owner/pages; lookup acquires a typed strong reference; partial IPC acquisition must unwind. | NX2 already owns transfer backing and rejects stale handles. Generalize the typed registry only with close/copy/move, stale-generation, permission, overflow and rollback fixtures. Do not import an emulated kernel/page-table stack. |
| [TIPC and raw sessions](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/service/nx2_gsv.h#L73-L106) | TIPC result framing differs from CMIF; a raw session returned through a domain object needs a real retained server/handler. | Add authored framing/lifetime fixtures when a caller needs the path. Require real endpoint and copy/move semantics before publishing an object. |
| [HID shared-memory ownership](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/hid_core/resources/shared_memory_holder.cpp#L16), [HID commands](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/service/hid/hid_server.cpp#L135) | Live producers/rings and client lifetimes matter. No Joy-Con 2 optical/JCMS implementation was found in the audited source; the newer vibration entry is an empty-success stub. | NX2 already has live host-input publication and an audited absent optical-device state. Eglantine supplies review material, not missing JCMS or haptics support. |
| [Fonts](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/service/ns/platform_service_manager.cpp#L114), [audio client](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/service/audio/nx2_audio_user_client.h#L25), [graphics scale](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/service/am/service/graphics_scale_controller.h#L13) | New command/object names are useful caller leads. Type mapping and some values remain guessed; unsupported font types are clamped upstream. | Implement only a reached, audited ABI over owned host resources. Report unavailable resources accurately. Do not substitute a different font, fabricate camera/chat events or treat an unknown command as implemented. |

Upstream's [automatic service stubs](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/service/nx2_stub_interface.h#L21)
are outside the reuse plan. Success requires the actual output, ownership and
state contract. CPU emulation, Dynarmic/JIT changes, emulated DRAM/GMMU, guest-core
folding and Android/Linux signal-context code do not provide an Apple native
runtime. Architecture-neutral SVC/TLS/relocation invariants can still guide an
independent implementation.

## Graphics findings and admission gates

Eglantine's [native GOB address permutation](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/textures/decoders.cpp#L21-L85)
predates the reviewed delta. Its [driver identification](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/service/nvdrv/devices/nvhost_ctrl_gpu.cpp#L277-L326)
and [channel initialization](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/control/channel_state.cpp#L76-L80)
enable native tiling globally. This is a concrete upstream layout reference,
but that global selection does not establish how a native SDK resource
selects that layout. No blanket NVN2 decoder change follows. An encode/decode
roundtrip using one formula proves internal consistency, not hardware layout.
An independent selection/layout oracle is still required before extending
production tiled uploads.

The most useful reusable graphics ideas are identities and ownership:

- [Address-space aliasing](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp#L124-L190)
  reuses physical backing across GPU virtual addresses. Native NX2 needs physical
  generation plus interval identity, independent view leases and transactional
  publication; exact-pair reuse is not a ready Metal interval allocator.
- [Image aliases and modification ordering](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/texture_cache/texture_cache.h#L1914-L1998)
  distinguish backing identity from the newest content representation. Shared
  buffer bytes do not prove coherence of private Metal textures. Compatible views
  need one authoritative image or explicit ordered copies between valid images.
- [Pending downloads](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/texture_cache/pending_download.h#L12-L57)
  retain image metadata until completion. Native CPU publication must likewise
  retain the actual allocation/image and honor completion order.
- [Fence management](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/fence_manager.h#L73-L122)
  offers ordering and batching ideas. Immediate callbacks and skipped backend
  work in optional fast modes do not define native completion semantics.

The [kind-modification stub](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/core/hle/service/nx2_gsv.h#L469-L481)
does not convert or synchronize compressed images. GPU descriptor enums,
NVN API enums, byte geometry and NX2 host storage-policy tokens are separate
domains. No upstream no-op or Vulkan format fallback admits a native Metal
representation. Current limitations remain documented in
[NVN2 Metal](NVN2_METAL.md), [native alias invariants](../reference/nvn-objects.md#nvn2-fresh-buffer-aliases-retain-their-backing)
and [Bananza diagnostics](DONKEY_KONG_BANANZA.md).

The shader/command review also found reusable validation ideas with explicit
boundaries. None of this source changed between `de83c314` and `b40ad217`.

| Source | Native adaptation and limit |
| --- | --- |
| [Bounded program capture](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/shader_recompiler/frontend/ampere/program_capture.h#L8-L53), [address maps](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/shader_recompiler/frontend/ampere/program_address_map.h#L7-L29) | Use branch/call/exit cases for authored control-flow tests. Keep native parsing bounded by validated SDK control/code lengths and actual pool extents. Upstream scheduling uses a different consumer layout; it does not replace NX2's corrected Ryujinx packing or remove computed-branch/call/return guards. |
| [Observed shader resources](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/shader_environment.cpp#L155-L195), [reflected binding consumption](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/renderer_vulkan/vk_graphics_pipeline.cpp#L366-L437) | Include every specialization value in immutable compile identity and derive Metal bindings from the locked translator's reflection plus an audited native handle contract. Vulkan binding numbers are not Metal indices. Driver-bank remapping and [synthesized blit constants](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/shader_recompiler/ir_opt/position_pass.cpp#L77-L141) do not implement arbitrary API uniform buffers. |
| [Rejected-program caching](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/shader_cache.cpp#L58-L125), [QMD dependencies](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/native_qmd.h#L64-L136) | Cache diagnostics by immutable content/tool identity and validate before visible effects. Do not inherit skipped draws, completion signals for rejected dispatches or a [bilinear compute substitute](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/engines/kepler_compute.cpp#L591-L612). |
| [Exact-source overrides](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/shader_recompiler/frontend/ampere/program_override.h#L10-L84), [memoization dependencies](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/macro_memo.h#L53-L104) | Exact input equality and complete dependency snapshots are useful cache principles. Code-only hashes, runtime hot replacements and polling for external overrides are outside NX2's reproducible offline pipeline. |
| [MME interpreter](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/video_core/macro_turing.h#L150-L218) | MME is GPU command processing, distinct from the game's directly executed ARM64 CPU. Bounds and read-before-write semantics can clarify GPU methods; its optional x86 JIT adds no Apple-native capability. Do not add a raw GPFIFO/MME pipeline without a separately demonstrated need. |
| [Return fixture](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/tests/video_core/ampere_returns.cpp#L1-L51), [MUFU conversion](https://github.com/Djihads80/Eglantine/blob/b40ad217ab829226841cb71f6e4c5cb05977a225/src/shader_recompiler/frontend/ampere/lower_to_maxwell.h#L1750-L1754) | Useful candidates for small semantic oracles. Lowerer-only checks do not prove translation, resource reflection or GPU results; each admitted form needs tests through its actual consumer. |

The native adapter's raw and graphics shader paths reject unsupported HMMA
instructions before calling the vendored lowerer, regardless of ambient tensor
mode. The upstream stub mode could otherwise emit a shader with incorrect
arithmetic. An authored tensor/exit fixture covers environment states and
preservation of prior outputs; this is a rejection fix, not tensor support.
See [ambient tensor modes and offline semantics](../reference/shaders.md#ambient-tensor-modes-must-not-change-offline-shader-semantics).
The imported-source pin remains unchanged.

## Additional fork deltas

These forks were reviewed as source, without downloading release assets. Shared
Eglantine mechanisms are not counted as independent implementations: PokeDinner's
continuity-run fix is materially the same as the `b40ad217` change above, and its
native-tiling flag still does not prove a native SDK resource selector.

| Source delta | Useful next step | Boundary and required proof |
| --- | --- | --- |
| PokeDinner [floating select](https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork/src/commit/006e27f4f31f5ffe151f01a9fa3824f2d7c3c739/src/shader_recompiler/frontend/ampere/lower_to_maxwell.h#L1537) preserves floating operand modifiers separately from bitwise select. Its [uniform vote](https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork/src/commit/006e27f4f31f5ffe151f01a9fa3824f2d7c3c739/src/shader_recompiler/frontend/ampere/lower_to_maxwell.h#L619) and [array-cube sampling](https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork/src/commit/006e27f4f31f5ffe151f01a9fa3824f2d7c3c739/src/shader_recompiler/frontend/ampere/lower_to_maxwell.h#L1968) accept additional bounded forms. | Independently test small opcode semantics before considering a narrow change. | Authored select vectors must cover predicates, ABS/NEG, NaN and signed zero; vote needs register/mode boundaries; cube-array needs coordinate/layer/LOD and result-layout checks through the locked translator and Metal readback. No compute or task/mesh coverage follows. |
| PokeDinner's [lowerer classification](https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork/src/commit/006e27f4f31f5ffe151f01a9fa3824f2d7c3c739/src/shader_recompiler/frontend/ampere/lower_to_maxwell.h#L95) removes the older RET exclusion from active-link destination handling. | Preserve NX2's existing RET regression when evaluating individual deltas. | A wholesale header replacement could lose a correction already in the imported snapshot. No pin or translator replacement is proposed. |
| PokeDinner tracks [GPU-written command memory](https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork/src/commit/006e27f4f31f5ffe151f01a9fa3824f2d7c3c739/src/video_core/dma_pusher.cpp#L477) before CPU consumption; [sparse allocation](https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork/src/commit/006e27f4f31f5ffe151f01a9fa3824f2d7c3c739/src/video_core/renderer_vulkan/vk_texture_cache.cpp#L2689) unwinds newly allocated layers on failure. | Apply producer completion, canonical backing identity and atomic publication principles to native resources. | An ever-written page bitset is not a per-write epoch. Test delayed completion, repeated writes, aliases, teardown and every failed allocation/bind step. Vulkan sparse images remain outside the native admitted subset. |
| Eglantine-MKW [retains lowered code and specialization state](https://github.com/Wael3rd/Eglantine-MKW/blob/efc1224674a20204e8d4064b887835626f553ff6/src/video_core/shader_environment.cpp#L332), avoiding duplicate lowering and restoring a driver-constant specialization flag on cached programs. | Preserve immutable compile inputs and live/cache equivalence. | NX2 uses a different offline pipeline. Code alone is an insufficient cache key: control, reflection, specialized constants, profile and chain identity matter. GPU-written source also requires coherent capture. |
| Eglantine-MKW [writes a temporary pipeline cache then renames it](https://github.com/Wael3rd/Eglantine-MKW/blob/efc1224674a20204e8d4064b887835626f553ff6/src/video_core/renderer_vulkan/vk_pipeline_cache.cpp#L1139). | Independently implement old-cache-preserving publication if needed. | Its exception path can still delete the previous valid cache; no durable sync is shown and writers share a temporary name. Test short writes, failed close/rename, full disk and concurrent writers. Rename alone does not prove power-loss durability. |
| Eglantine-MKW [refreshes memory budgets](https://github.com/Wael3rd/Eglantine-MKW/blob/efc1224674a20204e8d4064b887835626f553ff6/src/video_core/texture_cache/texture_cache.h#L228) and [destroys live compiler-pool objects](https://github.com/Wael3rd/Eglantine-MKW/blob/efc1224674a20204e8d4064b887835626f553ff6/src/shader_recompiler/object_pool.h#L74). | Measure Apple memory pressure and inspect analogous ownership patterns. | Vulkan heap policy is not Metal residency. Keep native ASTC where supported rather than adopting a lossy BC3 fallback. The foreign object pool is not used by NX2's locked translator; only fix an independently demonstrated analogous leak. |

PokeDinner also includes a title-specific terrain path that substitutes CPU stores
and skips a fingerprinted draw. This is a symptom lead, not a general native
shader implementation; no proprietary fingerprint is copied here. Per-title
copyback switches, texture-dimension substitutions and GameChat/camera scaffolding
likewise need an actual native caller/consumer contract before any implementation.
Boot progress or an upstream frame-rate report does not prove native Metal
correctness or an NX2 gameplay milestone.

## ns2jb: useful observations without importing code

The [README](https://gitlab.com/pepsipu/ns2jb/-/blob/95f5525089273c5fce0b95047f3600f8442e96ae/README.md)
describes a particular console firmware/runtime demonstration. Those hardware
claims are upstream self-reports, not reproduced tests. Native execution on a
Switch 2 does not establish Apple ABI, page/TLS behavior, signing or Metal support.
This audit excludes exploitation procedures and source instruction fingerprints.

| Selected text | Fact useful to an independent NX2 implementation | Limit |
| --- | --- | --- |
| [Filesystem client](https://gitlab.com/pepsipu/ns2jb/-/blob/95f5525089273c5fce0b95047f3600f8442e96ae/engine/switch2/app/filesystem.cjs#L66), [bulk transfer](https://gitlab.com/pepsipu/ns2jb/-/blob/95f5525089273c5fce0b95047f3600f8442e96ae/engine/switch2/app/bulk-transfer.cjs#L3) | Actual returned read counts, session-owned buffer spans and console/host chunk digests are checked. | Equal hashes prove transfer equality, not complete/authentic archives. The selected directory probe is bounded, not a complete recursive exporter. |
| [IPC framing](https://gitlab.com/pepsipu/ns2jb/-/blob/95f5525089273c5fce0b95047f3600f8442e96ae/engine/shared/ipc.cjs#L15) | Alignment, optional PID and copied/moved handles must be accounted for before decoding payloads. | The small JavaScript parser is not a hardened generic HIPC/CMIF/TIPC implementation to copy. Author bounded malformed-packet and rollback fixtures independently. |
| [Affinity](https://gitlab.com/pepsipu/ns2jb/-/blob/95f5525089273c5fce0b95047f3600f8442e96ae/engine/switch2/app/affinity.cjs#L26) | Process and SDK masks are checked, owned workers select an allowed core, and the result is read back. | This does not establish a universal core count or equivalent Apple physical-core scheduling. |
| [Typed process validation](https://gitlab.com/pepsipu/ns2jb/-/blob/95f5525089273c5fce0b95047f3600f8442e96ae/engine/switch2/ro/payload/process.c#L66) | A handle's object identity and returned metadata are checked before use. | Console process-manipulation mechanisms are outside NX2's native compatibility requirements. No such procedure is proposed here. |

The selected source does not provide an NCA4 parser, complete NSO integrity
pipeline or full update-overlay reconstruction. Its fixed SDK references are
specific to one runtime; matching names do not establish portable layouts.
NX2 should retain symbol resolution and independently verified, build-gated
diagnostics. See [cross-title ABI families](ABI_FAMILIES.md).

## Prioritized independent work

These are proposed units with admission tests, not implemented capabilities.
Keep the native ARM64 game CPU path and existing unsupported-operation guards.

| Priority | Unit | Evidence required before admission |
| --- | --- | --- |
| P0 | Independent tiled-layout selection and oracle | Asset-free single/multiple-GOB, mip, array and block-format vectors; independent native selection evidence; CPU and Metal upload/sample/readback agreement. Preserve the Switch 1 default. |
| P0 | Two fresh plain views of one physical allocation | CPU write/flush visible through both views; GPU write through one view read through the other and physical CPU backing; nonzero offset fixtures; strong leases across in-flight work; atomic rollback and finalize rejection. No image or compressible-policy expansion. |
| P0 | Canonical image authority | Descriptor compatibility decisions, write/clear through one image then sample another, ordered CPU publication/reimport, retained pending metadata, and explicit rejection of unsupported reinterpretation/compression paths. Buffer coherence alone does not pass this gate. |
| P1 | Coherent process/IPC object contract | Metadata-derived core/priority/budget constraints; typed close/copy/move and transfer ownership; malformed/truncated CMIF/TIPC packets, wrong handle types, stale generations and failure without partial publication. |
| P1 | Complete input provenance and bounded discovery | Explicit base/update selection, symlink/ambiguity checks, exact file list/sizes/digests, short/reordered/corrupt chunk failures and existing NSO segment hashes. Optional metadata must not become a build gate. |
| P1 | Lazy RomFS file access | Cycle/link/name/payload bounds, traversal rejection, sparse reads and unambiguous image selection. Retain the current distinction between root-table validation and full graph validation. |
| P1 | Small independently verified shader deltas | Floating-select modifier truth tables, uniform-vote boundary cases and bounded array-cube ABI tests; retain RET, rejection and chain-attestation regressions. No wholesale lowerer update. |
| P2 | Compile-input/cache identity and publication | Live/cache specialization equivalence; old verified cache survives every rejected publication, including concurrent writers. No imported cache format or vendor binary change. |
| P2 | Measured completion batching | Preserve command/publication order and real Metal completion while batching retirement. No timeout success, global no-download switch or emulated GPU pipeline import. |

The proposed units do not extend runtime admission. Validate each implementation
as a small change and record its remaining platform limits.
