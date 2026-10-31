# NX2 Port Studio

Experimental native Switch 2 bring-up for Apple Silicon. The first local
diagnostics use **Donkey Kong Bananza**; **Mario Kart World** remains a planned target.
Built from [NX Port Studio](https://github.com/RobiiAragon/NX-Port-Studio), with
[nxnative](https://github.com/seedds/nxnative) as the native execution foundation
and [Eden NX2](https://git.eden-emu.dev/Takoroka/eden-nx2/src/branch/eden-nx2)
as an additional protocol and Ampere shader reference.

**Current milestone: decrypted input validation, private bring-up workspaces,
a native macOS front end and a standalone Ampere lowering tool.**
This version does **not yet render Switch 2 games or export playable Mac/iPhone games**.
Donkey Kong Bananza's base and one update passed input checks and a private,
headless inherited-host CPU probe reached `nnMain`, then stopped deliberately
before graphics. The desktop app remains the input stage. See the
[Bananza diagnostic status](docs/nx2/DONKEY_KONG_BANANZA.md) for the scope and limits.

## Input

The same extracted layout used by Eden NX2:

```text
Mario Kart World/
├── exefs/
│   ├── main
│   ├── main.npdm
│   ├── rtld
│   ├── sdk
│   └── subsdk0 …       # when present in the game
└── romfs/              # or one decrypted .romfs / RomFS.img
```

These are already **decrypted/extracted ARM64 binaries**, not decompiled C/C++.
No `prod.keys`, title keys or firmware are needed for this input stage. Encrypted
NSP/XCI import is not part of the NX2 path. Whether a game needs particular system
resources during execution must be established separately.

The inspector checks Switch 2 NPDM layout, module bounds, LZ4/ZBIC decompression
and declared segment hashes. It fingerprints complete modules and metadata,
detects concurrent file changes and optionally reads SDK imports/exports.
RomFS image checks cover metadata ranges and the root entry, not full file
payload integrity. Source files are read only.

## Get started

Requirements for the input tools: Python 3.9+ and LZ4 for compressed modules.
The desktop front end requires Apple Silicon, macOS 14+ and Swift 6 to build.

```sh
python3 -m venv .nx2-build/venv
.nx2-build/venv/bin/python -m pip install -r requirements-nx2.txt
make -C native  # Ampere developer tool and bounded ZBIC decoder
.nx2-build/venv/bin/python scripts/nx2.py inspect "/path/to/Mario Kart World" --cpu-census
.nx2-build/venv/bin/python scripts/nx2.py prepare "/path/to/Mario Kart World" \
  --output "/path/to/private-work/MKW-NX2" --platform macOS
.nx2-build/venv/bin/python scripts/nx2.py verify "/path/to/private-work/MKW-NX2"
```

The workspace contains `inspection.json`, `build-plan.json`, the source pins and
a status note. It references the original game files without copying them.
Choose a **new private output folder outside the repository and dump folder**.
`--platform iOS` records the intended target; it does not generate or sign an IPA.
`--json` emits a structured result, with nonzero exit status for an invalid input.
`--symbols` adds declared dynamic imports/exports and compares their names across
modules, separating required and weak names not found. This does not validate the
ABI or relocation behavior; missing symbol tables are reported as incomplete.
Errors include stable codes
and recovery hints when available. `verify` rechecks the prepared executable
bytes and optional resources; it does not hash every RomFS file payload.

### Optional Mii/font candidates

```sh
.nx2-build/venv/bin/python scripts/nx2.py resources import "/path/to/Studio/firmware-catalog" \
  --source-platform switch1 --output "/path/to/private-resources/switch1-version"
```

Choose this registered library in the NX2 app or pass `--resources` to `prepare`.
It is optional, versioned and separate from the original Studio library. Only
supported Mii/shared-font files are imported, after hash checks; Switch 2 game
compatibility is **unverified**. Already decrypted resources do not need keys.

Run the new desktop app:

```sh
swift run --package-path macOS/NXPortStudio NX2PortStudio
```

Or create an app bundle:

```sh
bash scripts/package_nx2_studio.sh "/path/to/NX2 Port Studio.app"
```

The app uses its own bundle identifier and optional Python environment. Its
**Prepare validation tools** action installs LZ4 into that NX2 environment.
It does not reuse the original Studio's saved game, signing or account settings.
The local bundle has an ad-hoc Mac signature, not a notarized public release.

## Ampere development tool

```sh
make -C native
.nx2-build/native/nx2-ampere-lower captured-sm86-code.bin maxwell-words.bin
.nx2-build/native/nx2-ampere-lower --graphics native-vs-or-ps.bin maxwell-graphics.bin
```

This builds directly for the host architecture and uses the pinned,
Eden-derived Eglantine lowerer. Raw mode produces **intermediate Maxwell words**.
Graphics mode adapts the native vertex/fragment header and scheduling layout
for the locked translator. Unsupported instructions, other graphics stages and
computed branches requiring Eden's address-map ABI are rejected. Reflection,
runtime bindings and rendering validation remain open. See
[NVN2/Metal integration](docs/nx2/NVN2_METAL.md).

## What is verified

| Area | Evidence | Limit |
| --- | --- | --- |
| Decrypted input | Synthetic structures plus Bananza base/update, eighteen verified NSO segments | No complete RomFS payload hashes or signature verification |
| Invalid input | Truncation, missing modules, empty RomFS, corrupt hashes, invalid ranges, symlinks | Not full RomFS/signature verification |
| Workspace | Private output, preserved inputs, recorded builds/platform, refused overwrite | No game executable |
| CPU diagnostic | Bananza base/update reached `nnMain` in a private headless inherited host | Deliberate stop at graphics bootstrap; inherited service/memory assumptions |
| Ampere tool | 4,543 accepted graphics intermediates from 4,609 BNSH identities; corrected primitive programs compiled to Metal | No title-wide Metal or game-draw validation; unsupported cases fail |
| Experimental NVN2 | Base Bananza initializes Metal resources, real HID setup, two native graphics programs and its first buffer alias; authored GPU copy/readback passes | Second virtual views, virtual images and drawing remain incomplete; no game image or export |
| macOS front end | Swift build and shared CLI integration | No Switch 2 game boot |
| iPhone | Intended target recorded | No IPA, device boot or gameplay tested |

Checks, requiring no game files or keys:

```sh
make -C native
.nx2-build/venv/bin/python -m unittest discover -s nx2/tests -v
swift test --package-path macOS/NXPortStudio
```

## Next milestones

See the [roadmap](ROADMAP.md), [Bananza diagnostics](docs/nx2/DONKEY_KONG_BANANZA.md),
[Mario Kart World status](docs/nx2/MARIO_KART_WORLD.md)
and [architecture](docs/nx2/ARCHITECTURE.md). [Research notes](docs/nx2/RESEARCH.md)
catalog 27 native/runtime/tooling repositories and relevant primary documentation.
The [SDK family comparison](docs/nx2/ABI_FAMILIES.md) records shared CPU layouts
and the version boundaries that still need separate validation.
Sharing ARM64 makes native CPU
execution possible after rewriting; it does not implement NVN2, Metal shaders,
memory management or Horizon services automatically.

The inherited [Switch 1 README](README.switch1.md), translated README files,
`dm/games` profiles and old `NXPortStudio` executable describe the original
project. Their compatibility and network results do not apply to Switch 2.

## Credits and license

[Source pins](upstreams.lock.json) record repository revisions and imported
source hashes, including both Eden and Eglantine. [Credits](CREDITS.md) preserve authorship and dependency
notices. New NX2 code and the Eden derivative use **GPL-3.0-or-later**;
inherited nxnative/Studio MIT code keeps its original notices, also retained in
[LICENSE.nxnative](LICENSE.nxnative). Other dependencies retain their licenses.
