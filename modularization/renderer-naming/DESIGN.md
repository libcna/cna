# CNA terminology + renderer-identity normalization — campaign design

Date: 2026-08-10. Base: public `develop` `25db3ccbe0f963da61ff2a4e487a23a81bf54125`
(post-modularization base). Branch: `feature/renderer-naming-normalization`.

This is the owner-directed naming/API/build-surface normalization that precedes the future
41 → 55 renderer expansion. It changes names only — no renderer behavior, no module
boundaries, no sharp-runtime changes. It is intentionally a source/build naming
compatibility break: repository-internal consumers migrate to the new names, and no
deprecated aliases are kept.

Three normalization families:

- **A** — CNA graphics "backend" terminology → **renderer** (symbols, paths, namespaces,
  CMake variables/options/targets, tests, docs, diagnostics).
- **B** — historical DirectX/D3D public renderer identities → unified **DIRECTX\***
  naming (`DX1..DX8` → `DIRECTX1..DIRECTX8`, `D3D9..D3D12` → `DIRECTX9..DIRECTX12`),
  plus **OPENGLES → OPENGLES3**. There is deliberately no DIRECTX4 (CNA has no such
  identity). `DIRECT2D`, `FREEDIRECT`, `OPENGLES1` are unchanged.
- **C** — the active CNA extension preprocessor macros: marker `NOXNA` → **`CNAEXT`**,
  feature gate `CNA_NOXNA` → **`CNA_CNAEXT`** (plus the umbrella target and directly
  related build/probe surfaces).

The public renderer identity count stays exactly **41** before and after.

## Mechanics

All broad edits are performed by the committed engine
`modularization/renderer-naming/tools/apply_renames.py`, which

- applies an ordered, word-boundary-aware rule list per pass (`a`, `b1`, `b2`, `c`);
- limits edits to an explicit ACTIVE scope (below), leaving historical evidence alone;
- sentinel-protects third-party/upstream and other-subsystem symbols;
- emits a per-rule hit log (`maps/<pass>-rule-hits.tsv`) and a path move plan
  (`maps/<pass>-moves.tsv`) — these are the machine-readable old → new maps;
- performs file/directory renames through `git mv` only.

Reconciliation compares `baseline/` and `after/` captures
(`modularization/tools/capture_inventory.py`) through the maps:
every disappeared public declaration/test name must be one explicitly mapped rename,
every appearing one the corresponding replacement — zero unrelated loss/additions
(`tools/reconcile_naming.py`).

## Scope classification

**FULL scope (terminology + identity + macro renames apply):**
`modules/{core,math,graphics,graphics-ext,runtime,media,content,storage}`,
`modules/renderers/**`, `cmake/**`, `CMakeLists.txt`, `CMakePresets.json`, `main.cpp`,
`scripts/**`, `tests/**`, `examples/**`, `tools/**` (graphics-related trees),
`docs/**` except the input/devices docs below, `.github/workflows/*` (graphics jobs),
`README.md`, `CLAUDE.md`, `CHECKLIST.md`, `Doxyfile`.

**COMPOUND-ONLY scope** (only unambiguous graphics tokens — `*GraphicsBackend*`,
`CNA_GRAPHICS_BACKEND`, `CNA_BACKEND_*`, `CNA/Internal/Backends`, `CNA::Internal::Backends`
— plus the Phase-C macro family; the bare word "backend" is left alone because these
subsystems own their own backend concepts):
`modules/{input,audio,devices,devices-ext,net,gamer-services}`,
`docs/input-*.md`, `docs/devices*`, `docs/platform-input-notes.md`,
`.github/workflows/{input-ci,devices-tests}.yml`.

**EXCLUDED (historical evidence / third-party / ledgers — not rewritten):**
`audit/**`, `modularization/**` (previous campaigns; this campaign's own tree is new),
`remediation/**`, `integration/**`, `tasks/**`, `third_party/**`, `vendor/**`,
`dx*-spike/**` (committed spike records), `plan_*.md`, `NEXT*.md` (targeted edits to
`NEXT.md` only), `AUDIT.md`, `TODO.md`, `known_bugs.md`, `RAM.md`, `programs.md`,
`cnj.md`, `xnb.md`, `gltfissues.md`, `input_noxna.md`, `input_noxna_progress.md`,
`noxna_devices.md`, `NOTICE.md`, `THIRD_PARTY_NOTICES.md`, `LICENSE`, `header.txt`.
`plans/MODULARIZATION_PLAN.md` and `FUTURE.md`/`NEXT.md` receive targeted current-state edits
in the docs commit, not mechanical rewrites.

## Intentionally unchanged names (keep list)

- **Native Microsoft Direct3D vocabulary** — `ID3D11Device`, `D3D12_RESOURCE_DESC`,
  `d3d11.h`, link libraries `d3d9/d3d11/dxgi/...`, `D3DCompile`, HLSL profile names, DLL
  names, DXVK/vkd3d tool names (`run-wine-dxvk.sh`, `run-wine-dxvk9.sh`,
  `run-wine-vkd3d.sh`).
- **`modules/renderers/common/d3d` (D3DCommon)** — real shared Direct3D-native helper
  code; keeps its name, namespace `D3DCommon`, and target basename `d3dcommon`.
- **D3D-native helper/implementation classes** inside the `directx9..directx12`
  families (`D3D11Buffers`, `D3D11SamplerCache`, `D3D9EffectRenderer`,
  `D3D12TextureRenderer`, …): they wrap actual Direct3D API objects and deliberately
  retain their `D3D<N>` prefixes. Only the renderer identity class
  (`D3D<N>GraphicsBackend` → `DirectX<N>Renderer`), the selection surface, paths,
  namespaces, targets, registrations and docs flip to `DirectX<N>`.
  In `DX1..DX8` families `Dx<N>` was never a native API name, so ALL `Dx<N>*`
  identifiers flip to `DirectX<N>*`.
- **Skia upstream API**: `GrBackendRenderTarget(s)`, `GrBackendSurface`,
  `GrGLBackendSurface`, `SkSurfaces::WrapBackendRenderTarget`.
- **sokol upstream API**: `sg_backend`, `sg_query_backend`, `SG_BACKEND_*`, and the
  `CNA_SOKOL_API` native-API axis values (including its `"D3D11"` string — that is
  sokol's native API name, not a CNA identity).
- **Other CNA subsystems' backend concepts** (input/devices/net/audio):
  `SdlGamepadBackend`, `ISystemKeyboardBackend`, `ICameraBackend`, `SdlTrayBackend`,
  `ENetBackend`, `AndroidCompassBackend`, prose "audio backend", … — unrelated
  non-graphics backend terminology, unchanged.
- **`EasyGL`** stays the internal shared GL implementation identity
  (`CNA_RENDERER_EASYGL`, `cna_renderer_easygl`, `EasyGLRenderer`); it is not a public
  identity and is not renamed to OPENGLES3.
- `DIRECT2D`, `FREEDIRECT`, `OPENGLES1` public identities unchanged.
- Historical spike directories `dx*-spike/` and historical plan/audit files keep the
  names that were accurate when written.

## Phase C structure (two distinct macros, kept distinct)

- `NOXNA` (always-defined empty **marker** on extension declarations; `[[deprecated]]`
  under `CNA_STRICT_XNA_API`) → `CNAEXT`. `CNA_STRICT_XNA_API` itself is unchanged
  (it is the strictness switch, not the extension macro).
- `CNA_NOXNA` (CMake option + compile definition **gating** the `CNA::Graphics`
  engine-layer TUs in graphics-ext) → `CNA_CNAEXT`. They must not merge into one macro:
  the marker is defined in every TU via `CNAHelper.hpp`, so a shared name would make
  every `#ifdef` gate unconditionally true.
- Umbrella target `cna_noxna` / `CNA::NoXna` → `cna_cnaext` / `CNA::CnaExt`
  (a public build-surface rename, part of the sanctioned compatibility break; the
  composition over `CNA::GraphicsExt` + `CNA::DevicesExt` is unchanged).
- `tests/modules/probe_noxna.cpp` → `probe_cnaext.cpp`,
  `examples/noxna_settings_example.cpp` → `cnaext_settings_example.cpp`,
  `NOXNA.md` → `CNAEXT.md`, `modules/input/src/NoXna/` → `modules/input/src/CnaExt/`
  (a source-area directory named after the macro; module boundaries untouched).
- Historical ledgers `input_noxna.md`, `input_noxna_progress.md`, `noxna_devices.md`
  keep their names and content (historical evidence).

## Commit plan

0. evidence: this design + engine + baseline captures;
1. `refactor(graphics)`: backend → renderer terminology (pass `a`);
2. `refactor(renderers)`: DirectX identity normalization (pass `b1`);
3. `refactor(renderers)`: OPENGLES → OPENGLES3 (pass `b2`);
4. `refactor(extensions)`: NOXNA → CNAEXT (pass `c`);
5. `test`: reconciliation captures + gates;
6. `docs`: migration document + targeted roadmap/status updates.
