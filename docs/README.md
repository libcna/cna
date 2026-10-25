# docs/ index

This index exists so a reader (human or AI agent) can tell what's current in `docs/` without
opening every file. It groups files by topic and flags
which ones are known-current vs. historical/dated. Entries not explicitly flagged have not been
individually re-verified in the 2026-07-11 documentation pass that produced this index — treat
their currency with normal caution (check the file's own "last updated"/date header before relying
on it) rather than assuming either way.

## Start here

- **[`installed-cpp-sdk.md`](installed-cpp-sdk.md)** — opt-in binary C++ SDK, exported
  CNA/Sharp Runtime modules, relocation, C API coexistence and install regression gate.
- **[`binding.md`](binding.md)** — language bindings: why every language goes through the C ABI,
  the boundary rules, Sharp Runtime's place, how to design and test a binding, and what CNA.NET
  may claim. The ABI contract itself is [`c-api/`](c-api/README.md).
- **[`../handoff.md`](../handoff.md)** (repo root) — the current state of the project. Its §5 is the
  **one** authoritative list of current known bugs and limitations; treat it as the source of truth
  for "is X still broken" over any dated snapshot below. Nothing else in the repository keeps a live
  bug list.
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
- **`graphics-renderer-feature-matrix.md`** — current per-renderer Graphics feature status.
  Start here for "does renderer X support feature Y."
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
- **[`webgpu-renderer.md`](webgpu-renderer.md)** — current status, build instructions and explicit
  limitations for the `WEBGPU` renderer; detailed remaining work is in `../plans/plan_webgpu.md`.
- **[`xna-4-runtime-member-coverage.md`](xna-4-runtime-member-coverage.md)** — current
  Microsoft-reference runtime type and member census, with every missing declaration listed;
  the separate [Content Pipeline parity report](xna-content-pipeline-parity-report.md) covers build-time APIs.
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
  (Tasks 885/886) corrected from stale ❌ to ✅.
- `depthstencilstate-support.md` — refreshed 2026-07-11.
- `sampler-state-support.md` — **refreshed 2026-07-11**: mip-level `SetData` (Tasks 924-926) and
  EasyGL anisotropic filtering (Task 918) corrected from stale ❌ to ✅/fixed.
- `rasterizerstate-support.md` — Phase 38 audit; see `graphics-renderer-feature-matrix.md` for the
  current per-renderer `DepthBias` status.
- `model-content-pipeline-support.md` — current as of Task 916 (2026-07-09); honestly documents
  real remaining content-pipeline-loader gaps (no bone hierarchy, no `ParentBone`/`BoundingSphere`).
- `occlusionquery-support.md` — per-renderer `OcclusionQuery` support.
- `rendertarget-support.md`, `texture3d-texturecube-support.md`, `surface-format-support.md`,
  `texture-stream-formats.md`, `vertex-format-support.md`, `spritefont-support.md`,
  `viewport-displaymode-adapter-support.md` — not re-verified in the 2026-07-11 pass.
- `shader-effect-vs-fx-bytecode.md` — custom GLSL `ShaderEffect` versus compiled XNA effect
  bytecode, and which renderers execute the latter; `fx-bytecode-support-plan.md` is the original
  (historical) plan for that work.

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
- `xna_culling_compatibility_audit.md`, `xna_depth_occlusion_compatibility_audit.md` — recent
  (Tasks 954/955, 2026-07-11), cross-repo `../cna-samples` investigation write-ups; current as of
  their own dates. Their screenshots were removed on 2026-10-11 and remain in Git history.

## Platform / renderer limitations

- `android-graphics-limitations.md`, `web-emscripten-graphics-limitations.md` — per-platform
  Graphics constraints (Emscripten, Android NDK).
- **[`apple-platforms.md`](apple-platforms.md)** — macOS and iOS: build instructions, the iOS
  renderer allow-list, bundle/lifecycle/storage behavior, and an explicit per-claim evidence
  boundary (macOS has a native CI gate; iOS final-links for device and runs a one-frame smoke app
  in the simulator, but still lacks physical-device and feature evidence). Task breakdown is in
  `../plans/plan_apple.md`.
- **[`ascii-post-process-effect.md`](ascii-post-process-effect.md)** — `CNA::Graphics::AsciiPostProcessEffect`,
  the renderer-neutral ASCII/glyph-grid post-process effect (`modules/graphics-ext/`) that replaced
  the former `ASCII` graphics-renderer identity.
- `ascii-renderer.md` — **historical**: completeness status for the former `ASCII` (SDL-windowed
  retro glyph-grid) graphics renderer, removed 2026-08 in favor of the post-process effect above;
  see `../plans/plan_ascii.md` for the full historical task-by-task detail.
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

## Other

- [`renderer-expansion-candidates.md`](renderer-expansion-candidates.md) — research only, never a
  roadmap: the renderer set is curated (`renderer-registry.md`) and this catalog authorizes nothing.

- `avatars.md` — the XNA avatar API on original CNA avatars: description encoding, rendering,
  animations and the asset catalog; `avatar-demos.md` lists the avatar demos.
- `skinned-model-ext.md` — `SkinnedModelEXT`, the generic GPU-skinned model extension, a separate
  system from `Model`/`ModelMesh` (see `model-content-pipeline-support.md`'s own note).
- `gdm-coverage.md` — `GraphicsDeviceManager` coverage.
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
