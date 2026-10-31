# Observed NVN2 SDK families

Static analysis of the user-supplied base SDK modules on 2026-10-30. This
comparison does not execute the original GPU driver or establish game
compatibility. Module bytes, disassembly, build fingerprints and detailed
reports remain privately beside each dump.

## Native versions and reuse

| Inspected title | Native NVN protocol | Native shader current / minimum | Resolved API names |
| --- | --- | --- | --- |
| Donkey Kong Bananza | 231.16 | 201.11 / 201.0 | 698 |
| Drag X Drive Global Jam | 231.30 | 201.17 / 201.0 | 726 |
| Fast Fusion | 231.30 | 201.17 / 201.0 | 726 |

These titles establish two native SDK families. An additional Welcome Tour
inspection found its complete SDK NSO byte-identical to Fast Fusion's. That
provides a concrete opportunity to reuse module analysis between games.

The 201.0 shader artifact observed in Drag X Drive is distinct from its native
SDK's current 201.17 capability. Artifact versions, SDK protocol versions and
implemented host capabilities must be recorded separately. The host still
accepts only the audited 201.11 shader path: the console SDK's minimum of zero
does not establish support for other control readers or translators.

## Shared CPU layouts

The audit compared 48 representative constructors, setters, getters and
initializers per requested SDK, including their referenced default constants
and initialization spans. Device, memory-pool, sampler, texture and program
builders share the inspected CPU schemas across the three titles.
[Object-boundary notes](NVN2_METAL.md#object-boundaries) describe the bounded
host adapters. Observed initialization spans are minimum extents used by the
examined functions, rather than complete opaque-object size declarations.

The MapVirtual request also retains the inspected 40-byte CPU schema across
these families. Matching argument bytes does not prove virtual-memory,
resource-lifetime, storage-policy or GPU-coherence semantics.

## Differences that remain significant

- The newer name table retains all 698 older names and adds 28. Native ordinal
  positions shift; the host already resolves by name. A matching name does not
  independently validate an object's representation or a command's behavior.
- PoolGetFlags exposes a mask of `0x3fffff` in the inspected Bananza SDK and
  `0x1fffff` in the other two. Ordinary admitted low flags intersect; newly
  exposed bits still need their own semantics.
- TextureGetMemoryOffset zero-extends its stored low 48 bits in Bananza and
  sign-extends them in the newer family. Nonnegative offsets below `2^47`
  coincide. Negative and bit-47 cases need family-aware handling or explicit
  rejection within a narrower admitted contract.
- Protocol and shader capability queries differ. Host responses describe the
  implemented backend and must not copy a console capability table blindly.

## Admission strategy

The practical unit of reuse is a verified SDK ABI family. Exact module build
identity plus a complete module hash provides the strongest current admission
key. A future structural matcher also needs independent field-access anchors,
referenced constants, object bounds and capability checks. Normalizing
PC-relative addresses or comparing resolver-name hashes alone is insufficient.

Keep serialized shader controls, images, GPU commands and synchronization
separately validated. Shared CPU builders do not make the experimental Metal
path a universal Switch 2 runtime. Current execution evidence remains in
[Bananza diagnostics](DONKEY_KONG_BANANZA.md).
