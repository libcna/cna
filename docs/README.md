# docs/ index

169 Markdown documents in `docs/` (including this index) — this index exists so a reader (human or
AI agent) can tell what's current without opening every file. It groups files by topic and flags
which ones are known-current vs. historical/dated. Entries not explicitly flagged have not been
individually re-verified in the 2026-07-11 documentation pass that produced this index — treat
their currency with normal caution (check the file's own "last updated"/date header before relying
on it) rather than assuming either way.

## Start here

- **`../NEXT.md`** (repo root) — the single most reliable, actively-maintained document in the
  repository. §5 is the current known-bugs-and-limitations list; treat it as the source of truth for
  "is X still broken" over any dated snapshot below.
- **`graphics-renderer-feature-matrix.md`** — current per-renderer Graphics feature status
  (SDL_Renderer/EasyGL/Vulkan/Bgfx). Start here for "does renderer X support feature Y."
- **[`gltf-renderer-stride-conformance.md`](gltf-renderer-stride-conformance.md)** — current
  seven-layout audit and automated evidence across STUB, HEADLESS, OpenGL ES 3 and Vulkan.
- **[`gltf-renderer-pbr-fallbacks.md`](gltf-renderer-pbr-fallbacks.md)** — five-map native binding
  ABIs and semantic neutral textures, audited across every PBR-capable renderer implementation.
- **[`renderer-registry.md`](renderer-registry.md)** — the canonical list of the **47** public
  renderer identities (enum, CMake selector, compile definition, factory, platform/dependency
  gate). Start here for "which renderers does CNA have."
- **[`cnaext-engine-layer.md`](cnaext-engine-layer.md)** — the `CNA::Graphics` engine layer (HDR
  pipeline, post-process passes, shadows, sky, image-based lighting, materials, instancing/LOD,
  compute), which lives behind the `CNA_CNAEXT` CMake option and is **OFF by default**. Start here
  for "what does the engine layer do on renderer X"; the design is `../CNAEXT.md`, the task backlog
  `../plan_modern.md`, the running ledger `../NEXT_modern.md`, the measurements
  [`cnaext-perf.md`](cnaext-perf.md), and the fifteen-minute introduction
  [`cnaext-getting-started.md`](cnaext-getting-started.md).
- **[`tinygl-renderer.md`](tinygl-renderer.md)** — capability boundary for `TINYGL`, the
  fixed-function CPU OpenGL renderer (C-Chads/tinygl); task breakdown in `../plan_tinygl.md`, and
  the pre-implementation probe in `../tinygl-spike/README.md`.
- **[`renderer-expansion-candidates.md`](renderer-expansion-candidates.md)** — surveyed catalog of
  **41** possible future renderer identities, screened against the live registry and against the
  "no alias identities" rule, plus the list of things that must *not* become identities. A
  catalog only: it authorizes nothing, exactly like `../FUTURE.md`.
- **[`webgpu-renderer.md`](webgpu-renderer.md)** — current status, build instructions and explicit
  limitations for the experimental fifth renderer; detailed remaining work is in `../plan_webgpu.md`.
- **[`sokol-renderer.md`](sokol-renderer.md)** — capability boundary, build options and known
  limitations for the experimental `sokol_gfx` renderer (a pixel-verified 2D baseline; no 3D path,
  render targets or custom effects yet); task breakdown is in `../plan_sokol.md`.
- **[`diligent-renderer.md`](diligent-renderer.md)** — capability boundary, build options, the
  runtime device-type selection (`CNA_DILIGENT_DEVICE`) and known limitations for the experimental
  Diligent Engine renderer, the one renderer whose native graphics API is chosen at run time rather
  than by the CMake option; task breakdown is in `../plan_diligent.md`.
- **[`skia-renderer.md`](skia-renderer.md)** — current verified CPU-raster 2D capability boundary,
  dependency policy, tests, and explicit direct/emulation decisions; the 249-entry API ledger is
  [`skia-easygl-parity-ledger.md`](skia-easygl-parity-ledger.md), active work is in
  `../plan_skia.md`, Skia-only continuity is in `../NEXT_skia.md`, and the fresh-checkout procedure
  is [`skia-developer-build.md`](skia-developer-build.md). The accepted raster-versus-GPU decision
  and future acceleration reopening gate are in
  [`skia-surface-mode-adr.md`](skia-surface-mode-adr.md); the final CPU-raster checklist is
  [`skia-release-gate.md`](skia-release-gate.md). The checked routing inventory for the active
  post-baseline expansion is
  [`skia-successor-contract-matrix.md`](skia-successor-contract-matrix.md); its shared checked
  allocation and oracle rules are
  [`skia-successor-resource-oracles.md`](skia-successor-resource-oracles.md). Arbitrary blend work
  is anchored by the explicit [`skia-source-alpha-contract.md`](skia-source-alpha-contract.md).
  The normative 27-value format layout, sampling and renderability design is
  [`skia-surface-format-matrix.md`](skia-surface-format-matrix.md).
  The internal all-selector implementation is documented in
  [`skia-generated-blender.md`](skia-generated-blender.md) and its exact raster surface is promoted
  by SKIA-124. Checked 2D mip storage is documented in the successor resource policy; public mip
  construction remains gated by SKIA-126 and later tasks.
- **[`canvas-renderer.md`](canvas-renderer.md)** — current status for the Emscripten-only HTML Canvas
  2D renderer, incl. a manual browser verification checklist (this dev loop has no real browser DOM
  to pixel-verify against); detailed task breakdown is in `../plan_canvas.md`.
- **[`glide-renderer.md`](glide-renderer.md)** — build/runtime setup and current native
  fixed-function 2D plus constrained color/textured-vertex 3D scope of the Windows-only historical
  Glide 3.x renderer, which dynamically loads an external emulator DLL.
- **[`html-dom-renderer.md`](html-dom-renderer.md)** — current status for the Emscripten-only HTML DOM
  renderer, which renders SpriteBatch output as pooled CSS-transformed `<div>` elements instead of
  rasterizing into a canvas; detailed task breakdown is in `../plan_html_dom.md`.
- **`xna-4-api-coverage.md`** — current per-class Graphics coverage table plus the overall
  per-namespace XNA 4.0 API-surface numbers (227/245 = 92.7%, computed 2026-07-11).
- **[`migration-guide.md`](migration-guide.md)** — practical guide for porting an existing XNA/FNA
  game to CNA; the two gaps that block most real ports (`.xnb`, compiled `.fx` bytecode) are at
  the top.

## Graphics — per-effect / per-feature support matrices

Mostly written during Phases 35-55 (Tasks ~290-500), spot-checked and refreshed 2026-07-11 where
noted; not all rows in all of these have been re-verified against current source since their
original phase closed — check each file's own status banner/date.

- `basiceffect-support.md`, `alphatesteffect-support.md`, `dualtextureeffect-support.md`,
  `environmentmapeffect-support.md`, `skinnedeffect-support.md` — stock-effect conformance.
  **Refreshed 2026-07-11**: fog rows (Task 899) and `BasicEffect`'s multi-light/specular rows
  (Tasks 885/886) corrected from stale ❌ to ✅; a stale Vulkan `BlendState` mention in
  `dualtextureeffect-support.md` §3 also corrected.
- `depthstencilstate-support.md` — **refreshed 2026-07-11**: Vulkan's stencil-test pipeline (Task
  870) and `ReferenceStencil` (Task 872, Vulkan-only) corrected from stale ❌ to ✅/fixed.
- `sampler-state-support.md` — **refreshed 2026-07-11**: mip-level `SetData` (Tasks 924-926) and
  EasyGL anisotropic filtering (Task 918) corrected from stale ❌ to ✅/fixed.
- `rasterizerstate-support.md` — Phase 38 audit; Bgfx `DepthBias` status here predates Task 767's
  later fix (see `graphics-renderer-feature-matrix.md` instead for current Bgfx `DepthBias` status).
- `model-content-pipeline-support.md` — current as of Task 916 (2026-07-09); honestly documents
  real remaining content-pipeline-loader gaps (no bone hierarchy, no `ParentBone`/`BoundingSphere`).
- `occlusionquery-support.md` — current; tracks the Task 447/854 Vulkan fix correctly.
- `rendertarget-support.md`, `texture3d-texturecube-support.md`, `surface-format-support.md`,
  `texture-stream-formats.md`, `vertex-format-support.md`, `spritefont-support.md`,
  `viewport-displaymode-adapter-support.md` — not re-verified in the 2026-07-11 pass.
- `shader-effect-vs-fx-bytecode.md`, `fx-bytecode-support-plan.md` — planning docs for the
  compiled `.fx` bytecode gap (the single biggest real gap in the project — see the migration guide).

## Graphics — historical audits and dated snapshots

Kept for their investigation methodology and root-cause detail, not as current status:

- `graphics-compatibility-report.md` — Task 500's 2026-07-09 milestone declaration. Has a
  2026-07-11 status banner: its "5 confirmed bugs" gate is closed; treat the percentages here as a
  dated snapshot, not current.
- `easygl_bugs.md` — dated Task 227 (2026-06-27), predates hundreds of subsequent EasyGL changes.
  Has a 2026-07-11 status banner flagging 2 confirmed-stale rows (fixed inline) and noting the rest
  is spot-checked, not exhaustively re-audited.
- `coverage.md` — superseded for Graphics by `graphics-renderer-feature-matrix.md` (see that file's
  own header); its non-Graphics namespace estimates (Audio/Media/Content/Net/GamerServices) are
  the reason it's kept.
- `graphicsdevice-fna-audit.md`, `graphicsresource-fna-audit.md`, `graphics-resource-lifetime.md` —
  per-class FNA-fidelity audits, not re-verified in the 2026-07-11 pass.
- `xna_culling_compatibility_audit.md`, `xna_depth_occlusion_compatibility_audit.md` (+ their
  `_images/` folders) — recent (Tasks 954/955, 2026-07-11), cross-repo `../cna-samples`
  investigation write-ups; current as of their own dates.

## Platform / renderer limitations

- `android-graphics-limitations.md`, `web-emscripten-graphics-limitations.md` — per-platform
  Graphics constraints (Emscripten, Android NDK).
- **[`apple-platforms.md`](apple-platforms.md)** — macOS and iOS: build instructions, the iOS
  renderer allow-list, bundle/lifecycle/storage behavior, and an explicit per-claim evidence
  boundary (macOS has a native CI gate; iOS final-links for device and runs a one-frame smoke app
  in the simulator, but still lacks physical-device and feature evidence). Task breakdown is in
  `../plan_apple.md`.
- `sdl-renderer-2d-completeness.md` — SDL_Renderer's own full Phase 70 2D audit.
- **[`skia-renderer.md`](skia-renderer.md)** — the experimental Skia CPU-raster 2D renderer; unlike
  an accelerated Skia/GPU path, only its evidence-linked bounded feature table is advertised.
- `canvas-renderer.md` — the CANVAS (HTML Canvas 2D) renderer's own completeness status; unlike the
  others here, its ✅ marks mean "implemented and structurally reviewed," not "pixel-verified" — see
  the doc's own caveat.
- `html-dom-renderer.md` — the HTML_DOM (DOM/CSS) renderer's own capability status; its ✅ marks are
  backed by a real headless-browser run, not only a structural review.
- **[`ascii-post-process-effect.md`](ascii-post-process-effect.md)** — `CNA::Graphics::AsciiPostProcessEffect`,
  the renderer-neutral ASCII/glyph-grid post-process effect (`modules/graphics-ext/`) that replaced
  the former `ASCII` graphics-renderer identity.
- `ascii-renderer.md` — **historical**: completeness status for the former `ASCII` (SDL-windowed
  retro glyph-grid) graphics renderer, removed 2026-08 in favor of the post-process effect above;
  see `../plan_ascii.md` for the full historical task-by-task detail.
- `freedirect-renderer.md` — FreeDirect (formerly `DIRECTX3`; DirectDraw via the `../free-direct` sibling)'s own completeness status,
  current as of `plan_freedirect.md`'s Phase X1-X7 closure (2026-07-15).
- `glide-renderer.md` — Glide 3.x's native-API SpriteBatch and constrained color-vertex 3D path;
  runtime verification needs a separately supplied `glide3x.dll`, so this repository does not
  claim a bundled emulator.
- `fna-reference-harness.md` — the differential-testing infra (`tools/fna-reference/`) mentioned
  in `../README.md`'s verification-methodology bullet.

## Input namespace

`input-backend.md`, `input-build-and-test.md`, `input-fna-fidelity.md`,
`input-manual-verification-results.md`, `input-member-parity-matrix.md`,
`input-pre-merge-checklist.md`, `input-public-api-frozen.md`, `input-test-coverage.md`,
`demo-input-checklist.md`, `platform-input-notes.md` — not in scope for the 2026-07-11 Graphics
documentation-accuracy pass; check each file's own date before relying on it.

## Devices namespace (`Microsoft.Devices.Sensors`, CNAEXT)

`devices-android.md`, `devices-api-coverage.md`, `devices-build.md`, `devices-hardware-checklist.md`,
`devices-native-backend-design.md`, `devices_sensor_hardware_qa_template.md`,
`devices-thread-safety.md`, `cna-devices-camera-design.md`, `location-future-plan.md` — not in
scope for the 2026-07-11 Graphics documentation-accuracy pass; check each file's own date.

## Other

- `avatar-real-rendering-ext.md` — the Avatar real-rendering extension (`SkinnedModelEXT`), a
  separate system from `Model`/`ModelMesh` (see `model-content-pipeline-support.md`'s own note).
- `gdm-coverage.md` — `GraphicsDeviceManager` coverage.

---

*This index was written 2026-07-11 as part of a documentation-accuracy pass (see `../AUDIT.md` and
`../NEXT.md` for what changed). It is a map, not a guarantee — a file listed above without a
"refreshed"/"current" note may still contain stale claims that simply weren't hit by this pass.
When in doubt, prefer `../NEXT.md` §5 and `graphics-renderer-feature-matrix.md` over any file below.*
