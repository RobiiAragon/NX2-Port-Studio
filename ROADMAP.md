# NX2 Port Studio roadmap

Milestones require reproducible evidence for the exact game build and platform.

## 1. Decrypted input and development workspace

Implemented: independent repository, key-free extracted input, Switch 2 metadata
and NSO validation, private build plans, separate macOS interface and a native
Ampere instruction lowering tool. Tests use synthetic inputs; the real dump
and full shader semantics are not yet verified.

## 2. Mario Kart World native boot on Mac

Await the completed user dump. Inspect exact module build IDs and imports,
measure CPU instruction use, then adapt the native rewriter and required
Horizon/SDK services. Do not fabricate memory values or fixed addresses.

Exit evidence: rewritten ARM64 modules reach game initialization on Apple Silicon.

## 3. First correct frame

Implement NVN2 API interception, shader containers/reflection, Ampere translation
into Metal, object/layout bindings, sparse memory and fence semantics. Implement
task/mesh geometry instead of dropping draws.

Exit evidence: a correct title/menu frame compared with the user's console.

## 4. Mac races and persistence

Validate actual races, controls, audio/video, saves, scene transitions and longer
sessions. Investigate the incorrect instruction/service/memory operation rather
than hiding it with an empty-success stub.

Exit evidence: a completed local race and restart with a preserved save on Mac.

## 5. iPhone/iPad

Measure resident/GPU memory against the selected device budget. Integrate the
working NX2 runtime with inherited packaging and Xcode signing. Verify provisioning,
installation, boot and gameplay separately.

Exit evidence: a signed, verified IPA completing a race on a physical device.
No iOS compatibility is established by the input tools.

## 6. Additional titles and network play

Add titles based on measured differences with exact-build gates. LAN, LDN and
online services need their own implementation and physical validation. Inherited
Switch 1 network results do not establish Switch 2 interoperability.
