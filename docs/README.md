# docs/ index

58 files in `docs/` (including this index), no prior map — this index exists so a reader (human or
AI agent) can tell what's current without opening every file. It groups files by topic and flags
which ones are known-current vs. historical/dated. Entries not explicitly flagged have not been
individually re-verified in the 2026-07-11 documentation pass that produced this index — treat
their currency with normal caution (check the file's own "last updated"/date header before relying
on it) rather than assuming either way.

## Start here

- **`../NEXT.md`** (repo root) — the single most reliable, actively-maintained document in the
  repository. §5 is the current known-bugs-and-limitations list; treat it as the source of truth for
  "is X still broken" over any dated snapshot below.
- **`graphics-backend-feature-matrix.md`** — current per-backend Graphics feature status
  (SDL_Renderer/EasyGL/Vulkan/Bgfx). Start here for "does backend X support feature Y."
- **[`webgpu-backend.md`](webgpu-backend.md)** — current status, build instructions and explicit
  limitations for the experimental fifth backend; detailed remaining work is in `../plan_webgpu.md`.
- **[`canvas-backend.md`](canvas-backend.md)** — current status for the Emscripten-only HTML Canvas
  2D backend, incl. a manual browser verification checklist (this dev loop has no real browser DOM
  to pixel-verify against); detailed task breakdown is in `../plan_canvas.md`.
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
  later fix (see `graphics-backend-feature-matrix.md` instead for current Bgfx `DepthBias` status).
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
- `coverage.md` — superseded for Graphics by `graphics-backend-feature-matrix.md` (see that file's
  own header); its non-Graphics namespace estimates (Audio/Media/Content/Net/GamerServices) are
  the reason it's kept.
- `graphicsdevice-fna-audit.md`, `graphicsresource-fna-audit.md`, `graphics-resource-lifetime.md` —
  per-class FNA-fidelity audits, not re-verified in the 2026-07-11 pass.
- `xna_culling_compatibility_audit.md`, `xna_depth_occlusion_compatibility_audit.md` (+ their
  `_images/` folders) — recent (Tasks 954/955, 2026-07-11), cross-repo `../cna-samples`
  investigation write-ups; current as of their own dates.

## Platform / backend limitations

- `android-graphics-limitations.md`, `web-emscripten-graphics-limitations.md` — per-platform
  Graphics constraints (Emscripten, Android NDK).
- `sdl-renderer-2d-completeness.md` — SDL_Renderer's own full Phase 70 2D audit.
- `canvas-backend.md` — the CANVAS (HTML Canvas 2D) backend's own completeness status; unlike the
  others here, its ✅ marks mean "implemented and structurally reviewed," not "pixel-verified" — see
  the doc's own caveat.
- **[`ascii-backend.md`](ascii-backend.md)** — current status for the `ASCII` (SDL-windowed retro
  glyph-grid) backend; see `../plan_ascii.md` for full task-by-task detail.
- `freedirect-backend.md` — FreeDirect (formerly `DX3`; DirectDraw via the `../free-direct` sibling)'s own completeness status,
  current as of `plan_freedirect.md`'s Phase X1-X7 closure (2026-07-15).
- `fna-reference-harness.md` — the differential-testing infra (`tools/fna-reference/`) mentioned
  in `../README.md`'s verification-methodology bullet.

## Input namespace

`input-backend.md`, `input-build-and-test.md`, `input-fna-fidelity.md`,
`input-manual-verification-results.md`, `input-member-parity-matrix.md`,
`input-pre-merge-checklist.md`, `input-public-api-frozen.md`, `input-test-coverage.md`,
`demo-input-checklist.md`, `platform-input-notes.md` — not in scope for the 2026-07-11 Graphics
documentation-accuracy pass; check each file's own date before relying on it.

## Devices namespace (`Microsoft.Devices.Sensors`, NOXNA)

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
When in doubt, prefer `../NEXT.md` §5 and `graphics-backend-feature-matrix.md` over any file below.*
