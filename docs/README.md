# docs/ index

This index exists so a reader (human or AI agent) can tell what's current in `docs/` without
opening every file. It groups files by topic and flags
which ones are known-current vs. historical/dated. Entries not explicitly flagged have not been
individually re-verified in the 2026-07-11 documentation pass that produced this index — treat
their currency with normal caution (check the file's own "last updated"/date header before relying
on it) rather than assuming either way.

## Start here

- **[`maintainer-study-guide.md`](maintainer-study-guide.md)** — a rolling, evidence-driven route
  through the current module and renderer set: sources of truth, generated-code boundaries,
  study/change workflow, dependency-ordered phases and a first thirteen-week cycle. It replaces
  fixed multi-year calendars tied to retired renderers and deleted plans.
- **[`windows-visual-studio.md`](windows-visual-studio.md)** — the native Windows Visual Studio,
  VS Code and command-line workflow: shared presets, explicit MSVC CRT selection, vendored SDL
  cache rules, runtime deployment and the boundary of the automatic Windows CI evidence.
- **[`installed-cpp-sdk.md`](installed-cpp-sdk.md)** — opt-in binary C++ SDK, exported
  CNA/Sharp Runtime modules, relocation, C API coexistence and install regression gate.
- **[`binding.md`](binding.md)** — language bindings: why every language goes through the C ABI,
  the boundary rules, Sharp Runtime's place, how to design and test a binding, and what CNA.NET
  may claim. The ABI contract itself is [`c-api/`](c-api/README.md).
- **[`xna-fna-behavior-decisions.md`](xna-fna-behavior-decisions.md)** — measured cases where
  genuine XNA behavior overrides FNA, deliberately retained boundaries, and rejected historical
  binding findings. This is a decision ledger, not the current defect list.
- **[`../handoff.md`](../handoff.md)** (repo root) — the current state of the project. Its §5 is the
  **one** authoritative list of current known bugs and limitations; treat it as the source of truth
  for "is X still broken" over any dated snapshot below. Nothing else in the repository keeps a live
  bug list.
- **[`task-id-index.md`](task-id-index.md)** — what a task ID in a comment or commit (`XNASWEEP-231`,
  `GSC-0007`, `RRC-009`) belonged to, and where that work's surviving content lives. Every
  implementation plan has been retired; the plans are in Git history.
- **[`development-environment.md`](development-environment.md)** — the maintained Debian 13 /
  Linux development environment and system dependency guide: what the default build and its tests
  need, the sibling checkouts and submodules, and the optional feature, Wayland,
  Windows/`DIRECTX9` and debugging groups.
- **[`diagnostics.md`](diagnostics.md)** — CNA's OFF/STATS/FULL profiler architecture, public API,
  renderer-independent engine metrics, bounded event/trace formats, resource accuracy rules, and
  Inspector provider boundary. Performance methodology and measured cost are in
  [`diagnostics-benchmark.md`](diagnostics-benchmark.md).
- **[`inspector.md`](inspector.md)** — the optional authenticated out-of-process CNA Inspector,
  compact protocol, local browser UI, activation/security model, capabilities, and limitations.
  Inspector overhead measurements are in [`inspector-benchmark.md`](inspector-benchmark.md).
- **[`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md)** — reconciled
  cross-renderer Graphics feature status across all six rasterizing families plus HEADLESS/STUB.
  Start here for "does renderer X support feature Y," then use the live capability profile for the
  selected build, device and driver.
- **[`renderer-capability-profiles.md`](renderer-capability-profiles.md)** — the additive detailed
  feature/limit/per-format query model, its generated English limitations report and the matching
  C ABI. Start here when the legacy 64-bit capability summary is too coarse.
- **[`gltf-renderer-stride-conformance.md`](gltf-renderer-stride-conformance.md)** — current
  seven-layout audit and automated evidence across STUB, HEADLESS and OpenGL ES 3.
- **[`gltf-renderer-pbr-fallbacks.md`](gltf-renderer-pbr-fallbacks.md)** — five-map native binding
  ABIs and semantic neutral textures, audited across every PBR-capable renderer implementation.
- **[`link-size-architecture.md`](link-size-architecture.md)** — why a CNA program contains only
  the subsystems it uses: the link boundaries, the rules for adding features without growing
  every game, size-oriented build options, how to check a binary, and the footprint gates.
- **[`renderer-registry.md`](renderer-registry.md)** — the canonical list of the public
  renderer identities (enum, CMake selector, compile definition, factory, platform/dependency
  gate). Start here for "which renderers does CNA have."
- **[`cnaext-engine-layer.md`](cnaext-engine-layer.md)** — current standalone ASCII, CRT,
  colour-depth and DebugDraw extensions; the previous modern engine layer has been retired.
- **[`opengl33-renderer.md`](opengl33-renderer.md)** — current desktop OpenGL 3.3 core profile
  contract: EasyGL sharing, profile-specific behavior, build/test route, platform qualification and
  live-context boundaries.
- **[`webgl2-renderer.md`](webgl2-renderer.md)** — current browser WebGL 2 profile contract:
  EasyGL sharing, exact context/dialect rules, browser validation route, live capabilities and
  qualification boundary.
- **[`software-renderer.md`](software-renderer.md)** — current deterministic CPU-rasterizer
  contract: build/test route, stable architecture decisions, rendering and presentation surface,
  source-versus-compiled effect boundary, resource budgets and qualification limits.
- **[`fna3d-renderer.md`](fna3d-renderer.md)** — current Experimental FNA3D family contract:
  pinned forks, runtime OpenGL/Direct3D 11/SDL_GPU driver selection, compiled-effect route, live
  capabilities, resource boundaries and platform qualification.
- **[`webgpu-renderer.md`](webgpu-renderer.md)** — current native and browser status, build
  instructions, qualification evidence and explicit limitations for the `WEBGPU` renderer.
- **[`xna-4-runtime-member-coverage.md`](xna-4-runtime-member-coverage.md)** — current
  Microsoft-reference runtime type and member census, with every missing declaration listed;
  the separate [Content Pipeline parity report](xna-content-pipeline-parity-report.md) covers build-time APIs.
- **[`migration-guide.md`](migration-guide.md)** — practical guide for porting an existing XNA/FNA
  game to CNA; consult the current content and effect documentation for format support.
- **[`cnj-format.md`](cnj-format.md)** — the `.cnj` editable JSON content format: how
  `ContentManager` resolves `.xnb`/`.cnb`/`.cnj`/native files, the envelope, `sourceFile`
  sidecars, the per-type members, `Model` versions and custom loaders. The compiled form is
  [`cnb-format.md`](cnb-format.md).
- **[`content-pipeline.md`](content-pipeline.md)** — the CNA-native build-time
  Importer -> Processor -> Content Type Writer system above frozen CNB: `cna-content`, built-in
  source routes, dependency/XREF semantics, incremental manifests, determinism, atomic publication,
  Unicode paths, custom extensions, and the exact stable/experimental boundary.
- **[`xna-content-pipeline-compat-api.md`](xna-content-pipeline-compat-api.md)** — the design
  contract of the `Microsoft::Xna::Framework::Content::Pipeline` façade over that engine
  (`XNAPP-*`); `xna-content-pipeline-parity-report.md` is the generated
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
  All five pages were reconciled with current source and tests on 2026-10-31.
- `depthstencilstate-support.md` — refreshed 2026-07-11.
- `sampler-state-support.md` — reconciled 2026-10-31: current nine-value filter mapping,
  renderer-wide LOD/W propagation, sampler lifetime, and measured `SpriteBatch` publication;
  historical Phase 35 findings are separated from the two remaining EasyGL limitations.
- `rasterizerstate-support.md` — reconciled 2026-10-31: measured XNA culling, state freezing,
  conditional wireframe, depth bias, scissor and the distinct target-MSAA versus
  `MultiSampleAntiAlias` boundary across all renderer families.
- `model-content-pipeline-support.md` — `Model`'s FNA-audited runtime API and its four loading
  routes (`.xnb`, `.cnb`, `.cnj`, glTF) with what each builds; refreshed 2026-10-25.
- `occlusionquery-support.md` — reconciled 2026-10-31: recovered XNA lifecycle, overlapping logical
  intervals, exact-versus-visibility results and current boundaries across all renderer families.
- `graphics-resource-lifetime.md` — current ownership, event ordering, device tracking, move and
  native-retirement contract for `GraphicsResource`-derived types; reconciled 2026-10-31.
- `spritefont-support.md` — `SpriteFont`/`DrawString` behavior, UTF-8/BMP adaptation and the closed
  combined-`SpriteEffects` follow-up; reconciled 2026-10-31.
- `rendertarget-support.md`, `texture3d-texturecube-support.md`, `surface-format-support.md` —
  reconciled with their current implementations and regressions on 2026-10-31.
- `viewport-displaymode-adapter-support.md` — reconciled 2026-10-31: current adapter lifetime,
  XNA widescreen/device-selection rules, resize coverage and measured browser/Android boundary.
- `texture-stream-formats.md` — public PNG/JPEG/GIF and explicit DDS decoding contract.
- `vertex-format-support.md` — cross-renderer vertex declaration, format and semantic mapping,
  reconciled 2026-10-31.
- `shader-effect-vs-fx-bytecode.md` — reconciled 2026-10-31: renderer-dialect `ShaderEffect`
  acceptance, exact dialect and real execution across all eight families versus compiled XNA
  effect bytecode; `fx-compiled-effects.md` is the complete guide to the compiled path.

## Platform / renderer limitations

- `android-graphics-limitations.md` — Android NDK graphics constraints.
- **[`web-emscripten-graphics-limitations.md`](web-emscripten-graphics-limitations.md)** — current
  browser platform contract shared by WEBGL2/WEBGPU: final-link policy, canvas/display model,
  game-loop ownership, optional pthread ABI, persistence, context loss and evidence boundary.
- **[`apple-platforms.md`](apple-platforms.md)** — macOS and iOS: build instructions, the iOS
  renderer allow-list, bundle/lifecycle/storage behavior, and an explicit per-claim evidence
  boundary (macOS has a native CI gate; iOS final-links for device and runs a one-frame smoke app
  in the simulator, but still lacks physical-device and feature evidence), and the known gaps.
- **[`ascii-post-process-effect.md`](ascii-post-process-effect.md)** — `CNA::Graphics::AsciiPostProcessEffect`,
  the renderer-neutral ASCII/glyph-grid post-process effect (`modules/graphics-ext/`) that replaced
  the former `ASCII` graphics-renderer identity; the page also records that renderer's historical
  `ASCII-1`..`ASCII-82` campaign and migration commits.
- `fna-reference-harness.md` — the differential-testing infra (`tools/fna-reference/`) mentioned
  in `../README.md`'s verification-methodology bullet.
- **[`consoles.md`](consoles.md)** — game consoles: what NDA, homebrew and PC-handheld targets each
  require, the FNA model for licensed ports, the rules any console work follows from day one, and
  what certification asks of the framework. No console port exists.

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

- **[`location-proposal.md`](location-proposal.md)** — the proposal, with the owner's decisions of
  2026-10-25, for location support through `System.Device.Location`; nothing is implemented yet.

## Other

- `avatars.md` — the XNA avatar API on original CNA avatars: description encoding, rendering,
  animations and the asset catalog; `avatar-demos.md` lists the avatar demos.
- `skinned-model-ext.md` — `SkinnedModelEXT`, the generic GPU-skinned model extension, a separate
  system from `Model`/`ModelMesh` (see `model-content-pipeline-support.md`'s own note).
- **[`gdm-coverage.md`](gdm-coverage.md)** — current `GraphicsDeviceManager` lifecycle, reset,
  event, presentation-setting and native C API contract, including the deliberate Game-owned-device
  and logical-resize adaptations.
- **[`releasing.md`](releasing.md)** — where the version lives (one place: `project(CNA VERSION
  …)` plus `CNA_VERSION_PRERELEASE`), what derives from it, which numbers are deliberately *not*
  the product version (the C ABI version, XNA 4.0), and the step-by-step release checklist.
  Written 2026-08-20 for the first tag, `v0.1.0-alpha.1`.
- **[`build-performance.md`](build-performance.md)** — fast local/unit/release presets, the
  target-scoped compiler/linker/instrumentation policy, ccache operation, and the benchmark
  protocol for evaluating further build-speed work.

---

*This index was written 2026-07-11 and last reconciled 2026-10-10. It is a map, not a guarantee — a
file listed above without a "refreshed"/"current" note may still contain stale claims. When in
doubt, prefer `../handoff.md` §5, `renderer-registry.md` and `graphics-renderer-feature-matrix.md`
over any other file.*
