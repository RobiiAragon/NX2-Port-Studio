# Mario Kart World investigation

I verified my decrypted **base game, version 0**, program ID `0400C3F00006E000`,
against nxnative’s newly registered base profile. The three complete module
build IDs are admitted in [the native export descriptor](../../nx2/native_runtime.json).
The SDK identifies itself as 19.3.3 for Switch 2. My input checks cover module
bounds, decompression and declared hashes; they do not verify every RomFS file.

I updated the separate native Mac export candidate to the exact engine revision
[`1d4b8e82bcbae79fe10164d210779a4060b61605`](https://github.com/seedds/nxnative/tree/1d4b8e82bcbae79fe10164d210779a4060b61605),
whose shader chain is `475a55e74858`. I keep that chain and its translator
separate from the experimental NX2 backend and existing exported apps.
I have not yet completed an NX2 export, first-frame check, race, save restart
or memory comparison with this candidate. iOS is not qualified.

## Memory evidence and migration

The installed Mario Kart World app came from the external
[switch2mac](https://github.com/azmasm/switch2mac) revision `a61ad99f5914` with
local modifications, shader chain `d71f70e1d7a2`. It was not produced by NX2’s
pinned exporter. Its ordinary runtime log records resident memory growing from
**12,418 MiB at frame 300 to 17,059 MiB at frame 25,500**; reported GPU allocations
rise from **14,290 to 18,893 MiB**. These are different counters, not quantities
to add together on unified memory. I did not launch that app during this audit.

I found two concrete differences in that fork’s allocation path. Its static
sparse-texture registry retains each texture by serial without retiring the
entry when the game finalizes it. Unmapped sparse tiles also keep their heap
storage. Large guest pools are wrapped as whole zero-copy buffers rather than
as the ranges the GPU actually uses. The log establishes growth, while a
controlled replay is still needed to attribute each part of that growth.

The new upstream engine reclaims sparse tiles after their GPU fence and wraps
large pools in chunks. Its [base-game report](https://github.com/seedds/nxnative/blob/1d4b8e82bcbae79fe10164d210779a4060b61605/dm/games/0400C3F00006E000/README.md#frame-rate)
reports a 7.1 GB residency set and 9.6–9.8 GB wired memory in the first race on a
16 GB M6. I treat those as upstream measurements, not a prediction for my Mac
or a result of this candidate. I retain the upstream base profile’s 9 GiB application-pool plan; the external
fork used a different memory profile. I have not added a pool override, dropped
tiles, disabled rendering or imposed a host-memory clamp. Virtual address
reservations are separate from the resident-memory measurements above.

My acceptance run will compare the same title flyover and race at matching
resolution, with ordinary process/residency counters recorded after warm-up
and across repeated scene changes. I will check the picture as well as memory.
I preserve the installed app until the replacement passes that check.

## LAN and local wireless

Nintendo documents two distinct paths in the
[official multiplayer instructions](https://en-americas-support.nintendo.com/app/answers/detail/a_id/68610):
local wireless and built-in LAN. I can select built-in LAN by holding L and R
and clicking the left stick; the consoles need the same shared router and
matching software versions. The base-only candidate cannot claim compatibility
with a console running a newer update.

I inspected the original base executable’s imports. It uses 25 `nn::ldn`
functions: 24 names already correspond to NX Port Studio’s service hooks; the
remaining `MakeBroadcastAddress` is pure SDK address calculation. The socket
and NIFM surface likewise overlaps the existing backend. I have not equated
matching names with matching structure layout, protocol behavior or a working
room. The installed external fork and the clean pinned upstream engine answer
these network services as offline. I now integrate Studio's actual socket,
NIFM and LDN providers in a separately fingerprinted
[host source layer](../../nx2/engine_overlays/README.md). I apply it only to a
fresh exact-engine checkout, without changing the graphics chain or replacing
the installed app. I enable it for this admitted base profile with bundled
`DM_NETWORK=lan` for built-in LAN only. I keep the profile's LDN path disabled
because the original initialization requests a protocol the bridge does not
implement; matching public structures does not qualify that protocol.

The original SDK 19 wrappers also confirm the public network configuration’s
scene/channel/max-node/version offsets (`0x0a`, `0x10`, `0x12`, `0x14`) and the
scan filter’s flags at `0x5c`, masked to `0x37`.
The original executable also imports EventFd, EventFdRead and EventFdWrite.
My authored availability probe reproduced the prior ENETDOWN answer. I carry
Studio's real local counters and mixed descriptor waits into this layer;
the counters remain usable offline and do not invent network availability.

I tested the real socket/NIFM and LDN lifecycle paths against this engine,
including two independent host processes. I still need to qualify the remaining
game-facing result layouts in use and verify actual game room discovery,
creation, joining and a completed session. An IP LDN
bridge also needs a compatible peer implementation; it does not directly
speak a stock console’s radio protocol. A working built-in LAN test is a
separate result and must not be described as LDN validation.

### Ounce protocol qualification blocker

I traced the admitted original modules with the shared NSO loader. `main`
imports 25 LDN functions but does not import `SetProtocol`; `subsdk0` and `rtld`
do not import it either. That absence does not mean the game uses NX protocol.
Two original `main` call sites (`0x153d624`, `0x2247928`) invoke the SDK's LDN
initializer. In the admitted SDK build, its initializer at `0x673410` passes
**protocol 2** through the same service-interface slot used by `SetProtocol`.
The exported wrapper maps public arguments `0 → 2`, `1 → 1`, `2 → 2`: Default
selects Ounce, while NX is a different selection. I keep the original bytes
and detailed disassembly private; no extracted code is included here.

This agrees with the public [LDN protocol research](https://switchbrew.org/wiki/LDN_services#Protocol),
which identifies Mario Kart World as protocol 2 and distinguishes Ounce key
generation/authentication from NX. The layer's IP bridge carries the existing
NX-style membership with up to eight nodes and platform 0. I have not
implemented Ounce protocol 2, its platform/authentication behavior, or a
verified mapping to an IP-only implementation. Bypassing the SDK initializer
must not silently turn its Ounce request into successful NX initialization.
I therefore enable **only LAN** for this profile and report LDN as unavailable;
the generic LDN provider and its synthetic fixtures remain separate source work.

I found no evidence here that a larger node structure is the immediate blocker.
Nintendo documents [wireless play for 2–8 players and separate LAN play for
2–24 players](https://en-americas-support.nintendo.com/app/answers/detail/a_id/68610).
My bridge's eight-node limit is not a claim that either game mode works or
that player count equals console count. I still need an actual matching-build
LAN room and completed session; no two-game result is qualified.

## Online and remaining limits

I have not established Nextendo support for Mario Kart World. I will not reuse
Mario Kart 8 Deluxe’s service profile, endpoints or credentials for this title.
No online, LAN or LDN match is qualified by the current input admission.

Only the base executable identities are enabled. nxnative’s
[1.8.0 discussion](https://github.com/seedds/nxnative/pull/1116) is useful evidence
for newer services and rendering work; it is not an admitted update here.
I need the actual matching input and separate build checks before enabling it.

## Source candidate checks

I prepared an independent clean checkout at the exact pin, verified the
shader-chain identity (13 checks) and built its native Mac host for this title.
The compiler completed with 56 warnings in the unmodified upstream sources.
The authored upstream memory fixtures passed on this Mac: sparse layout
**21/21**, pool chunks **75/75** and sparse tile lifecycle **4,180/4,180**, with
Metal validation enabled for the GPU checks. They use synthetic data and do
not establish a game’s memory footprint or picture.

I also passed **75 NX2 source tests** and the documentation checks. I checked
the actual base dump and the existing Bananza/Raiders dumps against the new
descriptor without modifying them. An authored base-only admission case
reproduced the old `updates/0` failure; it now selects `base/`, preserves the
inputs and continues to reject a newer unqualified update.

For the host-network layer I passed **82 NX2 source tests**, rebuilt the NX2
Studio target and linked the complete patched host. Its registry validates
**1,112 unique forced HLE entries**. I passed the real provider fixtures:
**94** socket/NIFM checks, **4,688** discovery checks, **14,849** two-process LDN
checks, **2,152** EventFd checks and **100** tracked-output checks, plus LDN event
lifetime and guest pause/resume fixtures. ASan/UBSan runs passed as well.
I also passed **836,727** authored malformed-packet/parser checks. These are
source/provider results, not a game room, race or Switch peer measurement.
My signed synthetic app carries its local-network environment, privacy text
and exact host-layer receipt while explicitly keeping multiplayer unverified.
After the protocol audit, I also passed the signed fixture with LAN-only
settings and confirmed that it does not bundle automatic LDN selection.

The first hosted provider job passed its transport/sanitizer fixtures, then
failed the full host compilation because `macos-latest` selected Xcode 26.6,
whose MetalFX headers lack the pinned source's content-offset properties.
I moved this complete-host job to GitHub's documented Xcode 27 image and
explicit Xcode 27.2 toolchain. I keep the host link required; I have not changed
the graphics source or removed the failing build step.
