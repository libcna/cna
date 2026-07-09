# Graphics backend feature matrix — SDL_Renderer, EasyGL, Vulkan, Bgfx

Master, up-to-date cross-backend feature matrix, written for Task 451 (Phase 51). **Supersedes
`docs/coverage.md`**, which is dated 2026-06-21 and predates almost this entire session's work
(dozens of real bugs fixed across all 4 backends, an entire SDL_Renderer 2D-only audit phase,
Model/OcclusionQuery correctness phases) and never covered SDL_Renderer at all. `docs/coverage.md`
is kept for its still-accurate non-Graphics namespace estimates (Audio/Media/Content/Net/
GamerServices); this doc is Graphics-only and current.

Status legend: ✅ correct and verified · ⚠️ partial/emulated/environment-limited · ❌ known gap, not
fixed · ⛔ BLOCKED, needs a project-owner architecture decision.

## 2D SpriteBatch / SpriteFont

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer |
|---|---|---|---|---|
| All `Draw` overloads, sort modes, rotation/flip/scale/crop | ✅ | ✅ | ✅ | ✅ (2 real bugs fixed: rotation pivot, `transformMatrix`) |
| Custom `Effect` via `Begin(effect)` | ✅ | ✅ | ✅ | ❌ throws by design (no shader stage, 2D-only backend) |
| SpriteFont — glyph placement/spacing/newline/fallback/flip | ✅ pixel-verified (Tasks 424-429) | not separately re-audited | not separately re-audited | ✅ (1 real cross-backend bug found and fixed, Task 694) |
| `TextureAddressMode::Wrap`/`Mirror` via SpriteBatch | ✅ | ✅ | ✅ | ⛔ **BLOCKED** (Tasks 686/687) |

## Stock Effects

| Feature | EasyGL | Vulkan | Bgfx |
|---|---|---|---|
| BasicEffect core (MVP, lighting, texture, vertex color) | ✅ | ✅ | ✅ |
| BasicEffect `DirectionalLight1`/`2` + `EmissiveColor` | ✅ | ✅ | ✅ |
| BasicEffect real specular highlights (`SpecularColor`/`Power`) | ✅ | ✅ | ✅ |
| AlphaTestEffect core + fog | ✅ | ✅ | ✅ |
| AlphaTestEffect `VertexColorEnabled` | ✅ | ❌ (Task 887) | ❌ (Task 887) |
| DualTextureEffect core + fog | ✅ | ✅ | ✅ |
| DualTextureEffect `VertexColorEnabled` | ❌ (Task 889) | ❌ | ❌ |
| EnvironmentMapEffect core/Fresnel/reflection | ✅ | ✅ | ✅ |
| EnvironmentMapEffect `DirectionalLight1`/`2` | ❌ (Task 890) | ❌ | ❌ |
| EnvironmentMapEffect base-lerp alpha scaling | ❌ (Task 891) | ❌ | ❌ |
| SkinnedEffect core (72-bone GPU skinning) | ✅ | ✅ | ✅ |
| SkinnedEffect `DirectionalLight1`/`2` | ❌ (Task 893) | ❌ | ❌ |
| SkinnedEffect `SpecularColor`/`SpecularPower` | ❌ (Task 894) | ❌ | ❌ |
| SkinnedEffect `WeightsPerVertex` GPU enforcement | ❌ (Task 895) | ❌ | ❌ |
| Fog, all applicable effects/pipelines | ✅ | ✅ | ✅ |
| ShaderEffect (custom shader source) | ✅ (GLSL) | ✅ (SPIR-V) | ❌ `CreateEffectBackend` returns `nullptr` |

Note: several per-effect `docs/*-support.md` files (e.g. `basiceffect-support.md`) predate Tasks
885-900's fog/lighting/specular fixes on Vulkan/Bgfx and still show some of these rows as gaps —
this matrix reflects the current, post-fix state; those individual docs are due for a refresh but
not rewritten here (out of this task's own scope).

## RenderTarget / MSAA / mip / depth

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer |
|---|---|---|---|---|
| `RenderTarget2D`/`RenderTargetCube`/MRT construction | ✅ | ✅ | ✅ | ✅ (MRT count > 1 throws by design, Task 709) |
| MSAA (both RT types) | ✅ | ✅ | ✅ (`Bgfx_RenderTarget2D_MsaaResolve` fails only under this session's Xvfb/no-DRI3 sandbox, not a code bug) | N/A (2D-only, no AA needed) |
| Mip chains (both RT types) | ✅ | ✅ | ✅ | N/A |
| Per-instance `DepthStencilFormat` fidelity | ✅ | ✅ (Task 911) | ✅ | ⚠️ emulated (echoes the requested format back, no real backing storage) |

## Texture2D / Texture3D / TextureCube

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer |
|---|---|---|---|---|
| Texture2D `SetData`/`GetData`/`FromStream`/`SaveAsPng`/NPOT | ✅ | ✅ | ✅ | ✅ (4 real bugs found and fixed) |
| Texture2D mip-level `SetData` (level > 0) | ✅ | ❌ silent no-op (Task 867) | ❌ silent no-op (Task 867) | ❌ throws by design (Task 681) |
| Texture3D/TextureCube `SetData`/`GetData`, incl. mip | ✅ | ✅ | ✅ (needed a new `GetData` readback path, Task 914) | ⛔ **BLOCKED** — construction succeeds silently with a null backend, 94-test blast radius (Task 725) |
| Texture3D/TextureCube sampled in shaders | ❌ don't inherit `Texture` (Task 863, architectural) | ❌ | ❌ | N/A |
| Non-`Color` `SurfaceFormat` for real GPU texture data | ⛔ **BLOCKED** (Task 732) | same shared-code limitation | same | same |

## GraphicsDevice state objects

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer |
|---|---|---|---|---|
| `BlendState` (all presets + custom factors/equations) | ✅ | ❌ **almost entirely fake** — hardcodes one blend equation regardless of request, confirmed 5× via pixel tests (Task 868, open) | ✅ | ✅ (2 real bugs fixed) |
| `DepthStencilState` (compare func + full stencil ops) | ✅ | ✅ (Task 870 — real per-pipeline compare-op + stencil) | not separately re-confirmed this pass | ✅ never throws (deliberate no-op, matches FNA's backend-agnostic-until-drawn model) |
| `RasterizerState` | ✅ | not separately re-confirmed this pass | not separately re-confirmed this pass | ✅ never throws |
| Per-slot `SamplerState` (16 slots) | ✅ | ✅ | ✅ | ✅ (1 real bug fixed) |
| `GraphicsDevice.ReferenceStencil` | ❌ **no backend connection, all 3** (Task 872, open) | ❌ | ❌ | N/A |
| `Clear` honors `ClearOptions::Stencil` | ❌ **ignored, all 3** (Task 871, open) | ❌ | ❌ | ⚠️ emulated |

## OcclusionQuery (Phase 50, closed this session — see `docs/occlusionquery-support.md` for full detail)

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer |
|---|---|---|---|---|
| Wired to real GPU work (`Begin`/`End`) | ✅ | ⛔ **BLOCKED**, functionally inert, always reports 0 (Task 447) | ✅ (Task 448) | N/A — throws at construction (Task 727) |
| Pixel/query correctness (visible vs. occluded) | ✅ verified both directions (Tasks 445/446) | N/A | ⚠️ can't verify in this sandbox's software GL2.1 driver; dedicated-view architecture gap open (Task 917) | N/A |

## Model (Phase 49, closed this session — see `docs/model-content-pipeline-support.md` for full detail)

| Feature | Status |
|---|---|
| Runtime API (`Model`/`ModelMesh`/`ModelMeshPart`/`ModelBone`) | ✅ fully audited/FNA-faithful, several real bugs found and fixed (Tasks 431-439) |
| Content-pipeline loading (`ModelTypeReader`) | ⚠️ real gaps — no bone hierarchy, no `ParentBone` wiring, no `BoundingSphere`/`Tag`, custom `.model.json` format is not `.xnb`-compatible (Task 440); zero test coverage of the loader itself |
| `Model` constructor root-bone-index flexibility | ❌ open (Task 916) |

## Every currently-BLOCKED task (⛔)

| Task | Backend | One-line reason |
|---|---|---|
| 447 | Vulkan | Occlusion query — backend defers ALL draws to `RecordCommandBuffer`; no way to correlate a query's Begin/End span with a deferred draw without a real design decision (3 sub-questions, none guessed at) |
| 686 | SDL_Renderer | `TextureAddressMode::Wrap` via `SpriteBatch` — no native support in the `Draw()` path used; 3 options (throw / rewrite to `SDL_RenderGeometry` / hybrid), none picked |
| 687 | SDL_Renderer | Same underlying constraint as 686, for `Mirror` — resolving 686 resolves this too |
| 725 | SDL_Renderer | `Texture3D`/`TextureCube` construction succeeds silently with a null backend; 94 existing tests rely on that silent-success behavior, so fixing needs a blast-radius-aware architecture decision |
| 732 | EasyGL | Real `SurfaceFormat` GPU forwarding conflicts with an already-shipped, already-tested `Texture::ValidateFormat` contract (Task 176) plus the public `SetData`/`GetData` API being `Color*`-only |

## Known pre-existing test-failure baseline, per backend

Confirmed most recently by this session's own regression runs (always run sequentially per
backend, never concurrently — concurrent runs have previously produced transient GPU/driver-
contention false failures):

- **EasyGL**: 3 — `EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`,
  `easy-gl-resource-smoke-tests`. Reconfirmed as recently as Task 449's own regression (4510/4513).
- **Bgfx**: 1 — `Bgfx_RenderTarget2D_MsaaResolve` (this sandbox's Xvfb has no DRI3 support; the
  test's Vulkan-renderer workaround fails to negotiate and falls back to GL 2.1, where MSAA
  doesn't resolve — an environment limitation, not a code bug). Reconfirmed via Task 448's own
  regression (4413/4414).
- **Vulkan**: last full run (Task 911) was 4369/4378 — 12 known pre-existing (5× `BlendState`/
  Task 868, several `DepthStencilState`-adjacent, `ReferenceStencil`/Task 872, 1 `DepthBias`
  sub-case) plus 3 non-deterministic `ContentManagerSkinnedModelTest.*` segfaults under this
  sandbox's Vulkan/Xvfb/llvmpipe combination (confirmed via `git stash` to be pre-existing and
  unrelated to any session's changes, and to pass cleanly in isolation). Not re-run this session
  (no Vulkan-touching task since Task 911) — treat as the best-known baseline, not a guarantee.
- **SDL_Renderer**: 13 known pre-existing, all throwing `"SDL_Renderer does not support 3D"` —
  matches this backend's accepted 2D-only architectural scope exactly (`EffectApplyTest`,
  `GraphicsDeviceValidationTest.SetRenderTargets_*`, `SkinnedModelEXTPartTest.*`,
  `ContentManagerSkinnedModelTest.*`). Confirmed via Task 915's own systematic full-suite run.

## New tracked follow-up tasks opened this session

- **Task 916** — `Model`'s constructor auto-defaults `Root` to `bones[0]`, no way to specify a
  different root bone index (low-risk, purely-additive fix, not blocked).
- **Task 917** — Bgfx occlusion queries share a view/depth buffer with other same-frame geometry
  instead of using bgfx's own dedicated-measurement-view pattern; needed for true scene-depth
  query correctness (deferred, not blocked, can't be verified in this sandbox anyway).

## See also

- `docs/coverage.md` — non-Graphics namespace estimates (Audio/Media/Content/Net/GamerServices),
  still broadly accurate; its own Graphics section is superseded by this document.
- `docs/sdl-renderer-2d-completeness.md` — SDL_Renderer's own full Phase 70 audit in verbose detail.
- `docs/model-content-pipeline-support.md`, `docs/occlusionquery-support.md` — full detail for
  those 2 systems, summarized above.
- Per-effect docs (`docs/basiceffect-support.md` etc.) — largely predate Tasks 885-900's
  fog/lighting/specular fixes on Vulkan/Bgfx; this matrix reflects the current state, those
  individual docs have not been refreshed (out of this task's own scope).
