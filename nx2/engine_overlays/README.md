# Reviewed native engine host layer

I apply [lan_ldn.patch](lan_ldn.patch) only to the exact upstream commit in
[the export descriptor](../native_runtime.json). I preserve the clean upstream
engine and give the patched engine a separate cache identity: the base commit
plus the first 16 hex digits of the complete manifest's SHA-256. I hash the
manifest, the patch and every source file before and after application.
I never repair or patch an existing cache in place.

I carry the LAN/LDN host contribution from my nxnative fork at
[`bed87969d1e0`](https://github.com/RobiiAragon/nxnative/commit/bed87969d1e03a69251969b2bdec0dfd284b119c),
adapting its thread-wait addition to the older pinned base. I also carry the
local EventFd implementation from NX Port Studio commits
[`7cb63c27`](https://github.com/RobiiAragon/NX-Port-Studio/commit/7cb63c27edb273969f10dd7b7d069d88a55fdce8),
[`6506dbc6`](https://github.com/RobiiAragon/NX-Port-Studio/commit/6506dbc66715e2d22deabde3c88f4457b1119af3)
and [`869785a9`](https://github.com/RobiiAragon/NX-Port-Studio/commit/869785a96ef0a8425207eb78ac2459d8cfeae52c).
I preserve inherited MIT attribution in [LICENSE.nxnative](../../LICENSE.nxnative)
and the public protocol references in those sources. New adapter code remains
GPL-3.0-or-later. My [manifest](lan_ldn.json) records the exact provenance and
file fingerprints; it contains no game bytes or SDK files.

I include actual Darwin socket translation, interface/NIFM queries, bounded
UDP discovery, an IP LDN control/session bridge, local account identity and
local friend-history support. I use host-backed event counters for the SDK's
EventFd imports, with real counter, semaphore, nonblocking and descriptor-wait
semantics. Blocking calls publish a pausable guest context. I add the Mac local
network privacy text before signing when the bundled environment enables it.

I leave all graphics and shader-chain files at the clean pin. This patch does
not bring an online account, Nextendo route or TLS provider into the external
engine. I enable only LAN for the admitted Mario Kart World base profile;
its exported environment selects `DM_NETWORK=lan`. The game's original SDK
initialization selects Ounce LDN protocol 2, while this bridge implements
NX-style IP membership. I have not implemented or qualified Ounce protocol 2,
so I leave that profile's wireless path unavailable. The generic LDN fixtures
still test the actual existing bridge. Other admitted profiles keep their
existing offline default.

I validate the adapter with authored repositories and a signed synthetic Mac
app. I validate the patched engine separately with real loopback sockets,
two-process LDN sessions, malformed-packet tests, event lifetime and guest-write
tracking fixtures, ASan/UBSan, the actual packer's plist function and a complete
host link. I run that source-only check with
`python tools/maintainers/check_native_network.py /path/to/disposable/clean-engine`.
The command applies the layer to that owned checkout and creates build products
there; I do not use it on an existing engine cache or installed game.

These checks do not qualify a Mario Kart World room or race, stock-console
wireless interoperability, Nextendo support, graphics or a memory reduction.
I keep those pending results in [the game note](../../docs/nx2/MARIO_KART_WORLD.md).
