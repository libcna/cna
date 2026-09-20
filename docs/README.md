# docs/ index

229 Markdown documents in `docs/` (including this index) — this index exists so a reader (human or
AI agent) can tell what's current without opening every file. It groups files by topic and flags
which ones are known-current vs. historical/dated. Entries not explicitly flagged have not been
individually re-verified in the 2026-07-11 documentation pass that produced this index — treat
their currency with normal caution (check the file's own "last updated"/date header before relying
on it) rather than assuming either way.

## Start here

- **`../NEXT.md`** (repo root) — the single most reliable, actively-maintained document in the
  repository. §5 is the current known-bugs-and-limitations list; treat it as the source of truth for
  "is X still broken" over any dated snapshot below.
- **[`diagnostics.md`](diagnostics.md)** — CNA's OFF/STATS/FULL profiler architecture, public API,
  renderer-independent engine metrics, bounded event/trace formats, resource accuracy rules, and
  Inspector provider boundary. Performance methodology and measured cost are in
  [`diagnostics-benchmark.md`](diagnostics-benchmark.md).
- **[`inspector.md`](inspector.md)** — the optional authenticated out-of-process CNA Inspector,
  compact protocol, local browser UI, activation/security model, capabilities, and limitations.
  Inspector overhead measurements are in [`inspector-benchmark.md`](inspector-benchmark.md).
- **`graphics-renderer-feature-matrix.md`** — current per-renderer Graphics feature status
  (SDL_Renderer/EasyGL/Vulkan/DirectX). Start here for "does renderer X support feature Y."
- **[`renderer-capability-profiles.md`](renderer-capability-profiles.md)** — the additive detailed
  feature/limit/per-format query model, its generated English limitations report and the matching
  C ABI. Start here when the legacy 64-bit capability summary is too coarse.
- **[`gltf-renderer-stride-conformance.md`](gltf-renderer-stride-conformance.md)** — current
  seven-layout audit and automated evidence across STUB, HEADLESS, OpenGL ES 3 and Vulkan.
- **[`gltf-renderer-pbr-fallbacks.md`](gltf-renderer-pbr-fallbacks.md)** — five-map native binding
  ABIs and semantic neutral textures, audited across every PBR-capable renderer implementation.
- **[`renderer-registry.md`](renderer-registry.md)** — the canonical list of the **50** public
  renderer identities (enum, CMake selector, compile definition, factory, platform/dependency
  gate). Start here for "which renderers does CNA have."
- **[`cnaext-engine-layer.md`](cnaext-engine-layer.md)** — the `CNA::Graphics` engine layer (HDR
  pipeline, post-process passes, shadows, sky, image-based lighting, materials, instancing/LOD,
  compute), which lives behind the `CNA_CNAEXT` CMake option and is **OFF by default**. Start here
  for "what does the engine layer do on renderer X"; the design is `../CNAEXT.md`, the task backlog
  `../plans/plan_modern.md`, the running ledger `../NEXT_modern.md`, the measurements
  [`cnaext-perf.md`](cnaext-perf.md), and the fifteen-minute introduction
  [`cnaext-getting-started.md`](cnaext-getting-started.md).
- **[`renderer-expansion-candidates.md`](renderer-expansion-candidates.md)** — surveyed catalog of
  **41** possible future renderer identities, screened against the live registry and against the
  "no alias identities" rule, plus the list of things that must *not* become identities. A
  catalog only: it authorizes nothing, exactly like `../FUTURE.md`.
- **[`webgpu-renderer.md`](webgpu-renderer.md)** — current status, build instructions and explicit
  limitations for the experimental fifth renderer; detailed remaining work is in `../plans/plan_webgpu.md`.
- **[`canvas-renderer.md`](canvas-renderer.md)** — current status for the Emscripten-only HTML Canvas
  2D renderer, incl. a manual browser verification checklist (this dev loop has no real browser DOM
  to pixel-verify against); detailed task breakdown is in `../plans/plan_canvas.md`.
- **[`html-dom-renderer.md`](html-dom-renderer.md)** — current status for the Emscripten-only HTML DOM
  renderer, which renders SpriteBatch output as pooled CSS-transformed `<div>` elements instead of
  rasterizing into a canvas; detailed task breakdown is in `../plans/plan_html_dom.md`.
- **[`xna-4-runtime-member-coverage.md`](xna-4-runtime-member-coverage.md)** — current
  Microsoft-reference runtime type and member census, with every missing declaration listed;
  the separate [Content Pipeline parity report](xna-content-pipeline-parity-report.md) covers build-time APIs.
- **[`xna-4-api-coverage.md`](xna-4-api-coverage.md)** — per-class Graphics notes and historical
  estimates; use the current runtime member census above for API-surface percentages.
- **[`migration-guide.md`](migration-guide.md)** — practical guide for porting an existing XNA/FNA
  game to CNA; consult the current content and effect documentation for format support.
- **[`content-pipeline.md`](content-pipeline.md)** — the CNA-native build-time
  Importer -> Processor -> Content Type Writer system above frozen CNB: `cna-content`, built-in
  source routes, dependency/XREF semantics, incremental manifests, determinism, atomic publication,
  Unicode paths, custom extensions, and the exact stable/experimental boundary.
- **[`xna-content-pipeline-compat-api.md`](xna-content-pipeline-compat-api.md)** — the design
  contract of the `Microsoft::Xna::Framework::Content::Pipeline` façade over that engine
  (`plans/plan_xnapipeline_parity.md`); `xna-content-pipeline-parity-report.md` is the generated
  member-by-member parity report.
- **[`xna-content-pipeline-migration.md`](xna-content-pipeline-migration.md)** — porting an
  existing XNA content project: building the `.contentproj` you already have, the source-directory
  and custom-compiler routes, `.xnb` versus `.cnb`, custom importers/processors/writers, the `.xml`
  route, and what behaves differently on purpose. The content half of `migration-guide.md`.
- **[`xna-content-pipeline-components.md`](xna-content-pipeline-components.md)** — generated
  reference for every built-in importer, every extension it declares, every processor and every
  processor property, with the XNA default each answered and CNA's C++ spelling of it.
- **[`xna-content-pipeline-final-audit.md`](xna-content-pipeline-final-audit.md)** — generated: the
  twenty-six conditions the pipeline parity mission set for calling the local work done, each
  checked against the file or the named test that is its evidence.
- **[`xna-intermediate-xml-format.md`](xna-intermediate-xml-format.md)** — the XNA 4.0
  intermediate XML format as measured by running the genuine `IntermediateSerializer`; the
  specification CNA's serializer and `XmlImporter` implement, backed by the corpus in
  `tests/reference/xna40/intermediate/`.

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
- `rasterizerstate-support.md` — Phase 38 audit; see `graphics-renderer-feature-matrix.md` for the
  current per-renderer `DepthBias` status.
- `model-content-pipeline-support.md` — current as of Task 916 (2026-07-09); honestly documents
  real remaining content-pipeline-loader gaps (no bone hierarchy, no `ParentBone`/`BoundingSphere`).
- `occlusionquery-support.md` — current; tracks the Task 447/854 Vulkan fix correctly.
- `rendertarget-support.md`, `texture3d-texturecube-support.md`, `surface-format-support.md`,
  `texture-stream-formats.md`, `vertex-format-support.md`, `spritefont-support.md`,
  `viewport-displaymode-adapter-support.md` — not re-verified in the 2026-07-11 pass.
- `shader-effect-vs-fx-bytecode.md`, `fx-bytecode-support-plans/plan.md` — planning docs for the
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
  `../plans/plan_apple.md`.
- `sdl-renderer-2d-completeness.md` — SDL_Renderer's own full Phase 70 2D audit.
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
  see `../plans/plan_ascii.md` for the full historical task-by-task detail.
- `freedirect-renderer.md` — FreeDirect (DirectDraw via the `../free-direct` sibling; named `DIRECTX3` before 2026-08-04)'s own completeness status,
  current as of `plans/plan_freedirect.md`'s Phase X1-X7 closure (2026-07-15).
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
`devices-thread-safety.md`, `cna-devices-camera-design.md`, `location-future-plans/plan.md` — not in
scope for the 2026-07-11 Graphics documentation-accuracy pass; check each file's own date.

## Other

- `avatar-real-rendering-ext.md` — the Avatar real-rendering extension (`SkinnedModelEXT`), a
  separate system from `Model`/`ModelMesh` (see `model-content-pipeline-support.md`'s own note).
- `gdm-coverage.md` — `GraphicsDeviceManager` coverage.
- **[`releasing.md`](releasing.md)** — where the version lives (one place: `project(CNA VERSION
  …)` plus `CNA_VERSION_PRERELEASE`), what derives from it, which numbers are deliberately *not*
  the product version (the C ABI version, XNA 4.0), and the step-by-step release checklist.
  Written 2026-08-20 for the first tag, `v0.1.0-alpha.1`.
- **[`build-performance.md`](build-performance.md)** — fast local/unit/release presets, the
  target-scoped compiler/linker/instrumentation policy, ccache operation, and the benchmark
  protocol for evaluating further build-speed work.

---

*This index was written 2026-07-11 as part of a documentation-accuracy pass (see `../AUDIT.md` and
`../NEXT.md` for what changed). It is a map, not a guarantee — a file listed above without a
"refreshed"/"current" note may still contain stale claims that simply weren't hit by this pass.
When in doubt, prefer `../NEXT.md` §5 and `graphics-renderer-feature-matrix.md` over any file below.*
