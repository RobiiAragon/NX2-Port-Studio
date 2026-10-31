# Native Switch and Switch 2 source review

Reviewed on 2026-10-30. [The source catalog](research-sources.json) records 27
repositories, their exact reviewed revisions, licenses reported by GitHub and
relevant paths. README statements are maintainer claims, not NX2 gameplay tests.
Metadata, READMEs and trees were collected; selected source files were read in
detail. No third-party binaries or console exploits were run.

## Findings already used

| Source | Checked material | Applied in NX2 |
| --- | --- | --- |
| [nxnative](https://github.com/seedds/nxnative) | `dm/tools/nso.py`: flag-dependent sizes, compression and hashes | Uncompressed segments use their actual memory size, regardless of stale compressed-size fields; page locations and hashes are checked. |
| [nxdumptool](https://github.com/DarkMatterCore/nxdumptool) and [Switchbrew NSO](https://switchbrew.org/wiki/NSO0) | NSO flags and declared dynamic string/symbol table ranges | ZBIC flag recognition and bounded optional SDK import/export analysis. |
| [ZBIC](https://github.com/kinnay/zbic) / Atmosphère-derived Zstandard | The dual-licensed C amalgamation and single-frame decompression API | Pinned C files and a native bridge decoding into a bounded caller-owned buffer; no Python extension dependency. |
| [D2R Mobile](https://github.com/d2r-mobile/d2r-mobile), [manosaba-ios](https://github.com/iceorange-dev/manosaba-ios), nxnative | Resource validation and RomFS identity approaches | Complete executable fingerprints and workspace re-verification, plus standard RomFS range checks. These are original NX2 implementations, not bulk imports. |
| NX Port Studio's existing firmware importer | Versioned resource catalogs with per-file sizes and SHA-256 | Separate optional Mii/font candidate library, preserving declared origin and version, with no claim of Switch 2 compatibility. |
| [Eglantine](https://github.com/Djihads80/Eglantine) | Eden-derived lowerer delta and native header/scheduling layout | Pinned GPL snapshot with RET link classification and 2D/array TXD fixes; VS/PS graphics packing adapter with a synthetic branch-boundary regression. |

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
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) has MSL mesh reference
  shaders. The inherited shader chain already uses SPIR-V/MSL tooling; graphics
  reflection, resource bindings and the Ampere front end still need adaptation.
- [KosmicKrisp](https://docs.mesa3d.org/drivers/kosmickrisp.html) is another
  Vulkan-on-Metal implementation to assess for a Mac diagnostic path. Its
  documentation currently requires macOS 26/Metal 4 and says iOS is unsupported.
  It therefore cannot be adopted as a common Mac/iPhone solution without more work.
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK) remains relevant to studying
  Vulkan/Metal differences. Its current README has a deprecation/community
  maintenance notice; selecting a backend must follow actual platform coverage.

## Other projects reviewed

| Project | Relevance and boundary |
| --- | --- |
| [MacNativeGames](https://github.com/ly05010419/MacNativeGames) | Native host, Metal resource lifetime, synchronization and per-title porting reference. Switch 1 results are not Switch 2 support. |
| [nx2android](https://github.com/shirai91/nx2android) | ARM64 native host with Vulkan replay; useful separation of CPU and graphics. The name does not establish Switch 2 support; root license was not identified by GitHub. No code imported. |
| [NX2Apple](https://github.com/LNPANDY/NX2Apple), [NX Play](https://github.com/varyform/nx-native-wrapper), [LumaNX](https://github.com/Xc1Ym/LumaNX) | Packaging, library and progress workflows around nxnative. Not independent proof of an NVN2 backend. |
| [oboromi](https://github.com/0xNikilite/oboromi) | GPL shader decoder/source to compare SM86 encodings; README explicitly says commercial games and firmware cannot run. |
| [Older oboromi fork](https://github.com/Denisbgpro/oboromi) | Small proof of concept; distinct revision/license. Not treated as a working native port. |
| [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) | Loader and kernel/service semantics reference. Not used as an Apple runtime or run against the user's console. |
| [libnx](https://github.com/switchbrew/libnx) | Public service/SVC structures and observable behavior. Guest service declarations still need an Apple host implementation. |
| [deko3d](https://github.com/devkitPro/deko3d) | Public graphics object/texture layout reference already credited in the inherited engine. Not an NVN2 implementation. |
| [UberSlicer](https://github.com/nvnprogram/UberSlicer) | Research on NVN/NVN2 shader specialization/control data; narrow maintainer-tested game scope. No root license found; no code copied. |
| [Switch 2 controller bridge](https://github.com/mlstr0m/switch2bridge-macos) | Future BLE/input reference. Not required to run ARM64 modules or translate shaders. |
| [switch2loader](https://github.com/gr33k/switch2loader) | Console setup/transport project, not a native Apple game runtime. No setup or exploit executed. |
| [NeXo2](https://github.com/NeXoNetwork/NeXo2-emu) | Experimental homebrew, Maxwell and Vulkan paths. The reviewed source does not provide a ready commercial NVN2/Metal backend. No code imported. |
| [fOboromi](https://github.com/C-GBL/fOboromi) | SM86 decoder/handler reference with unimplemented operations; not proof of commercial-game execution. No code imported. |
| [THZoria/HBLauncher](https://github.com/THZoria/HBLauncher) | Public GPU-app SDK docs for a SmileBASIC framework, including native RPC wrappers and controller capabilities. This is not an ARM64 NRO loader or Metal backend. License not identified; documentation reviewed without copying code or running console setup. |
| [THZoria/NX_Firmware](https://github.com/THZoria/NX_Firmware) | Firmware catalog/release metadata, with no NVN2/Metal implementation identified. No firmware or release binaries downloaded. |
| [Pound profile](https://github.com/pound-emu/.github), [Pound site](https://github.com/pound-emu/www) | Organization/website material in the reviewed repositories, not a usable runtime source contribution. |

## Decisions

Keep native ARM64 execution plus API interception as the intended runtime path.
Do not replace it with a front-end wrapper or assume a Vulkan driver implements
NVN2. Use the supplied Bananza build for current bring-up; Mario Kart World
still awaits its own dump and exact module identities.
Prioritize verified native CPU bring-up, shader containers/reflection, ISBE
task/mesh semantics and memory lifetimes. Hardware, save and gameplay claims
require the user's completed dump and real executions.
