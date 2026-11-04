# Native Switch 2 Mac export

I added this path in NX2 Port Studio **0.3.0**. I use nxnative's actual Switch 2
CPU, NVN2 and native Ampere/Metal implementation at the reviewed revision
[`1d4b8e82bcbae79fe10164d210779a4060b61605`](https://github.com/seedds/nxnative/tree/1d4b8e82bcbae79fe10164d210779a4060b61605).
I prepare that source in a separate private cache rather than overwrite my
experimental backend or mix two incompatible shader chains. I keep its pin in
`nx2/native_runtime.json`; the experimental lowerer retains its separate
`upstreams.lock.json` and shader-chain identity unchanged. Studio’s version is
also separate from that frozen adapter package; changing a UI/export version
does not rebuild unrelated shader libraries.

The current source candidate advances that pin for Mario Kart World’s base-game
sparse-texture and pool-memory work. I have not yet rebuilt or replayed the
three admitted games with this new pin; my earlier Bananza export below belongs
to the previous engine revision.

I add a separately fingerprinted [LAN/LDN host layer](../../nx2/engine_overlays/README.md)
over this exact pin. Its scope is host services and local-network packaging;
I preserve the pinned shader chain. I enable only the LAN provider for Mario
Kart World's admitted base profile, with the Mac local network permission
description already bundled. The original SDK selects Ounce LDN protocol 2,
which this layer does not implement, so I keep its wireless path unavailable.
Source fixtures pass; a real game session remains unqualified. Bananza and
Raiders keep their offline default.

## Exact profiles

| Game | Selected version directory | Upstream evidence | My NX2 scope |
| --- | --- | --- | --- |
| Donkey Kong Bananza | `updates/458752` | [Mac opening-area gameplay; later areas and iOS untested](https://github.com/seedds/nxnative/blob/1d4b8e82bcbae79fe10164d210779a4060b61605/dm/games/040074A01BF12000/README.md) | I verified my dump's complete module identities, completed a private signed Mac export and installed it in Applications. I have not yet qualified its first frame or gameplay from this integration. |
| Mario Kart World | `base/` (version `0`) | [Base-game first race and measured memory limits](https://github.com/seedds/nxnative/blob/1d4b8e82bcbae79fe10164d210779a4060b61605/dm/games/0400C3F00006E000/README.md) | I verified my base dump’s complete module identities. My new-engine candidate build, memory comparison and network tests remain pending. |
| Splatoon Raiders | `updates/65536` | [Mac-specific profile and remaining limitations](https://github.com/seedds/nxnative/blob/1d4b8e82bcbae79fe10164d210779a4060b61605/dm/games/0400380020056000/README.md) | I verified my dump's complete module identities. I have not built or played this title through NX2 yet. |

My [descriptor](../../nx2/native_runtime.json) records the complete NSO build IDs of the profile’s executable modules
checked before export. I reject an unregistered title, an older/newer selected
update, a mixed base/update ID or a different executable build. Inspection
remains useful when a valid input has no admitted export profile. I admit only
these three exact versions. Mario Kart World’s newer updates,
other Switch 2 titles, iOS and multiplayer are not enabled by this admission.
For a base profile, version `0` means `base/`; I fixed the exporter’s previous
attempt to inspect a nonexistent `updates/0`. A newer update still blocks a
base-only profile instead of silently falling back.

## Requirements and input

I use an Apple Silicon Mac, Python 3.12 or newer, Xcode **27.2** with its Metal
toolchain, .NET and the tools required by the pinned nxnative script. Its
[requirements](https://github.com/seedds/nxnative/blob/1d4b8e82bcbae79fe10164d210779a4060b61605/README.md#build-from-the-command-line)
remain the authority for this engine; the Studio interface itself supports
macOS 14+, which does not lower the game's runtime/toolchain baseline.

```text
DonkeyKongBananza/
├── base/
│   ├── exefs/                 # main, main.npdm, sdk, rtld
│   └── romfs/
├── updates/
│   └── 458752/
│       ├── exefs/
│       └── romfs/             # complete patched filesystem, not a delta
└── dlc/                       # optional, upstream title-ID layout
```

I use already decrypted files. No console keys or firmware are requested by
this export path. I preserve the base/update/DLC inputs. The old flat
`exefs/` + `romfs/` layout remains valid for inspection, but native export needs
the complete game root above so the upstream packer selects versions correctly.
The `updates/` directory is optional for an admitted base version.

## In the app

1. I select the game root and inspect it. A Mac export section appears only
   after its exact update and module IDs pass.
2. **Prepare Mac export tools** installs the Python requirements into NX2's
   separate environment and checks out the exact reviewed engine commit.
3. **Create Mac app** runs the real engine with visible progress. Applications
   is the default destination. Existing apps are preserved; select a new name
   for a candidate.

I verify the exported identity, executable, self-contained resource links and
deep/strict code signature before returning a successful receipt. This is a
local Mac signature, not a notarized public game distribution. I do not export
an iPhone IPA from this path.

## CLI

```sh
python3 -m venv .nx2-build/venv
.nx2-build/venv/bin/python -m pip install -r requirements-nx2-runtime.txt
make -C native
mkdir -p /path/to/private-engine-cache
.nx2-build/venv/bin/python scripts/nx2.py native prepare \
  /path/to/private-engine-cache/nxnative --json
.nx2-build/venv/bin/python scripts/nx2.py native inspect \
  /path/to/DonkeyKongBananza --json
.nx2-build/venv/bin/python scripts/nx2.py native export \
  /path/to/DonkeyKongBananza --engine /path/to/private-engine-cache/nxnative \
  --output '/Applications/Donkey Kong Bananza — NX2.app' --language es --json
```

I reject a moving/wrong revision, changes outside the exact reviewed host layer,
changed layer bytes and untracked tool or compiled-source imports. I apply the
layer only while preparing a fresh owned checkout, then verify every resulting
source fingerprint. I keep the clean pin and each layer in separate cache
folders. Export clears ambient `DM_*` overrides before invoking the pinned
script, so another game's shell profile cannot change this build. Progress is
the engine's real newline-delimited JSON; a final `native-exported` receipt is
emitted only after signature checks and cleanup succeed.
For the current local-network profile I create an owned `DM_NETWORK=lan` environment
file and pass it to the existing packer. I verify its bundled copy and privacy
text after signing, then remove the temporary input. The export receipt names
the exact layer and keeps `multiplayer_verified` false.
The adapter can select independent provider combinations for future qualified
profiles; it does not enable every provider merely because its source is present.

## My private export check

I completed the full Bananza export with its optional DLC: 23,638 shaders translated and compiled, an Apple GPU pipeline archive, and an 11.9 GB self-contained Mac app. The engine preserved the registered v458752 module fixes and derived shader tables for additional DLC content. I verified the deep/strict signature and resource links, then installed the app with a same-volume rename, preserving one game bundle. The owned temporary extraction/shader workspace was removed. On November 2 I opened this installed candidate and selected Play. The ordinary runtime log progressed beyond frame 6,000 with submissions and draws. My app-control handle remained attached to the launcher while the engine rendered in a separate child, so I could not inspect the game image. I have not visually verified a first frame or played a session; runtime frame counters do not establish either result. I also had offline archive compilation running for another title, so I do not use this attempt as a performance measurement. I stopped the exact launcher process through its documented signal-forwarding path and verified that both launcher and rendering child exited.

## Storage and validation limits

I create one owned temporary workspace under the game's `nx2-work` directory.
I also give the upstream packer an owned staging folder on the destination
volume. After verification I rename only the finished app into place and
remove the packer's sibling inventory and partial products with that folder;
unrelated files beside the selected destination are preserved.
On success, failure or handled termination I retire that workspace, leaving
only a small redacted build log and, on success, its receipt. I never remove
the dump, a pre-existing output app or another operation's workspace. The
signed game bundle keeps its own resources; build cleanup must not leave
external links. An abrupt power loss or SIGKILL can still leave that operation's
temporary directory; I do not describe this as crash-proof garbage collection.

My authored fixtures exercise exact version/build admission, dirty/wrong
engine rejection, import shadowing, source preservation, signed synthetic app
verification, output preservation, failure cleanup and real child termination.
The Swift tests reject missing/late/unverified export receipts. These checks
do not establish GPU correctness, an online session or gameplay compatibility.

I preserve nxnative's MIT attribution and licenses. Its source cache and the
user's private game build are not copied into this repository or the Studio
release. My experimental NVN2 source remains GPL-3.0-or-later with its existing
Eden/Eglantine provenance.
