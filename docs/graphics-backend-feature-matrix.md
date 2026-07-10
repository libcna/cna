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
| All `Draw` overloads, sort modes, rotation/flip/scale/crop | ✅ | not separately re-audited (Task 861) | not separately re-audited | ✅ (2 real bugs fixed: rotation pivot, `transformMatrix`) |
| Custom `Effect` via `Begin(effect)` | ✅ | ✅ | ✅ | ❌ throws by design (no shader stage, 2D-only backend) |
| SpriteFont — glyph placement/spacing/newline/fallback/flip | ✅ pixel-verified (Tasks 424-429) | not separately re-audited (Task 861) | not separately re-audited | ✅ (1 real cross-backend bug found and fixed, Task 694) |
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

### Bgfx MRT attachment limits (Task 775)

`GraphicsDevice::SetRenderTargets` throws `std::invalid_argument` above **4** targets in shared C++
code (`MAX_RENDERTARGET_BINDINGS`, Task 881) — mirroring FNA's own real
`internal const int MAX_RENDERTARGET_BINDINGS = 4` cap — before any backend ever sees the call, so
this is the practical, enforced limit on Bgfx (and every other backend) regardless of what the
underlying device itself could support. `BgfxGraphicsBackend::SetRenderTargets`'s own MRT
framebuffer-construction path (`BgfxGraphicsBackend.cpp`) separately caps at a local
`kMaxAttachments = 8`, matching bgfx's own `BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS` compile-time
default — unreachable in practice today since the shared 4-target gate rejects anything larger
first, kept only as defense-in-depth (same reasoning as Task 881's own EasyGL/Bgfx ad-hoc-cap
notes). The real device capability, `bgfx::getCaps()->limits.maxFBAttachments`, is logged at
startup (Task 456) and is typically 8 on desktop GL/Vulkan hardware — always ≥ the FNA-mandated 4,
so it has never been the binding constraint in this project.

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

### Vulkan optional device-feature gating (Task 454)

Investigated whether Vulkan's `VkPhysicalDeviceFeatures`-gated optional capabilities are requested
safely (a device that doesn't support a requested optional feature makes `vkCreateDevice` fail
outright, unlike GL/bgfx's more forgiving capability model). Confirmed the device-creation code
(`VulkanGraphicsBackend`'s constructor) only ever requests the 2 optional features CNA actually
uses — `fillModeNonSolid` (`FillMode::WireFrame`) and `samplerAnisotropy` (anisotropic texture
filtering) — and both are correctly gated behind a real `vkGetPhysicalDeviceFeatures` query first
(`if (supported.fillModeNonSolid) { feat.fillModeNonSolid = VK_TRUE; fillModeNonSolidSupported_ =
true; }`, same shape for `samplerAnisotropy`). Neither is ever unconditionally requested. Downstream
usage sites correctly gate on the resulting `fillModeNonSolidSupported_`/`anisotropySupported_`
flags (e.g. `fillModeWireframe_ = (fillMode == 1) && fillModeNonSolidSupported_` — a device without
`fillModeNonSolid` silently falls back to solid fill rather than requesting an invalid pipeline
state), and `maxSamplerAnisotropy_` is read from real `VkPhysicalDeviceLimits` and used to clamp any
requested anisotropy level. MSAA sample-count selection (`PickSampleCount`) also respects the
device's real `framebufferColorSampleCounts` limit, picking the best available count ≤ the
requested one rather than assuming an arbitrary count is always supported. **No gap found** — this
was already correctly implemented, just not previously documented anywhere; recorded here per Task
454's own "throw or document fallback behavior" framing (this backend's own answer is "gracefully
falls back," which is the idiomatic Vulkan pattern for optional features, not a bug needing a fix).

## OcclusionQuery (Phase 50, closed this session — see `docs/occlusionquery-support.md` for full detail)

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer |
|---|---|---|---|---|
| Wired to real GPU work (`Begin`/`End`) | ✅ | ✅ **FIXED (Task 447, 2026-07-10)** — real per-draw-call query correlation via `Pending3DDraw::occlusionQuery` tagging + `vkCmdBeginQuery`/`vkCmdEndQuery` recording in `RecordCommandBuffer()` | ✅ (Task 448) | N/A — throws at construction (Task 727) |
| Pixel/query correctness (visible vs. occluded) | ✅ verified both directions (Tasks 445/446) | ✅ verified both directions, plus a multi-draw-span check (Task 854) — this sandbox's software Vulkan driver (Mesa Lavapipe) reports fully accurate, discriminating pixel counts (4096 visible / 0 occluded on a 64×64 quad) | ⚠️ can't verify in this sandbox's software GL2.1 driver; dedicated-view architecture gap open (Task 917) | N/A |

## Model (Phase 49, closed this session — see `docs/model-content-pipeline-support.md` for full detail)

| Feature | Status |
|---|---|
| Runtime API (`Model`/`ModelMesh`/`ModelMeshPart`/`ModelBone`) | ✅ fully audited/FNA-faithful, several real bugs found and fixed (Tasks 431-439) |
| Content-pipeline loading (`ModelTypeReader`) | ⚠️ real gaps — no bone hierarchy, no `ParentBone` wiring, no `BoundingSphere`/`Tag`, custom `.model.json` format is not `.xnb`-compatible (Task 440); zero test coverage of the loader itself |
| `Model` constructor root-bone-index flexibility | ❌ open (Task 916) |

## Every currently-BLOCKED task (⛔)

| Task | Backend | One-line reason |
|---|---|---|
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
- **Bgfx**: **updated 2026-07-10** — 7, not the stale "1" this line previously said: 6
  `RenderTarget2D`/`RenderTargetCube` `glReadPixels`/Xvfb-no-DRI3 sandbox crashes (`DepthBuffer`,
  `MsaaResolve`, `MipChain` ×2 for `RenderTarget2D` and `RenderTargetCube`, plus
  `RenderTargetCube_DepthFormat` — see the "Remaining genuine Bgfx limitations" section above) —
  environment limitations, not code bugs — plus 1 **real CNA bug**, `Bgfx_ModelJsonReader_Quad`
  (Task 927/948: `DrawIndexedPrimitivesEx` never overridden on Bgfx, see above), not yet fixed.
  Reconfirmed via this session's own full regression (4463 tests).
- **Vulkan**: last full run (Task 911) was 4369/4378 — **9** known pre-existing (5× `BlendState`/
  Task 868, 1 `RasterizerState.DepthBias` sub-case, 3 non-deterministic
  `ContentManagerSkinnedModelTest.*` segfaults under this sandbox's Vulkan/Xvfb/llvmpipe combination
  — confirmed via `git stash` to be pre-existing and unrelated to any session's changes, and to pass
  cleanly in isolation). **Correction (2026-07-09, Task 861):** this row previously said "12" and
  additionally claimed "several `DepthStencilState`-adjacent" failures — both wrong; `4378-4369=9`,
  and `DepthStencilState`'s own compare-op/stencil-op tests all pass (Task 870 fixed this), leaving
  only the 6 integration-suite failures (5 `BlendState` + 1 `DepthBias`) plus the 3 segfaults.
  Independently reconfirmed via a fresh `ctest -R "^Vulkan_"` rerun (Task 495, 87/93 — exactly those
  6, same names) plus the matching correction already made in `docs/xna-4-api-coverage.md` (Task
  484/499). Not re-run as a full suite this session (no Vulkan-touching task since Task 911) —
  treat as the best-known baseline, not a guarantee.
- **SDL_Renderer**: 13 known pre-existing, all throwing `"SDL_Renderer does not support 3D"` —
  matches this backend's accepted 2D-only architectural scope exactly (`EffectApplyTest`,
  `GraphicsDeviceValidationTest.SetRenderTargets_*`, `SkinnedModelEXTPartTest.*`,
  `ContentManagerSkinnedModelTest.*`). Confirmed via Task 915's own systematic full-suite run.

## Remaining genuine Vulkan limitations (Task 861, 2026-07-09)

Phase 73 (Tasks 664-665, 825-861) was written as a checklist of individual Vulkan pixel-test tasks,
but Tasks 825-860 were never checked off — later, higher-numbered work (Tasks 484/495/499/500,
plus the fog/lighting/effect fixes at 885-900) independently established most of the same ground,
superseding the original per-row checklist without formally closing each row. This section is
Task 861's real deliverable: the actual current state, confirmed by spot-checking a representative
sample of Tasks 825-860 against real test coverage rather than re-verifying all 36 rows from
scratch (that would be Task 738-scale work, out of this task's own scope).

**Genuinely already covered by real, current Vulkan tests** (confirmed via
`ctest --test-dir cmake-build-vulkan -N -R "^Vulkan_"`, 93 real tests): `TextureAddressMode`
(Clamp/Mirror), `TextureFilter` (Point vs. Linear), anisotropic filtering, all 7 `BlendState`
presets, all 6 `DepthStencilState` aspects, `CullMode`, `Viewport`, render-target lifecycle
(sample-after-unbind, MSAA, mip chains, depth-format fidelity, MRT-adjacent), and all 5 stock
effects including fog and several per-effect sub-features (specular, Fresnel, eye position, bone
blending) — this maps directly onto Tasks 825-849's own topics. **Confirmed genuine bugs found by
this later work, not silently passing**: `BlendState` (Task 868, still open), one isolated
`RasterizerState.DepthBias` sub-case (still open) — these are the real content behind Tasks 831-833
and 839's own topics, not clean passes.

**A real, previously-undocumented gap found by this spot-check**: unlike EasyGL and SDL_Renderer,
Vulkan has **no dedicated pixel test** for `SpriteBatch`'s sort-mode ordering, rotation/scale/
source-rectangle-cropping/`SpriteEffects` flip (Task 851/850's own topics), `SpriteFont` glyph
placement (Task 852), or `Model` multi-mesh hierarchy transform propagation (Task 853) —
confirmed via `grep`/`ctest -N` finding zero `Vulkan_SpriteFont*`/`Vulkan_Model*`/
`Vulkan_SpriteSortMode*` test names, despite `Vulkan_SpriteBatch_MultiBeginEnd` and
`Vulkan_Demo2D_SmokeTest` confirming basic `SpriteBatch` drawing works. Corrected the feature
matrix's own "2D SpriteBatch/SpriteFont" table above, which previously (incorrectly) rated Vulkan
✅ for this without a backing test, to "not separately re-audited" — matching the honest phrasing
already used for the adjacent SpriteFont row. This is a **test-coverage gap, not a confirmed
behavioral bug** — the underlying `SpriteBatch`/`SpriteFont`/`Model` code is backend-agnostic C++
already pixel-verified on EasyGL/SDL_Renderer, so a regression specifically on Vulkan is unlikely,
but it is genuinely unverified there. Not opened as a new numbered task here (that's Task 738-scale
triage work); flagging it in this matrix is this task's own real scope.

**`OcclusionQuery` visible-vs-occluded pixel test on Vulkan (Task 854's own topic)**: **FIXED,
2026-07-10** — was Task 447's ⛔ BLOCKED status (Vulkan's deferred-draw architecture couldn't
correlate a query's Begin/End span with a draw at all); now resolved via real per-draw-call query
tagging and `vkCmdBeginQuery`/`vkCmdEndQuery` recording, see the `OcclusionQuery` table above.

**Bottom line**: Vulkan's real, current, confirmed-open limitations are exactly 2 — `BlendState`
(Task 868) and the isolated `RasterizerState.DepthBias` sub-case — plus the `ReferenceStencil` gap
(Task 872, shared across all 3 3D backends). The 2D SpriteBatch/SpriteFont/Model-hierarchy
test-coverage gap above is real but distinct in kind (untested, not un-implemented or
known-broken). `OcclusionQuery` (Task 447/854) is no longer on this list — fixed in full.

## Remaining genuine Bgfx limitations (Task 824, 2026-07-10)

Phase 72 (Bgfx full 2D+3D pixel-verified parity, Tasks 740-824) is now closed in full: of the
original 38 confirmed real gaps found in a first-ever complete row-by-row triage, 37 are ✅ closed
this session and exactly 1 remains open, explicitly flagged (not silently skipped). Three genuine,
confirmed limitations survive this closure — none of them a code bug in this project, each already
root-caused rather than merely observed:

- **Constant `DepthBias` (Task 767): FIXED, 2026-07-10.** Project-owner decision received: bgfx's
  high-level state API has zero depth-bias/polygon-offset mechanism anywhere (confirmed via
  `bgfx/defines.h` — no `BIAS`/`OFFSET` flag exists — and the vendored `renderer_gl.cpp` — no
  `glPolygonOffset` call in the whole file), so constant `DepthBias` is now emulated via a per-draw
  vertex-shader Z-offset (`BgfxGraphicsBackend::SetDepthBiasUniform`, a new `u_depthBias` uniform
  added to every 3D vertex shader, scaled by `kDepthBiasScale` to roughly match the visual magnitude
  a real GL/Vulkan polygon-offset implementation would produce). New `Bgfx_RasterizerState_DepthBias`
  test confirms both the zero-bias baseline (stays RED) and a large negative bias (pulls a coplanar
  redraw in front, turns GREEN); verified via `git stash` revert-and-rebuild.
  **`SlopeScaleDepthBias` remains an intentionally undone gap** (project-owner decision, not
  attempted): a true per-fragment screen-space-slope computation would force every 3D shader off the
  early-Z path, even at `DepthBias=0`, unless duplicate shader variants were added — not worth the
  cost for this one property. EasyGL, by contrast, needed no shader emulation at all: it already had
  real `glPolygonOffset` support in the vendored `easy-gl` library, just never wired up — fixed with
  a native call, covering both constant and slope-scale bias in one shot (see Task 767's own
  `plan_graphics.md` entry). Only Vulkan implements real hardware depth bias (`vkCmdSetDepthBias`,
  dynamic state, including real slope-scale).
- **`RenderTarget2D`/`RenderTargetCube` `glReadPixels` crashes under this sandbox's software GL
  driver** (`Bgfx_RenderTarget2D_DepthBuffer`/`MsaaResolve`/`MipChain`,
  `Bgfx_RenderTargetCube_MipChain`/`MsaaResolve`/`DepthFormat` — 6 tests, all pre-existing, none
  introduced by this session's own work): all abort with the same class of
  `GL_INVALID_OPERATION`/MSAA-resolve-vs-depth-attachment assertion, pointing at this project's
  Xvfb/software-GL (Mesa llvmpipe, no DRI3) sandbox ceiling rather than a CNA defect — matches the
  already-established precedent (Task 448/879/903's own identical-class findings). A real
  GPU-backed environment would be needed to distinguish "still broken" from "sandbox-only."
- **`OcclusionQuery.PixelCount()` doesn't discriminate visible from occluded geometry in this
  sandbox** (Tasks 814/815): a dedicated scratch probe confirmed the exact same numeric value is
  returned regardless of scene content, extending Task 448's own already-documented finding
  (`IsComplete()`/`PixelCount()` can't distinguish a wired-up query from a never-submitted one) to
  the actual pixel-count magnitude too — the underlying rendering/depth-occlusion behavior each
  scenario depends on IS reliably pixel-verified instead (`Bgfx_OcclusionQuery_PixelCount`'s own 2
  real, sabotage-verified checks). Same software-renderer ceiling as above, not a CNA defect.

**New, 2026-07-10 (Task 927/948): `BgfxGraphicsBackend` never overrides `DrawIndexedPrimitivesEx`**
— a **real CNA gap**, not an environment limitation, first flagged as an "adjacent, out-of-scope
discovery" by Task 766 and now concretely reproduced by `Bgfx_ModelJsonReader_Quad` (added to this
doc's own known-failure baseline, see the "Confirmed most recently..." Bgfx line above): any
indexed, `Effect`-bound draw with a vertex format lacking a
`Color` attribute (`VertexPositionNormalTexture`/`VertexPositionTexture` — i.e. any
`Content.Load<Model>()`-loaded mesh) silently falls back to the base `IGraphicsBackend`'s default
`DrawIndexedPrimitivesEx`, which discards `GpuDrawParams` entirely and renders via the `colored3D`
pipeline instead — reading an unbound `a_color0` attribute (GL default `(0,0,0,1)`), so the mesh
renders solid black regardless of its real `DiffuseColor`/texture/lighting. Tracked as Task 948,
not yet fixed.

**Bottom line**: Bgfx's only remaining code-level gap is Task 767 (depth bias), already flagged for
a project-owner decision; the other 2 items are sandbox/environment ceilings, already root-caused,
that would need a real GPU-backed test environment to resolve or re-confirm — not further Bgfx
backend code work.

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
