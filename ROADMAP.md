# NX2 Port Studio roadmap

Milestones require reproducible evidence for the exact game build and platform.

## 1. Decrypted input and development workspace

Implemented: independent repository, key-free extracted input, Switch 2 metadata
and NSO validation, private build plans, separate macOS interface and a native
Ampere instruction lowering tool. Synthetic tests are supplemented by Bananza
base/update input checks and two real primitive shaders. Full shader semantics
and RomFS payload integrity remain unverified.

## 2. Native CPU bring-up on Mac

Bananza's base and update reached `nnMain` in a private headless inherited-host
diagnostic after fixing the process-page-size query. The probe deliberately
stops before graphics. Establish the new SDK initialization, services and memory
semantics beyond that limited path. I have now verified Mario Kart World’s
base dump and admitted its exact module identities in the separate pinned
export path. I have not exported or played it through NX2 yet.
Do not fabricate memory values or fixed addresses.

Exit evidence: rewritten ARM64 modules reach game initialization on Apple Silicon.

## 3. First correct frame

Implement NVN2 API interception, shader containers/reflection, Ampere translation
into Metal, object/layout bindings, sparse memory and fence semantics. Implement
task/mesh geometry instead of dropping draws.

In progress: the base Bananza diagnostic creates an actual Metal device and
memory/descriptor pools, display textures/swapchain, command buffers, queues and
two real graphics programs with matching Metal functions. The reached HID/JCMS
and shared-memory setup is implemented. Authored GPU copy/readback validates the
resource bridge separately. Virtual mappings and draw integration remain open;
unsupported calls still stop explicitly. No game-generated frame exists yet.

Exit evidence: a correct title/menu frame compared with the user's console.

## 4. Mac gameplay and persistence

Validate actual gameplay, controls, audio/video, saves, scene transitions and longer
sessions. Investigate the incorrect instruction/service/memory operation rather
than hiding it with an empty-success stub.

Exit evidence: a completed gameplay session and restart with a preserved save on Mac.

## 5. iPhone/iPad

Measure resident/GPU memory against the selected device budget. Integrate the
working NX2 runtime with inherited packaging and Xcode signing. Verify provisioning,
installation, boot and gameplay separately.

Exit evidence: a signed, verified IPA completing a gameplay session on a physical device.
No iOS compatibility is established by the input tools.

## 6. Additional titles and network play

Add titles based on measured differences with exact-build gates. LAN, LDN and
online services need their own implementation and physical validation. Inherited
Switch 1 network results do not establish Switch 2 interoperability.

I am investigating Mario Kart World’s memory and local network separately: the
installed external fork retains sparse texture allocations and exposes offline
network services. My candidate engine adds upstream tile reclamation and
chunked pool buffers. I still need a controlled memory comparison and a real
LAN/LDN room/session; matching service imports is not interoperability evidence.
