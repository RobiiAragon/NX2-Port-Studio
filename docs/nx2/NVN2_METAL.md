# Experimental NVN2 to Metal boundary

This is an incomplete developer path, enabled only by `DM_GAME_NVN2=1` when
building a host. Studio does not export a Switch 2 game with this path.
[Bananza diagnostics](DONKEY_KONG_BANANZA.md) distinguish real execution from
offline translation and document the last reached API.

## Admission and resources

The bootstrap still replaces the guest GPU driver. `nvn2_abi.h` admits named
entry points only after inspecting their argument conventions and CPU-visible
layouts. An unknown name or an unimplemented call is an explicit failure.
The Switch 1 no-op registrations are disabled in NVN2 builds. No unsupported
shader stages or mesh, ray-tracing, DLSS or display features are advertised as
implemented by the name table.

Device initialization creates the actual Metal device/queue. Memory pools
register the guest's unified-memory backing with Metal. Host GPU addresses
alias that memory, independently of the console driver's virtual addresses.
Protocol version 231.16 and shader control version 201.11 were observed in the
tested SDK. The small property table includes audited protocol allocation
constraints and the supported ASTC texture path; unknown queries fail.
It is not a copied table of console hardware capabilities.

Physical `GPU_NO_ACCESS` pools retain CPU backing and return no GPU address or
Metal buffer. Virtual pools reserve real, aligned inaccessible VA without a
Metal buffer for holes. Mapping admission is still being implemented; existing
NVN 1 mapping shortcuts are not considered valid NVN2 aliases.

The separate hardware fixture checks CPU-only physical storage, inaccessible
virtual reservations, failure cleanup and the guarded mapping resolver. It
performs no mapping or GPU submission and does not establish alias coherence:

```sh
make -C dm/gfx/tests t_nvn2_vpool GAME=0100152000022000
MTL_DEBUG_LAYER=1 ./dm/gfx/tests/t_nvn2_vpool
```

CI compiles this fixture; running it requires an actual supported Apple GPU.
Before mapping can be admitted, its no-copy buffers must retain backing through
GPU completion, physical flush ranges must reach their virtual aliases, and
dirty tracking must follow the shared physical pages. Alias metadata alone
cannot provide those semantics.

Storage-class queries classify audited allocation geometry and policy, not
Metal pixel interpretation. A known packed-format layout may have a valid
opaque class while texture creation for that format remains unsupported.
Class tokens are deterministic 16-bit host values; NVIDIA page kinds are not
forwarded to Metal. GPU-only compressible render targets use Metal-managed
storage, without importing NVIDIA compression tags. On Apple hardware without
D24S8 the existing D32F/S8 representation changes depth precision; raw CPU
aliases/import of such data remain unsupported and image accuracy is unverified.

## Compiler-managed scratch

The supported scalar local-memory path lowers to SPIR-V `Private` and MSL
per-invocation arrays. An authored fixture through the locked translator and
Metal compiler preserves its store/read and declares no scratch-pool buffer.
Therefore the host reports zero external scratch scale for this path rather
than copying NVIDIA SM counts. Empty reservations create no Metal buffer;
positive reservations require actual backing and range/alignment validation.
Recorded reservation state is revalidated at submit and retains any nonempty
backing until completion. This does not admit other local modes or address
escapes which the translator cannot represent.

## Object boundaries

The inherited reconstructed header describes NVN 1. Pointer signatures that
match do not imply compatible object bytes. In the inspected NVN2 interface:

- The device builder clears 24 bytes, with flags at byte 4.
- Pool builders retain their device at byte 8, storage at 16/24 and flags at 4.
  Pool accessors read flags at 16, GPU address at 24 and CPU storage/size at 48/56.
- Sampler builders occupy 48 bytes and pack their enums after the float fields.
  Sampler initialization clears 64 bytes and copies the builder at byte 8.
  The inherited `sizeof(NVNsampler)` is only 48; it must not allocate an NVN2
  object. The adapter uses a separately bounded host record.
- Queue builders occupy at least 72 bytes, with per-field presence bits that
  distinguish an explicit zero size from an unspecified default. The inherited
  queue-builder type reserves only 64 bytes; the NVN2 adapter bounds its own
  record separately. Queue memory queries provide a host allocation budget,
  rather than claiming to reproduce the console driver's private storage size.

The backend and interface compile with the same selected layout. This avoids
writing the Switch 1 sampler representation over a smaller guest stack slot.
Nonzero LOD snap is unsupported and fails explicitly. The CPU fixture checks
serialized fields and canaries beyond the audited object extents:

```sh
make -C dm/gfx/tests t_nvn2_abi GAME=0100152000022000
./dm/gfx/tests/t_nvn2_abi
```

That existing profile supplies build constants for the fixture only. It does
not establish a Switch 2 memory budget or Mario Kart 8 NVN2 compatibility.
The default fixture performs no GPU work and needs no game, firmware or key
files. Its optional `--metal` mode verifies actual synthetic texture copy and
readback on a supported Apple GPU; it is not a game-rendering test.

Native vertex and raster state is decoded into the backend's canonical state,
rather than reinterpreting packed NVN2 bytes as NVN 1 objects. Blend, color,
channel, depth/stencil, polygon, multisample and stream state have independent
CPU codecs. Unsupported advanced blend, logic/alpha-test and multisample modes
fail explicitly. An asset-free portable fixture checks literal packed vectors,
value copies, output canaries and unchanged output on rejected decoding:

```sh
clang -std=c11 -O2 -Wall -Wextra -Werror -I dm/gfx/nvn/include \
  dm/gfx/tests/t_nvn2_state.c -o /tmp/t_nvn2_state
/tmp/t_nvn2_state
```

## Offline VS/PS adapter

`nx2-ampere-lower --graphics` accepts a 128-byte native vertex/fragment header
followed by Ampere instructions. It retains the first 80 header bytes and
packs the lowerer's scheduling layout. Ryujinx counts scheduling slots relative
to the code start after its header, so a scheduling word and NOP precede the
unchanged Eden body. Relative branch distances are preserved. Tests through the
actual locked translator verify the entry instruction, private spill and
observable uniform-branch result, including scheduling boundaries.

The Eglantine source is an unmodified, GPL-3.0-or-later snapshot pinned in
[the source lock](../../upstreams.lock.json). The adapter rejects computed
branches because the existing Ryujinx translator does not implement Eden's
native-address map. CAL/RET are rejected in graphics mode because the locked
decoder omits an unconditional caller's continuation; a compiling output would
not establish correct execution. Other graphics stages are also rejected. Raw mode retains
its earlier headerless output contract. Every output is exclusive-created;
neither mode overwrites a source or a previous result.

`nx2/shaders.py` and the runtime control reader validate the audited 201.11
length/stage fields independently of the inherited NVN 1 parser. The BNSH reader
does not infer VS/PS pairs across intermediate stages and bounds aggregate
payload copies as well as individual sections. Source SPIR-V is optional
compiler data, absent from the inspected production material controls.

The dedicated NVN2 branch is integrated into the fingerprinted shader chain.
Global identity pins source inputs and embedded native-source attestation;
the local lowerer binary hash additionally enters each artifact's cache identity
and provenance. Strict translator/compiler checks remain enabled. The pinned
translator DLL is unchanged. A full build of the explicitly scoped two-shader
private diagnostic profile produces argument-buffer libraries and reflection;
it does not describe the rest of the game's shaders as built.

Those two programs load real Metal functions during native Bananza setup.
An actual game-generated GPU draw and frame remain open.
