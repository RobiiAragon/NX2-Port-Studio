# Mario Kart World bring-up

- Initial program ID: `0400C3F00006E000`, as reported in Eden NX2's public notes.
- Local user dump: **not supplied yet**.
- Local module build IDs: unknown until the dump is inspected.
- Native Mac boot, rendering, races and saves: **not tested**.
- Native iPhone boot, rendering, races and saves: **not tested**.
- Prepared input format: `exefs/main`, `main.npdm`, `rtld`, `sdk`, optional
  `subsdk*`, and sibling `romfs/` or a RomFS image. Case-insensitive ExeFS/RomFS
  directory discovery is supported; module names remain those used by the loader.

## Next run with the completed dump

Run `scripts/nx2.py inspect <game-root> --cpu-census --json` with the NX2 Python
environment. Save a private workspace with `prepare`. Do not commit the report,
dump, build IDs tied to unpublished fixes, extracted binaries, shaders or resources.

Inspect the actual SDK imports/exports, the graphics bootstrap and shader
containers. Establish exact build IDs before introducing any fixed-address hook.
Then implement and measure the first native boot and first frame on Mac.

Eden's Windows results are a useful baseline, not evidence that this native
host already runs the title. Its older "Where it stops now" notes lag some later
progress rows; use the pinned implementation and the most recent observations.
