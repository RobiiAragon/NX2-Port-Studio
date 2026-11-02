# Native Switch and Switch 2 source review

Reviewed on 2026-10-30. [The source catalog](research-sources.json) records 30
repositories, their exact reviewed revisions, recorded license status and
relevant paths. Repository links below identify those reviewed snapshots; live
primary-document links are separate references. README statements are maintainer
claims, not NX2 gameplay tests.
Metadata, READMEs and trees were collected; selected source files were read in
detail. No third-party binaries or console exploits were run.

The separate [cross-title SDK ABI audit](ABI_FAMILIES.md) compares the supplied
SDK modules, shared CPU layouts and version/semantic differences. Its static
reuse findings do not establish compatibility of the experimental renderer.
The [focused upstream audit](UPSTREAM_AUDIT.md) compares Eglantine
`b40ad217` with the unchanged imported `de83c314` snapshot, and records factual
ns2jb observations, selected PokeDinner/Eglantine-MKW deltas, reuse limits and
proposed admission tests. It imports no code.

## Findings already used

| Source | Checked material | Applied in NX2 |
| --- | --- | --- |
| [nxnative](https://github.com/seedds/nxnative/tree/2006d5ed8690d79ef4865a60917eb938cdb4c8dc) | `dm/tools/nso.py`: flag-dependent sizes, compression and hashes | Uncompressed segments use their actual memory size, regardless of stale compressed-size fields; page locations and hashes are checked. |
| [nxdumptool](https://github.com/DarkMatterCore/nxdumptool/tree/5ca8cd5e491fb0d7c196b06e7ceeee5c0dcc6a26) and [Switchbrew NSO](https://switchbrew.org/wiki/NSO0) | NSO flags and declared dynamic string/symbol table ranges | ZBIC flag recognition and bounded optional SDK import/export analysis. |
| [ZBIC](https://github.com/kinnay/zbic/tree/11b08f2712264bbed731545085cbd9702096ceb7) / Atmosphère-derived Zstandard | The dual-licensed C amalgamation and single-frame decompression API | Pinned C files and a native bridge decoding into a bounded caller-owned buffer; no Python extension dependency. |
| [D2R Mobile](https://github.com/d2r-mobile/d2r-mobile/tree/fb7c8e3c0f2717f5655dc8b2986427249f1715bb), [manosaba-ios](https://github.com/iceorange-dev/manosaba-ios/tree/d6dbeca28e042be8ee8e43e41956bedcf4e0bdb2), nxnative | Resource validation and RomFS identity approaches | Complete executable fingerprints and workspace re-verification, plus standard RomFS range checks. These are original NX2 implementations, not bulk imports. |
| NX Port Studio's existing firmware importer | Versioned resource catalogs with per-file sizes and SHA-256 | Separate optional Mii/font candidate library, preserving declared origin and version, with no claim of Switch 2 compatibility. |
| [Eglantine imported snapshot](https://github.com/Djihads80/Eglantine/tree/de83c3140e2ab6856d9a1ec1fec1cee96958d087) | Eden-derived lowerer delta and native header/scheduling layout | Pinned GPL snapshot with RET link classification and 2D/array TXD fixes; VS/PS graphics packing adapter with a synthetic branch-boundary regression. The separate audit of `b40ad217` does not update this pin. |

ZBIC uses the C files' BSD license option and preserves their notices. Other
code is not copied merely because a repository is public. See the exact import
pins and hashes in [upstreams.lock.json](../../upstreams.lock.json).

## Graphics research with direct relevance to Mario Kart World

- [Mesa ISBE layout](https://docs.mesa3d.org/drivers/nvk/isbe_layout.html) explains
  the primitive map and attribute storage used by `ISBERD` / `ISBEWR`. Its
  account of SKEW packing gives a reference for the missing task/mesh data path
  identified in Eden's Mario Kart World notes.
- [Mesa mesh shading notes](https://docs.mesa3d.org/drivers/nvk/mesh_shading_notes.html)
  document stage binding, payload storage, dispatch and output counts. They
  help define an implementation and tests; they are not an NVN2-to-Metal backend.
- nxnative's newer `dm/gfx/tools/dmsh_mesh.py` converts particular passthrough
  geometry programs into Metal mesh shaders. Its source explicitly limits the
  operation to preserving/remapping triangle output. That is useful MSL pipeline
  reference, but not a translator for arbitrary Ampere task/mesh programs.
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross/tree/57e4efcc84112abcd426dc5ec248b5339bee04f2) has MSL mesh reference
  shaders. The inherited shader chain already uses SPIR-V/MSL tooling; graphics
  reflection, resource bindings and the Ampere front end still need adaptation.
- [KosmicKrisp](https://docs.mesa3d.org/drivers/kosmickrisp.html) is another
  Vulkan-on-Metal implementation to assess for a Mac diagnostic path. Its
  documentation currently requires macOS 26/Metal 4 and says iOS is unsupported.
  It therefore cannot be adopted as a common Mac/iPhone solution without more work.
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK/tree/5189f8de86ec365860f63bf1da736815cfaea5cd) remains relevant to studying
  Vulkan/Metal differences. Its current README has a deprecation/community
  maintenance notice; selecting a backend must follow actual platform coverage.

## Other projects reviewed

| Project | Relevance and boundary |
| --- | --- |
| [MacNativeGames](https://github.com/ly05010419/MacNativeGames/tree/933b6a0d1438de160208a0a13eb5e8d7a2cb8094) | Native host, Metal resource lifetime, synchronization and per-title porting reference. Switch 1 results are not Switch 2 support. |
| [nx2android](https://github.com/shirai91/nx2android/tree/cd2e5d05ab679bbb801e10f548f03c20b83322e0) | ARM64 native host with Vulkan replay; useful separation of CPU and graphics. The name does not establish Switch 2 support; root license was not identified by GitHub. No code imported. |
| [NX2Apple](https://github.com/LNPANDY/NX2Apple/tree/7941020136b31f8b898ed0ba9dc9d91619363c4f), [NX Play](https://github.com/varyform/nx-native-wrapper/tree/bc242a3a0a471af42aa6e758d5f84c92273f757b), [LumaNX](https://github.com/Xc1Ym/LumaNX/tree/56c794538d02bf9a04fab0898b66f740702f25c7) | Packaging, library and progress workflows around nxnative. Not independent proof of an NVN2 backend. |
| [oboromi](https://github.com/0xNikilite/oboromi/tree/b97ea0cb32a0ef564ebf039bd8878dc344776e13) | GPL shader decoder/source to compare SM86 encodings; README explicitly says commercial games and firmware cannot run. |
| [Older oboromi fork](https://github.com/Denisbgpro/oboromi/tree/ec00242f5c00235c6a5610825d26e4f2017498cd) | Small proof of concept; distinct revision/license. Not treated as a working native port. |
| [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere/tree/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8) | Loader and kernel/service semantics reference. Not used as an Apple runtime or run against the user's console. |
| [libnx](https://github.com/switchbrew/libnx/tree/b156b4774fb2df7b93402169509266da7547fc4c) | Public service/SVC structures and observable behavior. Guest service declarations still need an Apple host implementation. |
| [deko3d](https://github.com/devkitPro/deko3d/tree/373bfd43d8b2d805fa2eb7d406dbb823032df958) | Public graphics object/texture layout reference already credited in the inherited engine. Not an NVN2 implementation. |
| [UberSlicer](https://github.com/nvnprogram/UberSlicer/tree/c32fcf2a7dd6cc19d733dc156f755197e486c880) | Research on NVN/NVN2 shader specialization/control data; narrow maintainer-tested game scope. No root license found; no code copied. |
| [Switch 2 controller bridge](https://github.com/mlstr0m/switch2bridge-macos/tree/809213eb938335e65a1a4be0eabf21b15c9559c7) | Future BLE/input reference. Not required to run ARM64 modules or translate shaders. |
| [switch2loader](https://github.com/gr33k/switch2loader/tree/ecbef2f46dc4cb3583e8a44a019d018a238c60ed) | Console setup/transport project, not a native Apple game runtime. No setup or exploit executed. |
| [NeXo2](https://github.com/NeXoNetwork/NeXo2-emu/tree/7fa73a5062e64370163a248d1a0b555272012c0f) | Experimental homebrew, Maxwell and Vulkan paths. The reviewed source does not provide a ready commercial NVN2/Metal backend. No code imported. |
| [fOboromi](https://github.com/C-GBL/fOboromi/tree/f0880fdb5adaf1c4687762e29b4e5879c2a70d7c) | SM86 decoder/handler reference with unimplemented operations; not proof of commercial-game execution. No code imported. |
| [THZoria/HBLauncher](https://github.com/THZoria/HBLauncher/tree/7fdb2acec9f086e0c9fdf7665cdaa1ff772825af) | Public GPU-app SDK docs for a SmileBASIC framework, including native RPC wrappers and controller capabilities. This is not an ARM64 NRO loader or Metal backend. License not identified; documentation reviewed without copying code or running console setup. |
| [THZoria/NX_Firmware](https://github.com/THZoria/NX_Firmware/tree/d235cd789f1447e2ae63ab4588251c020c9c2222) | Firmware catalog/release metadata, with no NVN2/Metal implementation identified. No firmware or release binaries downloaded. |
| [PokeDinner](https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork/src/commit/006e27f4f31f5ffe151f01a9fa3824f2d7c3c739), [Eglantine-MKW](https://github.com/Wael3rd/Eglantine-MKW/tree/efc1224674a20204e8d4064b887835626f553ff6) | Selected shader semantics, cache identity/publication and memory lifetime ideas. Differences and rejection tests are in the focused audit; shared mechanisms are not independent support evidence. No code or release assets imported. |
| [ns2jb](https://gitlab.com/pepsipu/ns2jb/-/tree/95f5525089273c5fce0b95047f3600f8442e96ae) | Selected acquisition/IPC/core-mask facts only. License not identified; no code, fixed SDK references or exploitation procedures imported. Console-native execution is not Apple ABI or Metal support. |
| [Pound profile](https://github.com/pound-emu/.github/tree/19608db5dd3b9fc3cb5be727d440871650c20a97), [Pound site](https://github.com/pound-emu/www/tree/74cddb7e62d2fe4b0d9634194389d3b408450c97) | Organization/website material in the reviewed repositories, not a usable runtime source contribution. |

## Decisions

Keep native ARM64 execution plus API interception as the intended runtime path.
Do not replace it with a front-end wrapper or assume a Vulkan driver implements
NVN2. Use the supplied Bananza build for current bring-up; Mario Kart World
still awaits its own dump and exact module identities.
Prioritize verified native CPU bring-up, shader containers/reflection, ISBE
task/mesh semantics and memory lifetimes. Hardware, save and gameplay claims
require the user's completed dump and real executions.
