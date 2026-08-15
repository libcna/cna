# XNA 4.0 API Coverage Audit

**Date:** 2026-06-26 (updated 2026-06-26 — Tasks 197–199; updated 2026-07-03 — Input/Touch
sections, `feature/input` Phases I1–I6; updated 2026-07-04 — final Input status, `feature/input`
Phase I9, `plan_input.md` tasks 700–840, coverage split by category; updated 2026-07-09 — Task 481,
Graphics sections rewritten after `plan_graphics.md` Phases 47–53: SDL_Renderer's own full 2D-only
audit phase (Phase 70, 15 real bugs found and fixed), EasyGL/Vulkan/Bgfx gap-closure phases
(Phases 71–73), the Model/OcclusionQuery correctness audits (Phases 49–50), a new golden-image
pixel-testing infrastructure, and a new FNA-vs-CNA JSON comparison harness — §7/§8 and the Stock
Effects/Recommended-next-steps sections below were stale by this entire stretch of work; see
`docs/graphics-renderer-feature-matrix.md` for the authoritative, currently-maintained per-renderer
detail this file now points to instead of duplicating; updated 2026-07-09 — Task 482 added the
"Coverage axes" section (Present/Implemented/Tested/FNA-compatible/Intentionally-unsupported as
independent questions); Task 483 added a dense per-class Graphics coverage table; Task 484 added
the complementary per-renderer Graphics support summary; Task 485 added a consolidated "Known
deviations from XNA/FNA" list, closing the Tasks 481-485 documentation arc; Task 490 added a
release checklist gating 90%/95%/100% compatibility milestone claims against concrete criteria,
closing Phase 54 in full, Tasks 481-490; updated 2026-07-17 — `plan_net.md` Task 9.1: GamerServices
and Net sections were stale since before decision 1a (Xbox-360-reference correctness bar) — both
namespaces are real, tested implementations now (Phases 1-8), not "not planned"/"intentionally
excluded"/"Guide-stub-only." Corrected §2/§3/§4/§5/§8/§10/§11 and added §9 (GamerServices/Net
per-feature support matrix, also closing a pre-existing §8→§10 numbering gap))  
**Reference:** FNA source at `/rv/data/library/github.com/FNA-XNA/FNA/src`  
**CNA headers:** `include/Microsoft/Xna/Framework/`

---

## 1. Overview

This document compares the CNA public C++ API surface against:

1. The FNA C# source tree, which is the primary faithful reimplementation of XNA 4.0.
2. The XNA 4.0 SDK documentation for areas that FNA omits (GamerServices, Avatar, etc.).

### Status Definitions

| Status | Meaning |
|--------|---------|
| **Implemented** | Header + working `.cpp` implementation; passes tests or is exercised in demos. |
| **Partial** | Header exists; some public methods are missing, or behavior is incomplete. |
| **Stub** | Header + declaration exist; behavior throws `std::runtime_error` / returns a no-op default. Marked with `// CNA_STUB:` in source. |
| **Missing** | No header in CNA at all. |
| **Intentionally excluded** | Deliberately not ported; reason documented below. |

### Coverage axes (Task 482, 2026-07-09)

The single "Status" column above (Implemented/Partial/Stub/Missing/Intentionally excluded) is a
useful at-a-glance tier, but it conflates several genuinely independent questions — a member can
be present in a header, do something real at runtime, and still never have been checked against
FNA's actual behavior. Percentages in §8 and elsewhere in this doc mix these together, which is
exactly the "ambiguous percentages" this task exists to prevent. When a coverage claim needs to be
precise (a new audit, a per-class table like §8, a bug report), qualify it against these 5
orthogonal axes instead of a single blended number:

| Axis | Question it answers | Example of a "no" |
|------|---------------------|---------------------|
| **Present** | Does a C++ declaration exist with the FNA/XNA 4.0 name (class/method/property/enum/constant)? | `ContentReader`/`ContentTypeReaderManager`/`LzxDecoder` — no header exists in CNA at all (excluded by CNA's non-XNB content-pipeline design, see §6). |
| **Implemented** | Does that declaration have a real `.cpp` body that does the actual thing, not a stub/no-op/throw? | `ResourceContentManager::OpenStream` — header and class exist, but the body unconditionally throws `std::runtime_error` (`// CNA_STUB:`, not implemented). `INTERNAL_applyReverb` is a second example — present, but a documented no-op (no aux-send bus in SDL3_mixer). |
| **Tested** | Does at least one automated test (`tests/`) or example (`examples/`) exercise this specific member, such that a regression would be caught? | Several `Microphone`/`BufferReady` real-hardware-capture-exceeds-threshold paths — implemented, but only the below-threshold case is tested (`NEXT.md`'s own documented gap). |
| **FNA-compatible** | Has the implemented behavior been directly checked against FNA's own source or real running output — not just "looks reasonable," but verified value-by-value or line-by-line? | `IndexElementSize`'s numeric values — implemented, tested (the test itself was what was wrong), but at the time NOT FNA-compatible: CNA used `16`/`32`, real FNA uses `0`/`1` (found by literally running FNA and diffing the output, Task 479; **fixed, Task 921, 2026-07-09** — CNA now uses `0`/`1` too). |
| **Intentionally unsupported** | Is a specific deviation from FNA behavior a deliberate, documented decision — not a "not yet done" gap? | Audio reverb, 3D HRTF/elevation — SDL3_mixer has no renderer primitive for either; documented in `CHECKLIST.md`, not tracked as an open bug. |

These axes are not mutually exclusive tiers on one scale. A member is typically
Present+Implemented+Tested well before anyone directly re-verifies it against FNA line-by-line —
that's normal, not a red flag, and most of this codebase sits there. The axis that actually
predicts hidden bugs is **FNA-compatible**: Tasks 471-479's entire FNA reference harness
(`docs/fna-reference-harness.md`) exists specifically to check that axis independently of the
other four, by running the real FNA implementation rather than re-reading its source a second
time — which is how the `IndexElementSize` divergence above was found despite the member being
present, implemented, and already tested for years.

### Per-class Graphics coverage (Task 483, 2026-07-09)

The tables above rate coverage per-namespace and per-subsystem; this table rates the ~26 major
`Microsoft::Xna::Framework::Graphics` classes individually against the 5 axes above. It is
deliberately dense, not exhaustive — for full per-renderer/per-feature detail behind any ⚠️/❌/⛔
below, see `docs/graphics-renderer-feature-matrix.md` (Task 451, the authoritative current source)
and `AUDIT.md`'s own per-class FNA-vs-CNA rows (`AUDIT.md` lines ~110–1341); this table does not
re-derive either, only summarizes and points to them. Legend matches the feature matrix: ✅ correct/
verified · ⚠️ partial/emulated/known gap · ❌ known gap, not fixed · ⛔ BLOCKED (needs a
project-owner decision) · N/A not applicable (e.g. a CNAEXT class has no "FNA-compatible" axis).
Every row below was checked against the real header/`.cpp` and its own `AUDIT.md` entry before
being rated — not filled in from memory (see Task 482's own post-merge correction above for why
that check matters).

| Class | Present | Implemented | Tested | FNA-compatible | Key gaps (task #) |
|---|---|---|---|---|---|
| `GraphicsDevice` | ✅ | ✅ (some overloads stub) | ✅ | ⚠️ | `ReferenceStencil` unconnected on EasyGL/Bgfx — **Vulkan fixed** (872, corrected 2026-07-09: an undocumented side effect of Task 870); `Clear` ignores `ClearOptions::Stencil` on all renderers (871) |
| `Texture2D` | ✅ | ⚠️ Partial (🔄 in AUDIT.md) | ✅ | ✅ core | Mip-level `SetData(level>0)` now real GPU upload on all 3 hardware renderers (867 split into 924 EasyGL/925 Vulkan/926 Bgfx, all closed 2026-07-09); still throws on SDL_Renderer (681, by design — 2D-only renderer); missing `SetDataPointerEXT`/`GetDataPointerEXT`/`TextureDataFromStreamEXT`; Color-only format |
| `Texture3D` | ✅ | ✅ (EasyGL/Vulkan/Bgfx) | ✅ | ✅ | ⛔ SDL_Renderer construction silently null-backed (725); not sampled in shaders on any renderer, architectural (863) |
| `TextureCube` | ✅ | ✅ (EasyGL/Vulkan/Bgfx) | ✅ | ✅ | Same ⛔ 725 as `Texture3D`; `DDSFromStreamEXT` real DXT1/3/5 decode (663) |
| `RenderTarget2D` | ✅ | ✅ | ✅ | ✅ | `DepthStencilFormat` fidelity real on EasyGL/Vulkan(911)/Bgfx; SDL_Renderer emulates (echoes format, no real backing store) |
| `RenderTargetCube` | ✅ | ✅ | ✅ | ✅ | Same shape as `RenderTarget2D` |
| `SpriteBatch` | ✅ | ✅ | ✅ | ⚠️ | `TextureAddressMode::Wrap`/`Mirror` ⛔ BLOCKED on SDL_Renderer (686/687); all other renderers correct |
| `SpriteFont` | ✅ | ✅ | ✅ | ✅ | `MeasureString(StringBuilder)` overload added (423); glyph placement/spacing/flip pixel-verified (424-429, 690-694) |
| `BasicEffect` | ✅ | ✅ | ✅ | ✅ | Core MVP/lighting/texture/specular pixel-verified on all 3 3D renderers, no open gaps |
| `AlphaTestEffect` | ✅ | ✅ | ✅ | ⚠️ | `VertexColorEnabled` missing on Vulkan/Bgfx (887) |
| `DualTextureEffect` | ✅ | ✅ | ✅ | ⚠️ | `VertexColorEnabled` missing on all 3 3D renderers (889) |
| `EnvironmentMapEffect` | ✅ | ✅ | ✅ | ⚠️ | `DirectionalLight1`/`2` (890) and base-lerp alpha scaling (891) missing on Vulkan/Bgfx |
| `SkinnedEffect` | ✅ | ✅ | ✅ | ⚠️ | `DirectionalLight1`/`2` (893), `SpecularColor`/`Power` (894), `WeightsPerVertex` GPU enforcement (895) missing on Vulkan/Bgfx |
| `ShaderEffect` (CNAEXT) | ✅ | ⚠️ | ✅ | N/A — not XNA API | Only EasyGL honors the documented "load from GLSL source" contract; Vulkan/Bgfx expect pre-compiled SPIR-V/binary despite the shared constructor signature |
| `BlendState` | ✅ | ✅ | ✅ | ⚠️ | Vulkan (868) and Bgfx (923) both closed 2026-07-09 with real per-`Blend`/`BlendFunction` mapping; Bgfx's alpha-factor-independence half is fixed in code (verified correct against bgfx's own decode logic) but could not be independently pixel-verified in this sandbox (a confirmed renderer/environment limitation, not a CNA defect); EasyGL/SDL_Renderer already correct |
| `DepthStencilState` | ✅ | ✅ | ✅ | ⚠️ | Compare-op + full stencil ops real on Vulkan (870, fixed); `ReferenceStencil` also fixed on Vulkan (872, EasyGL/Bgfx still open); `ClearOptions::Stencil` gap is tracked under `GraphicsDevice` above (871) |
| `RasterizerState` | ✅ | ✅ | ✅ | ⚠️ | One isolated Vulkan `DepthBias=-1e6` sub-case failure, unresolved; `FillMode::WireFrame` correctly feature-gated on Vulkan (454) |
| `SamplerState` | ✅ | ✅ | ✅ | ✅ | `TextureFilter::Anisotropic` now genuinely applied on EasyGL (918, closed — real `GL_EXT_texture_filter_anisotropic` call, clamped to the live driver cap); Vulkan/Bgfx already supported it |
| `VertexBuffer` | ✅ | ✅ | ✅ | ✅ | No open gaps |
| `IndexBuffer` | ✅ | ✅ | ✅ | ✅ | `IndexElementSize`'s underlying numeric values now match FNA exactly, `0`/`1` not `16`/`32` (921, closed 2026-07-09 — see Coverage axes example above, now historical) |
| `VertexDeclaration` | ✅ | ✅ | ✅ | ✅ | Construction/assignment confirmed never throws (729) |
| `Model`/`ModelMesh`/`ModelBone` | ✅ | ✅ runtime API | ✅ | ✅ | 4-arg constructor now has an optional `rootBoneIndex` param, additive-only, default `0` matching the prior behavior (916, closed 2026-07-09); content-pipeline loader (`ModelTypeReader`) has real gaps vs. FNA's `.xnb` (no bone hierarchy/`ParentBone`/`BoundingSphere`/`Tag`, zero loader test coverage, 440) |
| `OcclusionQuery` | ✅ | ✅ | ✅ | ⚠️ | EasyGL fully correct both directions (445/446); Vulkan fully correct both directions plus multi-draw-span, real per-draw-call correlation implemented (447/854, 2026-07-10); Bgfx fixed (448) but can't be pixel-verified in this sandbox's software GL driver, dedicated-view gap open (917) |
| `Viewport` | ✅ | ✅ | ✅ | ✅ | The one class with a direct running-FNA cross-check beyond source-reading (`Project`/`Unproject`, Tasks 476/479) — all cases matched exactly |
| `PresentationParameters` | ✅ | ✅ | ✅ | ✅ | No open gaps |
| `GraphicsAdapter` | ✅ | ✅ | ✅ | ✅ | No open gaps |

### Per-renderer Graphics support (Task 484, 2026-07-09)

The table above rates coverage per-class, across renderers; this one flips the axis — one row per
renderer, summarizing overall Graphics maturity at a glance. For full per-feature detail behind any
row below, see `docs/graphics-renderer-feature-matrix.md` (Task 451, the authoritative source this
table summarizes, not duplicates) and its own per-renderer known-failure-count section; this table
adds nothing that source doesn't already contain in more detail.

| Renderer | Overall maturity | Fully correct | Partial / emulated | Architecturally blocked (⛔) | Known pre-existing test-failure baseline |
|---|---|---|---|---|---|
| **EasyGL** | Most mature — the primary renderer; nearly everything below is ✅ | All 5 stock effects (core+lighting+specular+fog), `SpriteBatch`/`SpriteFont`, all 4 state classes, `RenderTarget2D`/`Cube` (MSAA/mip/depth-format), `Texture2D/3D/Cube` `SetData`/`GetData` (incl. real mip-level upload, Task 924), `OcclusionQuery` (both directions verified), `VertexBuffer`/`IndexBuffer`/`VertexDeclaration`, real `TextureFilter::Anisotropic` (918) | `Texture3D`/`TextureCube` don't inherit `Texture` so can't be sampled in shaders (863, architectural, **NEEDS_HUMAN**) | Non-`Color` `SurfaceFormat` GPU forwarding (732 — conflicts with the already-shipped `Texture::ValidateFormat` contract, Task 176) | 3 — `EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`, `easy-gl-resource-smoke-tests` (Task 449's regression, 4510/4513); reconfirmed 4535/4539 as of Task 924, 2026-07-09 |
| **Vulkan** | Second-most mature — most rendering correct; `BlendState` closed 2026-07-09 (868), `ReferenceStencil` also confirmed already-fixed (872, corrected 2026-07-09), only one isolated `DepthBias` sub-case remains as a real state-class gap | All 5 stock effects (core+lighting+specular+fog on every Vulkan 3D pipeline, including `colored3d`/`textured3d`/`colored_textured3d`/`dual_texture3d`/`skinned3d`/`env_map3d` — Task 899 closed this fully on 2026-07-07, predating this table; **correction (2026-07-09, caught while writing Task 488):** this row previously and incorrectly claimed these pipelines "still lack fog," which was already stale when this table was first written), `RenderTarget2D`/`Cube` (MSAA/mip/per-instance `DepthStencilFormat` fidelity via Task 911's format-keyed pipeline cache), `Texture2D/3D/Cube` `SetData`/`GetData` (incl. Task 865's real GPU readback, and real mip-level upload as of Task 925), `OcclusionQuery` — real per-draw-call query correlation implemented, both directions plus multi-draw-span pixel-verified (Task 447/854, 2026-07-10), `SamplerState`, `VertexBuffer`/`IndexBuffer`/`VertexDeclaration`, **`BlendState`** — real per-`Blend`/`BlendFunction` mapping across all 9 3D pipeline-creation sites (Task 868, closed 2026-07-09) | `DepthStencilState` compare-op + stencil ops now real (Task 870), `ReferenceStencil` also confirmed already-connected via `vkCmdSetStencilReference` (872, corrected 2026-07-09 — was undocumented since Task 870); one isolated `RasterizerState.DepthBias=-1e6` sub-case unresolved | none remaining (`OcclusionQuery`'s own former BLOCKED status, 447, was resolved 2026-07-10 — see the ✅ column) | **Updated 2026-07-11, per `NEXT.md`:** 1 — `Vulkan_DepthBias` only. The 3 `ContentManagerSkinnedModelTest.*` segfaults previously counted here (Xvfb/llvmpipe environment issue) were fixed by Task 953 (closed 2026-07-11), no exclusions needed anymore. Current confirmed run: `CnaTests` 4371/4373 (2 hardware skips), `ctest` 126/127. **Historical note:** prior to Task 868's fix this baseline was 9 (5× `BlendState` failures included, fixed 2026-07-09). |
| **Bgfx** | Third — broad 2D+3D functionality with extensive pixel verification as of Phase 72, but not full parity: occlusion-query correctness can't be confirmed in this sandbox, and 2 named RenderTarget failures plus a `RenderTargetCube` depth-output bug (Task 952, deferred) remain open — see the last column | All 5 stock effects (core+lighting+specular+fog), `RenderTarget2D`/`Cube` (MSAA/mip/depth), `Texture2D/3D/Cube` `SetData`/`GetData` (incl. Task 914's blit-based readback), all 4 state classes, `SpriteBatch` `SamplerState` (Task 750) | `OcclusionQuery` Begin/End wiring is real (Task 448) but correctness (visible vs. occluded pixel counts) can't be pixel-verified under this sandbox's software GL 2.1 driver — dedicated-view architecture gap open (917); `ShaderEffect` custom-source loading unsupported (`CreateEffectRenderer` returns `nullptr`) | None | **Updated 2026-07-11, per `NEXT.md`:** 2 remaining — `Bgfx_RenderTarget2D_MsaaResolve` (this sandbox's Xvfb has no DRI3 support; a documented environment limitation, not a code bug) and `Bgfx_RenderTargetCube_DepthFormat` (Task 952, **DEFERRED** — a `Depth24Stencil8`-attached `RenderTargetCube` face produces no colour output, root cause still not found after 3 investigation rounds). Task 951 (closed 2026-07-11) fixed 5 other pre-existing `RenderTarget2D`/`RenderTargetCube` `glReadPixels` crashes (including the `MipChain` flake previously tracked here) via a dedicated always-last-processed bgfx "flush" view. Current confirmed run: `CnaTests` 4375/4377 (2 hardware skips), `ctest` 103/105. |
| **SDL_Renderer** | Deliberately 2D-only by design — not a maturity gap, an architectural scope boundary; its own 2D path is comprehensively audited and pixel-verified (`docs/sdl-renderer-2d-completeness.md`, Phase 70, 15 real bugs found and fixed) | All `SpriteBatch`/`SpriteFont` draw paths, all `BlendState`/`SamplerState` behavior, `RenderTarget2D` basic round-trip, `GetBackBufferData` (Task 915) | `RenderTarget2D`'s `DepthStencilFormat` is emulated — echoes the requested format back with no real backing store; `ClearOptions::Stencil` is emulated too | `TextureAddressMode::Wrap`/`Mirror` via `SpriteBatch` (686/687 — no native support in the `Draw()` path used, 3 unpicked design options); `Texture3D`/`TextureCube` construction succeeds silently with a null renderer, a 94-existing-test blast radius if changed (725) | 11 — all throwing/exercising `"SDL_Renderer does not support 3D"` (`EffectApplyTest`×2, `SkinnedModelEXTPartTest.*`×6, `ContentManagerSkinnedModelTest.*`×3), matching this renderer's accepted 2D-only scope exactly. Corrected from an original 13 by Task 709's own fix; reconfirmed via Task 915/456. |

### Known deviations from XNA/FNA (Task 485, 2026-07-09)

Closes out the Tasks 481-485 documentation arc. Present/Implemented/Tested coverage (above) answers
"does it exist and get exercised" — this list answers a narrower, more dangerous question: **for
API surface that IS present, implemented, and tested, where does CNA's actual runtime behavior
diverge from real XNA/FNA's**, whether that's a confirmed bug awaiting a fix or a permanent,
deliberate design decision. Every entry below was re-checked against its cited task row /
`CHECKLIST.md` / `AUDIT.md` entry before being listed here, not transcribed from memory. Scoped to
`Microsoft::Xna::Framework::Graphics`, matching Task 481's own Graphics-only scoping for this file.

#### Confirmed bugs — all 4 now fixed (2026-07-09)

**Update:** every row this section originally listed is now closed — the table is kept for
historical reference (what was found and how), not as a current "still open" list.

| Deviation | CNA behavior (as found) | Real FNA behavior | Tracked as |
|---|---|---|---|
| `IndexElementSize` numeric values | `SixteenBits=16`, `ThirtyTwoBits=32` (apparently assumed the enum encodes a literal bit-width) | Implicit, sequential `SixteenBits=0`, `ThirtyTwoBits=1` | Task 921 — **CLOSED**, this Tasks 479-485 arc's own headline finding, from running real `FNA.dll` and diffing its output |
| Vulkan `BlendState` | Hardcoded one blend equation (`NonPremultiplied`'s) for anything other than `Opaque`, ignoring the actual requested `Blend`/`BlendFunction` values entirely | Applies the exact requested blend factors/functions per `BlendState` | Task 868 — **CLOSED**, confirmed 5× via pixel tests on real hardware, now fixed with real per-pipeline blend-state mapping |
| EasyGL `TextureFilter::Anisotropic` | Silently fell back to plain trilinear filtering — no `GL_EXT_texture_filter_anisotropic` call anywhere in the renderer | Applies real anisotropic filtering (Vulkan/Bgfx both do too) | Task 918 — **CLOSED**, real GL call now wired, clamped to the live driver cap |
| `Model`'s non-default constructor | Auto-defaulted `Root` to `bones[0]`, with no parameter to specify a different root bone index | Never sets `Root` in the constructor at all — `ModelReader` assigns it externally from an explicit `rootBoneIndex` naming any bone | Task 916 — **CLOSED**, additive optional `rootBoneIndex` param added, default `0` matches prior behavior |

#### Intentional, permanent deviations

These will not change — they are documented design decisions, not gaps awaiting a task number.
Full detail and rationale for each lives in `CHECKLIST.md`'s "Known acceptable C++ deviations from
FNA/XNA" table or the cited `AUDIT.md` section; not re-derived here.

| Deviation | Why |
|---|---|
| `GetHashCode()` returns `std::size_t`, not C#'s `int` | C++ has no fixed 32-bit hash convention; platform-native size is used project-wide (`CHECKLIST.md`) |
| `ref`/`out` parameters become value-reference pairs (e.g. `TryGetValue`-style overloads) | No C# `ref`/`out` mechanism exists in C++ (`CHECKLIST.md`) |
| `IEnumerable<T>` replaced by `begin()`/`end()` (CNAEXT), e.g. on `ModelBoneCollection` | C++ iterator idiom, not a C# `IEnumerable` port (`CHECKLIST.md`) |
| `Model::Draw()` defaults a mesh's parent-bone index to `0` when `ModelMesh::ParentBone` is `nullptr`, instead of FNA's `NullReferenceException` | A null dereference is undefined behavior in C++, not a catchable exception like C#'s; defaulting to bone 0 is a strictly safer failure mode (`CHECKLIST.md`, Task 431) |
| `ModelMesh::Draw()` silently skips a mesh part whose `Effect` is `nullptr`, instead of FNA's `NullReferenceException` on `effect.CurrentTechnique` | Same rationale as the `Model::Draw()` row above — safer failure mode than a null-pointer dereference (`CHECKLIST.md`, Task 431, corrects an earlier inaccurate claim in Task 728) |
| `Model::CopyBoneTransformsFrom`/`CopyBoneTransformsTo` loop over `Bones.Count`, not the caller-supplied array's length like FNA does | FNA's own loop bound (`sourceBoneTransforms.Length`) makes it throw partway through for an array larger than `Bones.Count`; CNA's bound is a deliberately safer, non-fragile alternative (`CHECKLIST.md`, Task 436) |
| `Texture2D::FromStream` silently auto-detects and decodes DDS/DXT1/3/5 content internally (`TryDecodeDds`), with no separate `DDSFromStreamEXT` method matching FNA's API shape | Real FNA's `FromStream` never auto-detects DDS — it always goes through the ordinary image decoder and throws on unsupported formats. CNA's auto-detection is a usability improvement, but is a genuine behavioral divergence from FNA's stricter contract (`AUDIT.md`, Texture2D detailed audit) |
| `Texture2D::SetData(const Color*, int elementCount)` silently returns (no exception) for `data == nullptr` or `elementCount <= 0`, while the 5-arg `SetData(level, ...)` overload throws `std::invalid_argument` for the same inputs | Real FNA's `SetData<T>(T[] data)` always throws (it delegates to the validated 5-arg overload) — CNA's inconsistency between its own two overloads is already encoded as expected/tested behavior, not corrected (`AUDIT.md`, Texture2D detailed audit) |
| `Texture::GetFormatSizeEXT`/`ValidateGetDataFormat` are `public static` on `Texture`, not FNA's `internal` | CNA's class hierarchy has no reachable `protected`/friend equivalent across all 3 real call sites (`Texture3D`, `TextureCube`, `GraphicsDevice::GetBackBufferData`), since `Texture3D`/`TextureCube` don't inherit `Texture` in CNA (`AUDIT.md`, Texture3D detailed audit, Task 271-280) |

### Release checklist for Graphics compatibility milestones (Task 490, 2026-07-09)

This project has historically used informal per-subsystem percentage estimates (see the old
Phase 9–14 coverage table earlier in `plan_graphics.md`) to describe Graphics maturity. Those
estimates are useful color commentary but are not a checklist — nothing gates a "~95%" claim
against a concrete, falsifiable condition. This section replaces ad-hoc percentages with pass/fail
criteria, built directly on the axes/tables Tasks 482–485 already established. **Don't estimate a
new percentage from this checklist — use it to gate whether a round-number milestone claim
(90%/95%/100%) is honest, using the counts already tracked in this file and `plan_graphics.md`.**

**Before claiming any milestone at all**, the counts below must be pulled fresh from the real
current state of Task 483's per-class table, Task 484's per-renderer table, and Task 485's known-
deviations list plus any newer findings (e.g. Task 922, found by Task 487 after Task 485's table
was written and not yet folded into it) — do not reuse cached numbers from an old audit.

#### Before claiming "Graphics is ~90% XNA/FNA-compatible"

- [ ] Every class in Task 483's per-class table is rated Present ✅ and Implemented ✅ on **at least
      one** renderer (no class is entirely ❌/missing).
- [ ] Every class is rated Tested ✅ on at least one renderer (no class has zero automated coverage).
- [ ] At least one renderer (currently EasyGL) has zero ❌-rated classes in Task 483's table.
- [ ] The BLOCKED-task count (Task 484's per-renderer table; currently 4: 686, 687, 725, 732 — 447
      was resolved 2026-07-10) is stable and each one has a written, current rationale — not
      silently stale.
- [ ] `docs/migration-guide.md`'s "what doesn't work at all yet" list matches the current BLOCKED
      list exactly (no drift between the two documents).

#### Before claiming "Graphics is ~95% XNA/FNA-compatible"

- [ ] All "before 90%" items above still hold.
- [ ] Every stock Effect (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/
      `SkinnedEffect`) has its core rendering (MVP, lighting, texture, fog) pixel-verified on
      **all three** 3D renderers (EasyGL, Vulkan, Bgfx), not just EasyGL.
  - Currently true per Task 483/484's tables; the residual gaps are named per-effect secondary
    features (887, 889, 890, 891, 893–895), not core rendering.
- [ ] The "Confirmed bugs, not yet fixed" list (Task 485) has a hard ceiling — **update (2026-07-09):
      the original 5 items (921 `IndexElementSize`, 868 Vulkan `BlendState`, 918 EasyGL
      `Anisotropic`, 916 `Model` `Root` default, 922 `SpriteBatch::Draw`'s 7th-overload signature
      gap) are all now CLOSED — this gate is currently satisfied at 0 items.** This is a factual
      status update only, not a milestone declaration — declaring "~95%" itself remains the project
      owner's own call (Task 500's own precedent: declaring a milestone is a decision, not routine
      backlog progress), and the other "before 95%" checkboxes below have not all been re-verified
      since this file was written. Whoever next reviews this checklist should re-pull fresh counts
      for every item, not just this one, before considering the claim.
- [ ] `IndexElementSize`'s FNA-compatible axis specifically (Task 921) does not silently block a 95%
      claim on its own — it's a narrow, symbolically-isolated enum-value bug, not a systemic one —
      but it must remain tracked, not quietly dropped from this list once claimed.

#### Before claiming "Graphics is 100% XNA/FNA-compatible"

- [ ] Zero entries remain in the "Confirmed bugs, not yet fixed" list (Task 485 plus any newer
      findings like 922) — all fixed, tested, and moved to "closed" in `plan_graphics.md`.
- [ ] Zero BLOCKED tasks remain (currently 5) — each has either shipped a real implementation or an
      explicit, project-owner-approved permanent exclusion moved into the "Intentional, permanent
      deviations" list instead of staying BLOCKED indefinitely.
- [ ] Every class in Task 483's table is ✅ across Present/Implemented/Tested **on every renderer**
      that supports that class's feature domain at all (SDL_Renderer is exempt for 3D-only classes,
      per its own documented 2D-only architectural scope).
- [ ] The FNA-compatible axis (Task 482) has been **independently verified**, not assumed, for every
      class that has a running-FNA reference available — via the Tasks 471–479 FNA-vs-CNA JSON
      comparison harness (`tools/fna-reference/`, `tools/cna-reference/`,
      `scripts/compare-fna-reference.py`) or an equivalent direct comparison — for classes outside
      that harness's current 4 categories (enums/state-presets/PackedVector/Viewport), an
      equally-direct FNA source line-by-line comparison is the floor, not "looks reasonable."
- [ ] Every "Intentional, permanent deviation" (Task 485's second table) has been re-confirmed as
      still permanent, not just carried forward from an old audit — a deviation accepted 6 months
      ago may no longer be the right call once a renderer's real capabilities change.
- [ ] Full regression suite passes on all 4 renderers (not run concurrently, per this project's own
      established convention) with **zero** known-pre-existing-failure carve-outs left unexplained
      by an environment-limitation note (e.g. `docs/xna-4-api-coverage.md`'s per-renderer table's own
      "known pre-existing test-failure baseline" column must read 0 for a genuine 100% claim, not
      just "documented and accepted").

**Current status against this checklist (2026-07-09, informational only — not itself a milestone
claim):** the ~90% bar looks achievable soon (all classes already rate ✅/⚠️ rather than ❌ per Task
483); the ~95% bar is blocked on the 5-item confirmed-bugs list above; 100% additionally requires
closing all 5 currently-BLOCKED tasks, which each need a project-owner architecture decision first
(see `docs/graphics-renderer-feature-matrix.md`'s BLOCKED-task table) — none are silent gaps, but
none are trivial engineering either.

### What counts as "public XNA 4.0 API"

- `public` and `protected` members of non-internal classes/structs/enums in the `Microsoft.Xna.*` namespace.
- FNA-internal helper classes, platform renderers, `internal` C# classes, and Xbox-Live–specific runtime classes that were never meaningful on PC are **excluded** from this audit.

---

## 2. Namespace Coverage

| Namespace | FNA has it | CNA has it | Status |
|-----------|-----------|------------|--------|
| `Microsoft::Xna::Framework` (root) | ✅ | ✅ | Implemented |
| `Microsoft::Xna::Framework::Audio` | ✅ | ✅ | Implemented (see §4 for the small remaining accepted-deviation list) |
| `Microsoft::Xna::Framework::Content` | ✅ | ✅ | Partial (see §3) |
| `Microsoft::Xna::Framework::Design` | ✅ | ❌ | Intentionally excluded (see §6) |
| `Microsoft::Xna::Framework::GamerServices` | ❌ (not in FNA) | ✅ | Implemented — real Achievements, Avatar (full real-rendering extension, see `docs/avatar-real-rendering-ext.md`), Friends, Presence, Leaderboards, Privileges, Profile, SignedInGamer, Guide (see §3/§4) |
| `Microsoft::Xna::Framework::Graphics` | ✅ | ✅ | Implemented / Stub |
| `Microsoft::Xna::Framework::Graphics::PackedVector` | ✅ | ✅ | Implemented |
| `Microsoft::Xna::Framework::Input` | ✅ | ✅ | Implemented |
| `Microsoft::Xna::Framework::Input::Touch` | ✅ | ✅ | Implemented (see §4) |
| `Microsoft::Xna::Framework::Media` | ✅ | ✅ | Implemented / Stub |
| `Microsoft::Xna::Framework::Media::Video` | ✅ | ✅ | Implemented |
| `Microsoft::Xna::Framework::Storage` | ✅ | ✅ | Implemented |
| `Microsoft::Devices::Sensors` | ❌ (not in FNA) | ✅ | Stub – Windows Phone extension |

---

## 3. FNA Public API Found but Missing in CNA

### `Microsoft::Xna::Framework::Content`

| Missing class | Notes |
|---------------|-------|
| `ResourceContentManager` | Loads content from .NET assembly resources. CNA equivalent would load from embedded binary. **Should be stubbed.** |
| `ContentReader` | XNB binary stream reader used by stock ContentTypeReaders. CNA uses a file-extension reader approach, not XNB. **Intentionally deferred** – see §6. |
| `ContentTypeReaderManager` | Internal registry used by ContentReader. Not needed without XNB support. **Intentionally deferred.** |
| `ContentExtensions` | Adds `GetLoadedAssets()` extension to ContentManager. FNA-internal convenience; not core XNA 4.0 public API. **Intentionally excluded.** |
| `ContentSerializerAttribute` and siblings | Design-time attributes for the Content Pipeline build tool. Not a runtime concern. **Intentionally excluded** – see §6. |
| `LzxDecoder` | XNB LZX decompressor. FNA internal. **Deferred, low priority** – see `xnb.md`. |

### `Microsoft::Xna::Framework::GamerServices`

**Updated (`feature/net`, Phases 1-8 of `plan_net.md`, decision 1a):** stale as of this table's
original writing. FNA does not implement GamerServices (it targets PC XNA only, where
GamerServices is Xbox-Live-only), but CNA deliberately went beyond FNA's scope here — decision 1a
made the real Xbox 360 XNA 4.0 reference behavior (not Windows' no-op stubs) the correctness bar,
since CNA already has its own real avatar/networking implementations to hold to that standard.
Every class below now exists with a real `.cpp` implementation, not a stub — see §4 for the
per-class implementation notes and `plan_net.md`/`AUDIT.md` for the line-by-line FNA-fidelity
audits behind them:

| Class | Notes |
|---------------|-------|
| `GamerServicesComponent` | `GameComponent` subclass; implemented. |
| `GamerServicesNotAvailableException` | Implemented. |
| `Gamer` | Abstract base for `SignedInGamer` and `NetworkGamer`; implemented. |
| `SignedInGamer` | Represents a locally signed-in player profile; implemented, including real local persistence for achievements/leaderboards (Phase 4). |
| `GamerCollection<T>` | Header-only generic collection template (no `.cpp` needed); implemented. |
| `SignedInGamerCollection` | Indexed collection of locally signed-in gamers; implemented. |
| `GamerPresence` | Current presence information; implemented. |
| `GamerPresenceMode` | Enum of presence modes; implemented. |
| `GamerPrivilegeSetting` / `GamerPrivileges` / `GamerPrivilegeException` | Privilege flags/exception; implemented. |
| `GameDefaults` | Static helper for reading default player settings; implemented. |
| `FriendCollection` | Collection of friend gamers; implemented, but `SignedInGamer::GetFriends()` always returns it empty - see §9. |
| `FriendGamer` | Represents a friend in the gamer's friend list; implemented, same population gap as `FriendCollection` above. |

Also present and implemented, beyond this table's original scope: `Achievement`/`AchievementCollection`,
the full `Avatar*` real-rendering extension (`AvatarRenderer`, `AvatarAppearanceEXT`, `AvatarBone`,
`AvatarAnimation*`, wardrobe attach/detach — see `docs/avatar-real-rendering-ext.md`), `GamerProfile`,
`GamerZone`, `LeaderboardReader`/`LeaderboardWriter`/`LeaderboardEntry`/`LeaderboardIdentity`/
`LeaderboardKey`/`LeaderboardOutcome` (local-fake-persisted, Phase 4), `PropertyDictionary`,
`GamerServicesDispatcher`, and the full `*EventArgs`/exception family
(`SignedInEventArgs`/`SignedOutEventArgs`/`InviteAcceptedEventArgs`/`GuideAlreadyVisibleException`/
`GameUpdateRequiredException`/`NetworkException`/`NetworkNotAvailableException`).

---

## 4. CNA API Present but Incomplete

### `Microsoft::Xna::Framework::Content::ContentManager`

- **Missing:** `ServiceProvider` property (`IServiceProvider*`). XNA 4.0 ContentManager takes a service provider in its constructor and exposes it. CNA uses `setGraphicsDevice()` instead. Any XNA 4.0 game that passes a service provider to ContentManager will not compile without this.
- **Missing:** Protected `OpenStream(string)` virtual — needed if subclasses want to intercept asset loading.
- **Missing:** Protected `ReadAsset<T>` — needed if subclasses customise loading.
- **Status:** Partial

### `Microsoft::Xna::Framework::Graphics::GraphicsDevice`

- All public methods are declared. Some behaviors are renderer-dependent and may be no-ops.
- **Status:** Partial (functionally working for 2D and 3D; some query methods return stubs)

### `Microsoft::Xna::Framework::Graphics` — Stock Effects

- `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`: API surface complete on all 4 renderers. **Stale claim corrected 2026-07-09 (Task 481):** Bgfx is no longer blocked on depth/blend state — Phase 72 ("Bgfx: full 2D+3D pixel-verified parity") closed that gap; all 5 stock effects' core rendering (MVP, lighting, texture, fog) now works and is pixel-verified on EasyGL, Vulkan, **and** Bgfx. Remaining per-effect gaps are narrow and tracked individually (e.g. `AlphaTestEffect.VertexColorEnabled` on Vulkan/Bgfx, Task 887; `EnvironmentMapEffect`'s `DirectionalLight1`/`2`, Task 890) — see `docs/graphics-renderer-feature-matrix.md`'s "Stock Effects" table for the full, currently-accurate per-feature/per-renderer breakdown. `ShaderEffect` (custom GLSL/SPIR-V) works on EasyGL/Vulkan; Bgfx's `CreateEffectRenderer` still returns `nullptr` for it.
- **Status:** Implemented (EasyGL, Vulkan, Bgfx — core rendering); a handful of named secondary-light/vertex-color extras remain per-renderer gaps, see the feature matrix.

### `Microsoft::Xna::Framework::Graphics::PackedVector::*`

All 17 XNA 4.0 PackedVector types are fully implemented:

| Type | Packed width | Status |
|------|-------------|--------|
| `Alpha8` | 8-bit | ✅ Implemented |
| `Bgr565` | 16-bit | ✅ Implemented |
| `Bgra4444` | 16-bit | ✅ Implemented |
| `Bgra5551` | 16-bit | ✅ Implemented |
| `Byte4` | 32-bit | ✅ Implemented |
| `HalfSingle` | 16-bit half-float | ✅ Implemented |
| `HalfVector2` | 32-bit (2× half) | ✅ Implemented |
| `HalfVector4` | 64-bit (4× half) | ✅ Implemented |
| `NormalizedByte2` | 16-bit (2× snorm8) | ✅ Implemented |
| `NormalizedByte4` | 32-bit (4× snorm8) | ✅ Implemented |
| `NormalizedShort2` | 32-bit (2× snorm16) | ✅ Implemented |
| `NormalizedShort4` | 64-bit (4× snorm16) | ✅ Implemented |
| `Rg32` | 32-bit (2× unorm16) | ✅ Implemented |
| `Rgba1010102` | 32-bit (10-10-10-2) | ✅ Implemented |
| `Rgba64` | 64-bit (4× unorm16) | ✅ Implemented |
| `Short2` | 32-bit (2× int16) | ✅ Implemented |
| `Short4` | 64-bit (4× int16) | ✅ Implemented |

`HalfTypeHelper` implements the full FNA IEEE 754 half-float algorithm including subnormals, ±∞, ±0, and NaN (Tasks 197–199). Pack rounding uses `std::lroundf` matching FNA's `Math.Round` for signed-normalized types (Tasks 198). Golden-value tests and edge-case tests (clamping, specials) all pass.

- **Status:** Implemented

### `Microsoft::Xna::Framework::Audio`

> Updated 2026-07-04 (Fáze 9 `P9-DOCS-002`) — the entries below describing XACT/`Microphone`/
> `DynamicSoundEffectInstance` as unimplemented stubs were stale by a full branch's worth of work
> (`plan_audio.md` Fáze 0–9). See `AUDIT.md`'s Audio table for the per-class summary and
> `plan_audio.md` for the complete file-by-file history.

- `AudioEngine`, `SoundBank`, `WaveBank`, `Cue`: **Implemented.** Real hand-written `.xgs`/`.xsb`/
  `.xwb` (XACT) parser (`CNA::Internal::Audio::XactParser`), mixed through SDL3_mixer. Cue playback,
  variation selection (weighted lottery, matching FAudio), category volume/pause/resume/stop, 3D
  pan/attenuation, natural-completion state reconciliation, and fire-and-forget lifecycle are all
  real, including real Doppler pitch shift (`P9-3D-004/005`). Not FAudio/FACT — SDL3_mixer is the
  renderer, so 3D HRTF/elevation and streaming-wavebank offset/packetSize params are documented
  accepted deviations, not stubs (`CHECKLIST.md`).
- `Microphone`: **Implemented.** Real SDL3 capture device enumeration, `Start()`/`Stop()`,
  `GetData()`/`GetQueuedBytes()`, `BufferReady` event.
- `DynamicSoundEffectInstance`: **Implemented.** Real buffer queue via `SDL_AudioStream`,
  `BufferNeeded` event pumped by `FrameworkDispatcher::Update()`, `PendingBufferCount` tracks real
  stream consumption.
- `SoundEffect`, `SoundEffectInstance`: **Implemented** (SDL3_mixer backend). Move-only with
  instance-tracking `Dispose()` cascade; real low/high/band-pass filters; real Doppler pitch
  shift via `Apply3D` (`P9-3D-004/005`); reverb and full 3D HRTF/elevation remain documented
  no-ops (SDL3_mixer has no aux-send bus or 3D audio graph).
- **Status:** Implemented, with a small set of accepted deviations documented in `CHECKLIST.md`
  (no reverb/HRTF/elevation, `QUEUE`/`REPLACE_OLDEST`/
  `REPLACE_QUIETEST` instance-limit behaviors all collapsing to "evict oldest," a cue-level
  instanceLimit eviction's victim search having no category/same-cue-definition filter at all) —
  none of these are "unimplemented," they are deliberate, reasoned SDL3_mixer-renderer trade-offs
  (or, for the `maxInstanceBehavior` collapse and the unfiltered victim search, matching FAudio's
  own acknowledged/shipped behavior exactly, quirks included). `Cue::IsPlaying`/`IsPaused` mutual
  exclusivity was one such deviation but is now fixed (`P9-LIFECYCLE-013`, resolved) — pausing no
  longer clears `IsPlaying`, matching real FACT's independent bits. XACT category- **and**
  cue-level `instanceLimit`/`maxInstanceBehavior`/fade in/out are now both real and enforced
  (`P9-CATEGORY-005..011`, resolved).

#### Audio compatibility table (`P9-DOCS-005`)

A concise summary of every known-and-documented gap versus real XNA 4.0/FNA. Full rationale for
each row is in `CHECKLIST.md`'s "Known acceptable C++ deviations" table (search for `Audio:`);
this table exists to answer "is X implemented?" at a glance without reading every row there.

| Bucket | Behavior |
|---|---|
| **Implemented** (matches FNA/XNA 4.0) | `SoundEffect` construction (file/buffer/buffer+range+loop), `Play`/`CreateInstance`, move-only Dispose cascade to every live instance • `SoundEffectInstance` Play/Pause/Resume/Stop/Volume/Pan/Pitch/IsLooped, real low/high/band-pass filters, `Resume()`-plays-if-never-started quirk, real Doppler pitch shift via `Apply3D` (exact `F3DAudio.c` `CalculateDoppler` formula, `P9-3D-004/005`), real 4-coefficient stereo crossfeed pan blending matching FNA's `SetPanMatrixCoefficients` exactly instead of hard-silencing the opposite channel (`P11-PAN-001`, RFC-1) • `DynamicSoundEffectInstance` SubmitBuffer/SubmitFloatBufferEXT, `BufferNeeded`, real `PendingBufferCount` • `AudioEngine` `.xgs` parsing, categories, global variables, `Update()` lifecycle sweep, Dispose cascade, real `System::IO::FileNotFoundException` on a missing settings file and `System::InvalidOperationException` on existing-but-corrupt settings (`P9-HARDWARE-003`, matching FNA's `TitleContainer.ReadToPointer`/checked `FACTAudioEngine_Initialize` exactly) • `SoundBank`/non-streaming `WaveBank` real `System::IO::FileNotFoundException` on a missing file (same fix) • `SoundBank`/`WaveBank` `.xsb`/`.xwb` parsing (compact + non-compact + ADPCM), `PlayCue`, `GetCue`, real lazy streaming reads • `Cue` playback, natural-completion state reconciliation, weighted-lottery variation selection for non-interactive tables AND variable-range-driven selection for interactive (`type==3`) tables (`P9-XACT-002/003/004`, not a uniform fallback), category routing, `IsPlaying`/`IsPaused` as independent (non-mutually-exclusive) flags matching real FACT (`P9-LIFECYCLE-013`, resolved), real authored `fadeOutMS` timing for `Stop(AsAuthored)` -- a real linear volume ramp over the exact authored duration, hard-stopping immediately when no fade is authored at all (every simple cue), matching FACT's own `FACTCue_Stop` condition exactly (`P9-STOP-010`, resolved) • real XACT category- and cue-level `instanceLimit`/`maxInstanceBehavior` enforcement (`FAIL` rejects, `REPLACE_LOWEST_PRIORITY` evicts by priority, `QUEUE`/`REPLACE_OLDEST`/`REPLACE_QUIETEST` evict oldest; cue-level checked before category-level, matching FACT's own `play_sound()` order) plus real fade-in/fade-out within that same instance-limit-replacement path, at both levels (`P9-CATEGORY-005..011`, resolved) • `AudioCategory` Pause/Resume/Stop/SetVolume against real active cues over a mutation-safe snapshot • real continuous XACT RPC volume/pitch re-evaluation every `AudioEngine::Update()` tick, not just once at `Cue::Play()` time (`P9-XACT-016`), extended to the built-in `"AttackTime"`/`"ReleaseTime"` envelope variables (real elapsed-play/release-time tracking, `P10-RPC-002/003/004`) and to RPC-driven live filter frequency/Q targeting (`P10-FILTER-002/003/004/006`) • a bounded loop region (`loopStart`/`loopLength`) plays its pre-loop intro exactly once, then repeats only `[loopStart, loopStart+loopLength)` — confirmed against real decoded audio, not just the SDL3_mixer property docs (`P10-LOOP-003/004`) • `Microphone` real SDL3 capture • `AudioListener`/`AudioEmitter`/`RendererDetail`/all enums |
| **Approximate** (real effect, not bit-exact vs FNA/XAudio2/FACT) | `Apply3D` pan now projects onto the listener's own right axis (`Forward`/`Up`-aware, `P9-3D-010`) instead of raw world-space X, but is still a linear approximation, not real X3DAudio's multi-speaker energy-diffusion azimuth pipeline (Doppler and distance attenuation are exact closed-form formulas, see Implemented — pan is the only remaining approximate positional effect; stereo hard-pan itself no longer eliminates the opposite channel, `P11-PAN-001`, see Implemented) • category/cue `maxInstanceBehavior`'s `QUEUE`/`REPLACE_OLDEST`/`REPLACE_QUIETEST` values all collapse to "evict oldest active cue," matching FAudio's own acknowledged collapse of those three (`P9-CATEGORY-005/010/011`) • a cue-level instanceLimit eviction's victim search has no category or same-cue-definition filter at all, matching FAudio's own `handle_instance_limit(cue, NULL)` exactly — it can evict an unrelated cue instead of a sibling instance of the same named cue (`P9-CATEGORY-011`) |
| **Intentionally unsupported** (documented, no plan to implement) | reverb (`INTERNAL_applyReverb` is a no-op — no aux-send/return bus in SDL3_mixer) • true 3D elevation/HRTF (no native 3D audio graph) • `NoAudioHardwareException` never thrown from `AudioEngine`'s own constructor (CNA always reports exactly one renderer, so FNA's "zero renderers" check can never fail here; it *is* thrown from `SoundEffect`/`DynamicSoundEffectInstance` when the mixer device itself fails to open, `P9-HARDWARE-002`) • `SoundBank`/`WaveBank` silently stay in a stub/unprepared state on existing-but-corrupt content (confirmed matching FNA, `P9-HARDWARE-003`: FNA never checks its own native bank-creation calls' return codes either) |
| **Not yet implemented / open decision** | RPCs targeting a DSP preset (`parameter >= RPC_PARAMETER_COUNT`) remain unevaluated — no DSP preset system exists at all |

#### `Apply3D` / 3D audio fidelity — consolidated summary (`P9-3D-001..010`)

Fáze 9's `P9-3D` group (`plan_audio.md`) audited every piece of `Apply3D` against FNA/FAudio one
at a time; this is the consolidated result now that all nine original items plus one user-directed
follow-up (`P9-3D-010`) have landed. Per-property breakdown:

| Property | Fidelity | Notes |
|---|---|---|
| Distance attenuation | **Exact** | Matches FAudio's `F3DAudio.c` `ComputeDistanceAttenuation` no-custom-curve formula precisely: full volume within `SoundEffect.DistanceScale`, inverse-distance falloff (`gain = DistanceScale/distance`) only strictly beyond it (`P9-3D-003`). |
| Doppler pitch shift | **Exact** | Matches FAudio's `F3DAudio.c` `CalculateDoppler` precisely: relative-velocity projection onto the emitter-to-listener axis, `SpeedOfSound`/`DopplerScale` clamping, NaN guard, `[0.5, 4.0]` output clamp (`P9-3D-004/005`). Needed no native 3D audio API — pure math over `Position`/`Velocity`, applied via the same `MIX_SetTrackFrequencyRatio` the plain `Pitch` property already uses. |
| Pan | **Approximate** | Linear projection onto the listener's own right axis (`Cross(Forward, Up)`, normalized) over distance, clamped to `[-1,1]` — orientation-aware since `P9-3D-010` (previously raw world-space `(emitter.X - listener.X) / distance`, ignoring listener orientation entirely; found during the `P9-3D-009` audit and implemented as a user-directed follow-up). One remaining known gap vs. real X3DAudio: still a single linear axis, not X3DAudio's full multi-speaker energy-diffusion azimuth pipeline (no equivalent in SDL3_mixer's single stereo-gain-pair model, `CP-19`) — elevation/vertical displacement plays no role, matching CNA's documented no-HRTF/elevation limitation. Stereo sources now crossfeed-blend on a hard pan (`Pan`=±1) instead of eliminating the opposite channel outright, matching FNA's `SetPanMatrixCoefficients` exactly (`P11-PAN-001`, RFC-1 — previously a real gap here, fixed after this row was first written; mono sources are bit-exact either way). The **emitter's** own `Forward`/`Up` remain unread — matching real X3DAudio, where emitter orientation only affects multi-channel emitter configurations, not a mono source's pan. |
| Elevation / true HRTF | **Not supported** | SDL3_mixer has no positional-audio DSP graph or head-related transfer function; no plan to implement (would need a different renderer entirely). |
| Reverb (aux-send routing) | **Not supported** | Confirmed by the `P9-XACT-010` audit to be FACT's sound-level `SOUND_FLAG_HAS_DSP` reverb-send-enable flag, not something `Apply3D` itself touches; SDL3_mixer has no aux-send/return bus for it to route to. |

**Bottom line:** of `Apply3D`'s three positional effects (pan, distance attenuation, Doppler),
two are bit-exact matches for real XNA/FNA's math, and the third (pan) is a deliberate,
narrowly-scoped linear approximation that's now orientation-aware (`P9-3D-010`) but still not a
full multi-speaker azimuth port, with the single-linear-axis/no-elevation limitation as its one
remaining named gap rather than a vague "not fully implemented" (stereo hard-pan's own separate
crossfeed gap was fixed by `P11-PAN-001`). Elevation/HRTF and reverb aux-send remain out of scope,
since both would require a fundamentally different audio backend than SDL3_mixer.

#### SDL3_mixer vs FAudio/FACT renderer limitations (`P9-DOCS-006`)

CNA's audio backend is **SDL3_mixer**, not FAudio/FACT — XACT content (`.xgs`/`.xsb`/`.xwb`) is
parsed by a hand-written `CNA::Internal::Audio::XactParser` and played through SDL3_mixer's own
mixing graph, which is structurally different from FAudio's XAudio2-derived voice graph. Concrete
consequences, all downstream of this one architectural choice:

- **No `F3DAudio` equivalent.** SDL3_mixer has no positional-audio DSP graph; pan and distance
  attenuation are approximated at the CNA layer, not computed by the renderer. Doppler pitch shift
  is the exception: it's a closed-form formula over `Position`/`Velocity` needing no native 3D
  audio API at all, so CNA computes it exactly (matching FAudio's `F3DAudio.c`
  `CalculateDoppler`, `P9-3D-004/005`) and applies it via `MIX_SetTrackFrequencyRatio` (the same
  primitive already used for the plain `Pitch` property). Correspondingly XACT `FACT3DApply`-style
  dedicated 3D application from `Cue` also is not the same call path — CNA approximates it via
  `SoundEffectInstance::Apply3D` on every playing wave.
- **No aux-send/return bus.** FAudio (like XAudio2) supports a shared reverb submix voice;
  SDL3_mixer has no equivalent routing concept, so reverb has no renderer primitive to attach to.
- **Only one "cooked" (post-mix, pre-output) callback slot per track.** This is why the DSP filter
  (`T-4C`) and a hypothetical stereo-crossfeed pan implementation (`CP-19`) can't coexist without a
  real redesign — SDL3_mixer's per-track callback isn't a chain, it's a single slot.
- **Stereo panning is a 2-value gain pair (`MIX_StereoGains`), not a 4-coefficient output matrix.**
  FAudio/XAudio2 can crossfeed (send part of the left channel to the right output and vice versa);
  SDL3_mixer's own `MIX_SetTrackStereo` primitive can only scale each channel's own output, so
  using it alone would silence the opposite channel on a hard pan instead of blending it in. CNA
  works around this by fixing `MIX_SetTrackStereo` to unity and applying a real 4-coefficient
  crossfeed matrix itself (matching FNA's `SetPanMatrixCoefficients` exactly) in the same per-track
  cooked callback the DSP filter above already uses (`P11-PAN-001`, RFC-1) — so a hard pan blends
  both channels into the favored speaker, matching FNA, not a remaining gap.
- **Loop region uses a "stop at frame N" property (`MIX_PROP_PLAY_MAX_FRAME_NUMBER`) combined with
  a separate loop-back point (`MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER`).** Despite having no
  dedicated `LoopBegin`/`LoopLength` pair, the combination matches FNA/XAudio2's semantics exactly:
  the intro plays once, then only the loop region repeats -- confirmed against real decoded audio
  via a raw SDL3_mixer callback (`P10-LOOP-003/004`), not just inferred from the property docs.
- **Global master gain is a real, live mixer-level primitive** (`MIX_SetMixerGain`/
  `MIX_GetMixerGain`) — unlike some of the above, this is a case where SDL3_mixer's model is
  *simpler and better suited* than re-deriving master volume per-track would have been (`CP-16`).

#### FNA-matching vs CNA-specific compromise (`P9-DOCS-007`)

Two different kinds of "doesn't match FNA line-for-line" show up in this module, and it matters
which one a given deviation is:

1. **Deliberate CNA-specific compromises**, forced by the SDL3_mixer backend choice — the entire
   "SDL3_mixer vs FAudio/FACT" list above. These are documented in `CHECKLIST.md` precisely because
   they are permanent, reasoned trade-offs, not bugs to eventually fix. (The interactive-variation
   selection used to be a uniform-pick fallback here, but that was a real gap, not a renderer
   compromise — it's been variable-range-driven since `P9-XACT-002/003/004`, see Implemented above;
   this stale claim was corrected during the `P10-AUDIT` pass, `plan_audio.md`.)
2. **Bugs that were fixed to *actually* match FNA**, not compromises — e.g. every `P9-LIFECYCLE`/
   `P9-CATEGORY`/`P9-VALIDATION` fix in Fáze 9 (natural cue-completion reconciliation, the
   category mutate-during-iteration bug, the `offset+count` integer-overflow segfault, `Resume()`
   calling `Play()` when never-started) made CNA *more* faithful to FNA, they didn't introduce new
   compromises. Where CNA intentionally throws instead of replicating an FNA quirk (`Cue::
   GetVariable`/`SetVariable` after Dispose, `P9-LIFECYCLE-015`; `Resume()` after Dispose,
   `P9-VALIDATION-010`), it's because the FNA behavior being deviated from is itself an
   unintentional native-crash bug in FAudio, not a documented contract — see each fix's own
   comment in source for the specific FNA/FAudio line reference.

When in doubt about which category a future finding falls into: if fixing it would require
touching SDL3_mixer's actual capabilities, it's category 1 (document in `CHECKLIST.md`). If it's
purely a CNA-side logic bug independent of the renderer, it's category 2 (just fix it).

#### Full per-member cross-reference (`P10-AUDIT-002/003`)

The compatibility table above answers "is X implemented?" at class granularity. This section goes
one level deeper: every public property/method/constructor/enum-value/exception across all 18
files in `include/Microsoft/Xna/Framework/Audio/`, bucketed as **Implemented+Tested**,
**Implemented, untested**, **Approximate**, or **Unsupported**, cross-referenced against the
authoritative FNA source (`/rv/data/library/github.com/FNA-XNA/FNA/src/Audio/*.cs`) and this
repo's test suite. Produced by five parallel audit passes (2026-07-07); every "new gap" they found
is either fixed in this same pass (see the list at the end) or explicitly deferred with a reason.

**`SoundEffect`** — every constructor overload (file / buffer / buffer+range+loop), copy-deleted +
move ctor/assign, dtor/`Dispose`, `IsDisposed`, `Name` get/set, static `MasterVolume`/
`DistanceScale`/`DopplerScale`/`SpeedOfSound` (incl. `ArgumentOutOfRangeException` guards),
`CreateInstance`, both `Play` overloads, static `GetSampleDuration`/`GetSampleSizeInBytes`,
`FromStream` (incl. 7 malformed-input paths) — all **Implemented+Tested**. `Duration` get is
**Implemented, untested in isolation** (only exercised transitively via `FromStream`'s duration
checks against a known buffer) — fixed this pass, see below.

**`SoundEffectInstance`** — dtor/move ctor/assign, `Play`/both `Stop` overloads/`Pause`/`Resume`,
both `Apply3D` overloads (incl. `NotSupportedException` for `listenerCount != 1`), `IsDisposed`,
`Volume`/`Pan`/`Pitch`/`IsLooped` get/set (incl. post-`Play` `IsLooped` `InvalidOperationException`),
`State`, and the private filter machinery (`INTERNAL_apply{Low,High,Band}PassFilter`,
`INTERNAL_applyXactTrackFilter`, `INTERNAL_applyRpcFilterOverride`,
`INTERNAL_calculateFilterCutoff`/`OneOverQ`, `INTERNAL_calculateListenerRight`/`calculatePan`) via
`SoundEffectInstanceTestAccess` — all **Implemented+Tested**. `INTERNAL_applyReverb` is the one
**Unsupported** member (documented no-op, tested for non-throw).

**`DynamicSoundEffectInstance`** — ctor (permissive, no validation, matching FNA exactly,
`P10-DYN-001/002/003`), `PendingBufferCount`, `IsLooped` (no-op override), `Dispose`,
`GetSampleDuration`/`GetSampleSizeInBytes`, `Play`/`Stop`(both)/`Pause`/`Resume`, both
`SubmitBuffer` overloads (incl. `int32` offset+count overflow regression), both
`SubmitFloatBufferEXT` overloads, `State`, `BufferNeeded` (incl. reentrancy) — all
**Implemented+Tested**.

**`AudioEngine`** — `ContentVersion`, both constructors, `IsDisposed`, `GetCategory` (all 3
exception paths), `GetGlobalVariable`/`SetGlobalVariable` (all exception paths), `Update`,
`Dispose`/`Disposing`, internal registries (wave bank/category/cue-instance-limit bookkeeping) —
all **Implemented+Tested**. `RendererDetails` is **Implemented, weakly tested** (the one test only
asserts non-emptiness, not the exact single `("SDL3_mixer", "SDL3_mixer")` entry) — strengthened
this pass, see below.

**`AudioCategory`** — `Name`, `Pause`/`Resume`/`SetVolume`/`Stop` (real routing verified against
active cues), `Equals`/`GetHashCode`/`operator==`/`operator!=` — all **Implemented+Tested**.
`GetHashCode` uses `std::hash<std::string>`, not .NET's algorithm — already a documented, generic
Audio-wide deviation (`CHECKLIST.md`), not new here.

**`SoundBank`** — ctor (incl. null-engine/empty-filename/missing-file/corrupt-file),
`IsDisposed`/`IsInUse` (by design only reflects fire-and-forget `PlayCue` cues, not
`GetCue()`-obtained ones, matching its own doc comment), `GetCue`, both `PlayCue` overloads,
`Dispose`/`Disposing` — all **Implemented+Tested**.

**`WaveBank`** — non-streaming ctor — **Implemented+Tested**. Streaming ctor's `offset`/
`packetSize` parameters are **Approximate**: parsed into the signature but never forwarded to the
real stream-open call, matching FNA's own `WaveBank.cs` exactly (already documented,
`CHECKLIST.md`). `IsDisposed`/`IsPrepared` (incl. corrupt-file)/`IsInUse` (incl. paused/dispose/
natural-completion), `Dispose`/`Disposing` — **Implemented+Tested**.

**`Cue`** — `Disposing`, `IsDisposed`, `IsPaused` (independent flag alongside `Playing`, matching
real FACT's bitmask, `P9-LIFECYCLE-013`), `IsPlaying` (incl. natural-completion reconciliation),
`IsPrepared`, `IsStopped`, `IsStopping` (real tail state — authored fade **and** RPC-only release,
`P10-RPC-004`), `Name`, `Apply3D`, `GetVariable`/`SetVariable` (incl. every built-in variable:
`Distance`/`DopplerPitchScalar`/`OrientationAngle` live-written by every `Apply3D` call,
`AttackTime`/`ReleaseTime` live only through RPC curve evaluation and never through plain
`GetVariable`, correctly matching real FACT's asymmetry), `Play`, `Pause`/`Resume` (no-op, not
throw, after `Dispose` — deliberate, safer-than-FNA divergence), `Stop` (both
`AudioStopOptions` values), `Dispose` — all **Implemented+Tested**. `IsCreated`/`IsPreparing` are
**Approximate**: always `false` and permanently unreachable, since CNA's synchronous `.xsb`
parsing skips FACT's `CREATED`→`PREPARING` phase entirely (`state_` starts at `Prepared` in the
ctor and never regresses) — tested and commented in `CueTests.cpp`, but not previously in
`CHECKLIST.md`/this doc — added this pass, see below.

**`AudioListener`/`AudioEmitter`** — ctor, `Forward`/`Position`/`Up`/`Velocity` get/set (round-trip
tested), `AudioEmitter::DopplerScale` (incl. negative → `ArgumentOutOfRangeException`) — all
**Implemented+Tested**. No coordinate-flip logic vs. FNA's internal `F3DAUDIO_LISTENER`
Z-negation, but that's an unobservable FNA-internal marshaling detail (`get(set(x)) == x` holds
identically on both sides), not a behavioral difference.

**`Microphone`** — `Name`, static `All`/`Default`, `BufferDuration` get/set (incl. both throw
paths), `IsHeadset` (hardcoded `false` — confirmed this **matches FNA exactly**: FNA's own getter
has a `// FIXME: I think this is just for Windows Phone?` comment and also unconditionally returns
`false`), `SampleRate` (hardcoded 44100, matches FNA's `SAMPLERATE` constant), `State`, both
`GetData` overloads (incl. negative offset/beyond-buffer/zero-or-negative count/integer-overflow
guards), `GetSampleDuration`/`GetSampleSizeInBytes` (delegate to `SoundEffect`), `Start`/`Stop` —
all **Implemented+Tested**. `BufferReady` and the internal `CheckAllBuffers` sweep are
**Implemented, untested for the real-hardware-capture-exceeds-threshold path** (only the
empty-subscriber-list/below-threshold cases are tested) — expected, already covered by `NEXT.md`'s
general "device-dependent tests only run against the dummy driver" note, not a new gap.

**Enums** (`AudioChannels`, `AudioStopOptions`, `MicrophoneState`, `SoundState`) and
**`RendererDetail`** — every value/member **Implemented+Tested**, exact match to FNA.

**Exceptions** — `NoAudioHardwareException`'s ctors/hierarchy are **Implemented+Tested**, and it
**is** thrown for real from `SoundEffect`/`DynamicSoundEffectInstance` when the mixer device fails
to open, with dedicated end-to-end coverage via the `cna_audio_no_hardware_harness` subprocess.
`NoMicrophoneConnectedException`/`InstancePlayLimitException`'s ctors/hierarchy are
**Implemented+Tested**, but neither is ever thrown anywhere in CNA production code — confirmed by
grepping FNA's own `Audio/*.cs`, this is exact dead-code parity with the reference (FNA declares
both but never throws either from its own Audio source either), not a gap.

**Gaps found by this audit and their disposition:**
- `Cue::IsCreated`/`IsPreparing` permanently unreachable — undocumented until this pass; added to
  `CHECKLIST.md` and the compatibility table above.
- `CueTests.cpp`'s `IsStoppingIsAlwaysFalse` test name/comment was stale (written before
  `P9-STOP-010`/`P10-RPC-004` added real `IsStopping` tail states) — renamed and recommented to
  describe the specific case it actually covers (immediate-stop-only, no authored tail), not a
  blanket claim the rest of this same file's own tests already contradict.
- `SoundEffect::Duration` had no dedicated direct test against a known buffer — added.
- `AudioEngine::RendererDetailsNonEmpty` only asserted non-emptiness — strengthened to assert the
  exact single SDL3_mixer entry.
- `Microphone.hpp`'s `setBufferDurationProperty` Doxygen comment stated the valid range as
  "[100, 999]"; the real (FNA-matching) condition is `< 100 || > 1000` — corrected the comment
  (behavior was already correct; this was a wording-only nit).
- Every other candidate gap the five audit passes surfaced was, on inspection, either already
  documented in `CHECKLIST.md` or a confirmed exact match to FNA's own behavior (including two
  cases of FNA's own dead code) — no new production behavior changes were needed.

### `Microsoft::Xna::Framework::Input::Touch`

- `TouchPanel`, `TouchPanelCapabilities`, `TouchCollection`, `TouchLocation`, `TouchLocationState`, `GestureSample`, `GestureType`: headers exist, full API surface.
- SDL3 touch renderer is wired up (`feature/input` branch, `plan_input.md` Phase I2, INPUT-TOUCH-*/INPUT-GESTURE-* cluster): `SDL_EVENT_FINGER_*` feeds `TouchPanel::INTERNAL_onTouchEvent`, `TouchDeviceExists`, and `DisplayWidth`/`DisplayHeight` (from the real back-buffer size). Gestures (Tap, DoubleTap, Hold, Horizontal/Vertical/Free drag, Flick, Pinch, PinchComplete) are recognized end-to-end by `GestureDetector` and covered by a dedicated test suite.
- **Input member-level parity (2026-07-06):** the full public Input surface is now mechanically parity-checked against FNA — member/signature parity via the generated `docs/input-member-parity-matrix.md` (INPUT-API-027) + the compile-time signature freeze (INPUT-API-031), and enum values byte-pinned (INPUT-API-034). Keyboard keycode/scancode maps are byte-identical to FNA (INPUT-KBD-009/010). See `plan_input.md` for the per-type task status.
- Gesture recognition is a byte-faithful port of FNA's `GestureDetector.cs` (audited, task 829) with deterministic clock-injected tests (task 830); multi-touch edge cases + coordinate scaling covered (tasks 825–828).
- Known deviation: `TouchPanel::GetState()` reads a panel-owned event map rather than FNA's per-frame poll population of `touches_` (documented in-source, task 714/PLAT-86) — CNA's input bridge is event-driven, not poll-driven. The map is internally unbounded, but the public snapshot is deterministically capped at `MAX_TOUCHES` (8) to match FNA (DEC-10, 2026-07-05).
- `TouchPanel::GetCapabilities()` reports `MaximumTouchCount = 0` when disconnected and **4** when connected, matching FNA/XNA (DEC-09, 2026-07-05 — XNA always reports 4; a fixed XNA-compat value, not the `MAX_TOUCHES` tracking cap).
- **Status:** Implemented

### `Microsoft::Xna::Framework::Media`

- `Song`, `SongCollection`, `MediaPlayer`, `MediaQueue`: implemented through CNA's audio facade.
- `Album`, `Artist`, `Genre`, `Picture`, `Playlist` and their collections: implemented by the
  local-library index, including tags, playlists, thumbnails, and platform-provided user folders.
- `MediaLibrary`: implemented as a synchronous local-library snapshot.
- `Video`, `VideoPlayer`: implemented with FFmpeg decoding and an opaque CNA mixer playback stream.
- **Status:** Implemented, with the documented compatibility deviations in `CHECKLIST.md`.

### `Microsoft::Xna::Framework::Storage`

- `StorageDevice`, `StorageContainer`, `StorageDeviceNotConnectedException`: headers exist with full XNA API shape. Behavior is implemented via native file-system calls.
- **Status:** Implemented

### `Microsoft::Xna::Framework::GamerServices`

**Updated (`feature/net`):** the previous "`Guide`-only" characterization is stale — see §3 above
for the full class list, now all implemented. `Guide` itself (`Show()`, `IsTrialMode`) remains a
real UI-less stub (no on-screen system UI to show, consistent with FNA's own PC-targeted `Guide`
being similarly minimal), but the surrounding real API — signed-in gamer state, presence,
achievements, leaderboards, friends, privileges, and the Avatar real-rendering extension — is
implemented and exercised by the `demo_*` avatar/net examples and their own test suites.
- **Status:** Implemented (Guide itself remains a minimal stub — no system UI exists to show)

---

## 5. XNA 4.0 API Not Present in FNA

These classes were part of XNA 4.0 but FNA does not implement them, because FNA targets PC XNA
only, where GamerServices/Avatar/Net are Xbox-Live/Xbox-360-exclusive and have no PC equivalent.

**Superseded by decision 1a (`plan_net.md`):** the three subsections that originally lived here —
GamerServices, Avatar API, and Net — are **no longer excluded or "not planned."** CNA made the
real Xbox 360 XNA 4.0 reference behavior (not Windows' PC no-op stubs) the correctness bar for
these three areas specifically, since accurate multiplayer/avatar behavior has real value for CNA
games even though it's outside FNA's own PC-only scope. All three now have real, tested
implementations:

- **GamerServices** — see §2/§3/§4 above for the current per-class implementation status.
- **Avatar API** — the real-rendering extension is documented in full in
  `docs/avatar-real-rendering-ext.md`; `AvatarRenderer`, `AvatarAppearanceEXT`, wardrobe attach/
  detach, and skeletal animation are implemented and exercised by the 8 `demo_avatar_*` examples.
  (FNA's own faithful-XNA-surface `AvatarRenderer` — `State`/`ParentBones`/`BindPose` — is
  preserved separately and remains `Unavailable`/throws exactly as real XNA does off-Xbox; see
  `demo_avatar_bone_state_boundary` for a live comparison of both paths.)
- **Net** — `Microsoft.Xna.Framework.Net` (`NetworkSession`, `PacketReader`/`Writer`,
  `NetworkGamer`, etc.) is real, ENet-backed transport, not a stub. `NetworkSessionType::SystemLink`
  (LAN-style local play, including real host migration and simulated latency/packet-loss testing
  hooks — `plan_net.md` Phases 5/6) is fully implemented; `PlayerMatch`/`Ranked`/session invites
  remain documented stubs (no matchmaking renderer exists to implement them against — see §9 for
  the current per-feature breakdown).

---

## 6. Intentionally Excluded or Deferred API

### `Microsoft::Xna::Framework::Design` — TypeConverter classes

FNA provides 13 TypeConverter subclasses (e.g. `BoundingBoxConverter`, `ColorConverter`) that integrate XNA math types with `System.ComponentModel.TypeDescriptor` for use in .NET design-time editors (Visual Studio property grid).

**Why excluded from CNA:**
- `System.ComponentModel` is a .NET-only framework; no C++ equivalent exists.
- CNA has no design-time editors.
- These classes have zero runtime value for game code.
- Implementing a `TypeConverter` abstraction in sharp-runtime solely for this purpose would add significant complexity for no benefit.

**Decision:** Permanently excluded. If ever revisited, a minimal `ITypeConverter` interface could be added to sharp-runtime with no-op converters.

### `Microsoft::Xna::Framework::Content` — XNB pipeline classes

CNA's `ContentManager` is not XNB-based. It uses file-extension readers instead of the XNB binary format.
**Not permanently excluded** — a real binary `.xnb` loader is a plausible future addition, but low
priority (no current CNA task is blocked on it) and a substantial undertaking (LZX decompression
port, a reflection-free type-reader dispatch table, ~40 per-type readers). See `xnb.md` (repo root)
for a from-source analysis of what it would actually require, written as planning material only —
no implementation exists or is scheduled.

**Deferred (not implemented, low priority — see `xnb.md`):**
- `ContentReader` (reads from an XNB stream)
- `ContentTypeReaderManager` (manages XNB type reader registrations)
- `ContentSerializerAttribute` and siblings (content pipeline build-tool attributes)
- `LzxDecoder` (XNB LZX compression)

**Deferred:**
- `ResourceContentManager` — loads from C++ embedded resources; no XNB needed. Should be stubbed as a subclass of `ContentManager` with a `// CNA_STUB:` comment.

### FNA-Internal Classes (excluded as non-XNA-4.0 API)

| Class | Reason |
|-------|--------|
| `FNA3D` | FNA private GPU renderer |
| `PipelineCache` | FNA internal shader cache |
| `ProfileCapabilities` | FNA internal feature detection |
| `VertexDeclarationCache` | FNA internal |
| `DxtUtil`, `X360TexUtil` | FNA internal texture utilities |
| `EffectHelpers`, `EffectMaterial`, `Resources` | FNA internal effect helpers |
| `GestureDetector` | FNA internal touch gesture FSM |
| `FNALoggerEXT` | FNA-specific logging extension |
| `BaseYUVPlayer`, `IVideoPlayerCodec`, `VideoPlayerAV1`, `VideoPlayerTheora` | FNA internal video codec renderers |
| All `FNAPlatform/*`, `Utilities/*` | FNA platform glue |

---

## 7. Stock Effect Renderer Parity

**Rewritten 2026-07-09 (Task 481) — the previous version of this section (dated 2026-06-26, Task
196) was stale by an entire session's worth of work** (`plan_graphics.md` Phases 71–73: EasyGL
final gap closure, Bgfx full 2D+3D pixel-verified parity, Vulkan gap closure) and its central claim
— "Bgfx `SetDepthTestEnabled`/`SetBlendEnabled` still throw, no 3D pixel tests possible" — is no
longer true. Rather than re-duplicate detailed per-effect/per-renderer tables here (which drift
stale again the same way), this section now points at the single, currently-maintained source:

**See `docs/graphics-renderer-feature-matrix.md`** (Task 451) for the authoritative "Stock Effects"
table (all 5 stock effects × 4 renderers, per-feature rows down to `DirectionalLight1`/`2`,
`SpecularColor`/`Power`, `VertexColorEnabled`, `WeightsPerVertex`), the "2D SpriteBatch/SpriteFont"
table, and the full list of currently-BLOCKED tasks (686/687 SDL_Renderer `Wrap`/`Mirror`, 725
SDL_Renderer `Texture3D`/`TextureCube`, 732 EasyGL non-`Color` `SurfaceFormat` — Task 447 Vulkan
OcclusionQuery is no longer on this list, see the 2026-07-10 update below).

**Update (2026-07-10, Task 823) — Phase 72 (Bgfx full 2D+3D pixel-verified parity) is now closed
in full**: of the 38 real gaps found in a first-ever complete row-by-row triage of that phase's 85
rows, 37 are now ✅ closed this session (2 real, previously-undiscovered bugs found and fixed along
the way — `BgfxRenderer::ApplySamplerState`'s incomplete `TextureFilter` split-Min/Mag/Mip
mapping, Task 743; `RenderTarget2D`/`RenderTargetCube`'s missing `SurfaceFormat` validation, Task
774, both shared-code fixes affecting all 4 renderers) and 1 remains explicitly `NEEDS_HUMAN`
(Task 767, depth bias/slope-scale depth bias — bgfx has zero depth-bias mechanism at the API
level, a permanent ceiling needing a project-owner decision on shader-level Z-offset emulation).
Every `GraphicsDevice` state-object category (`DepthStencilState`/`RasterizerState`/`BlendState`/
`SamplerState`), `SpriteBatch`/`SpriteFont`/`Model`/`OcclusionQuery`/`Texture2D`/`Texture3D`/
`TextureCube`/`RenderTarget2D`/`RenderTargetCube`/`Viewport` row group is now fully pixel-verified
on Bgfx — see `plan_graphics.md`'s own Phase 72 intro blockquote for the complete session log.

**Update (2026-07-10, Task 860) — Phase 73 (Vulkan gap closure) is now closed in full too**: of
the phase's 8 confirmed real gaps (`SpriteBatch` sort-mode/rotation/scale/crop/flip, `SpriteFont`,
`Model` hierarchy — Task 861's own finding — plus `Texture2D`/`Texture3D` partial-region/NPOT
tests) and 2 UNCERTAIN rows (MRT mixed-format, `Viewport` math), every one is now ✅ — no bugs
found on Vulkan itself in any of them (Task 774's shared-code `SurfaceFormat`-validation fix,
found while closing Bgfx's identical row, already covered Vulkan too). At the time of this update,
only Task 854 (`OcclusionQuery` pixel/query test) remained open, `BLOCKED` on Task 447's own
already-tracked architecture decision — see the update immediately below for its resolution later
the same day.

**Update (2026-07-10, Task 854) — Task 447/854 (Vulkan OcclusionQuery) is now fully CLOSED, and
Phase 73 has moved to `plan_graphics_20260709.md` in full** (every one of its 37 rows now ✅). The
project owner picked a direction for Task 447's 3 open design questions (tagging mechanism,
multi-draw-span policy, per-frame query-pool reset sequencing) and all 3 are now implemented: a
new `Pending3DDraw::occlusionQuery` field tags every deferred draw with the currently-active
query, `RecordCommandBuffer()` wraps contiguous same-query draw runs in a real `vkCmdBeginQuery`/
`vkCmdEndQuery` pair (one render pass at a time — a query spanning a render-target switch is not
re-opened, matching the approved "reject additional spans across a render-pass boundary" policy),
and every query tagged on a pending draw gets a fresh `vkCmdResetQueryPool` each frame. New
`Vulkan_OcclusionQuery_PixelCount` test (6/6 pass) confirms genuinely accurate, discriminating
pixel counts (4096 for a fully-visible 64×64 quad, 0 when occluded, 4096 again when split across 2
draws in one query span) — see `docs/occlusionquery-support.md`'s own updated Vulkan section for
full detail.

**Headline summary as of 2026-07-09** (see the matrix doc for detail and task numbers):

- **Core rendering for all 5 stock effects** (MVP transform, lighting, texture sampling, fog) is
  implemented and pixel-verified on **EasyGL, Vulkan, and Bgfx** — Bgfx's own 3D pipeline (depth
  test, blend state) is real and working, not the blocked stub the previous version of this
  section described.
- Remaining gaps are narrow, named, per-feature items, not whole-renderer blockers: e.g.
  `AlphaTestEffect.VertexColorEnabled` (Vulkan/Bgfx, Task 887), `DualTextureEffect.
  VertexColorEnabled` (all 3, Task 889), `EnvironmentMapEffect`'s secondary directional lights and
  base-lerp alpha scaling (Vulkan/Bgfx, Tasks 890/891), `SkinnedEffect`'s secondary lights/specular/
  `WeightsPerVertex` enforcement (Vulkan/Bgfx, Tasks 893-895).
- `ShaderEffect` (custom shader source): implemented and pixel-tested on EasyGL (GLSL) and Vulkan
  (SPIR-V); Bgfx's `CreateEffectRenderer` still returns `nullptr` for it — the one remaining
  whole-feature gap in this section.
- **SDL_Renderer** is a 2D-only renderer by design (stock 3D effects are N/A there) — but its own 2D
  path (`SpriteBatch`/`SpriteFont`/`BlendState`/etc.) went through a full, dedicated audit phase
  this session (`plan_graphics.md` Phase 70, 15 real bugs found and fixed) and is now comprehensively
  pixel-verified; see `docs/sdl-renderer-2d-completeness.md`.
- `GraphicsDevice` state objects (`BlendState`/`DepthStencilState`/`RasterizerState`/`SamplerState`)
  have their own per-renderer correctness table in the feature matrix, separate from the stock-effect
  table above. **Update (2026-07-09):** Vulkan's `BlendState` (previously "almost entirely fake" —
  hardcoded one blend equation regardless of request) is now fixed (Task 868), as is Bgfx's own
  narrower version of the same gap (Task 923) — the stock effects themselves always rendered
  correctly regardless.

---

## 8. Overall Coverage Estimate

Coverage is estimated as the fraction of public XNA 4.0 API surface that is usable
(not merely declared) in a typical 2D or 3D game on the EasyGL renderer.

**Note (2026-07-09, Task 481; updated Task 739):** this table's own framing is EasyGL-scoped by
design (see line above) and its Graphics-related rows are still broadly accurate for that one
renderer. It does **not** describe Vulkan/Bgfx/SDL_Renderer coverage — those differ meaningfully
per feature (Vulkan's `BlendState` was almost entirely fake, now fixed, Task 868; Bgfx's own
narrower version of the same gap also fixed, Task 923; SDL_Renderer is comprehensively
pixel-verified for 2D but has 5+ named BLOCKED/architectural gaps). See
`docs/graphics-renderer-feature-matrix.md` for the current, per-renderer, per-feature breakdown
rather than relying on a single blended percentage across 4 renderers with genuinely different
maturity levels.

| Subsystem | Estimated coverage | Notes |
|-----------|-------------------|-------|
| Math types (`Vector2/3/4`, `Matrix`, `Color`, `BoundingBox`, etc.) | ~98 % | All types implemented; minor numeric-precision edge cases only |
| `GraphicsDevice` (core device + state) | ~85 % | Missing: some query/counter methods; sRGB SurfaceFormats silently linear |
| `SpriteBatch` | ~95 % | All overloads; all `SpriteEffects`; `transformMatrix`; tested |
| `Texture2D / Texture3D / TextureCube` | ~90 % | All mip levels, partial rects, all 6 cube faces; sRGB formats not fully mapped |
| `RenderTarget2D / RenderTargetCube` | ~90 % | DiscardContents/PreserveContents; MRT; round-trip tested |
| `VertexBuffer / IndexBuffer` | ~90 % | All typed + raw paths; skinned stride 52; tested |
| `BasicEffect` | ~95 % | All 4 shader variants; lighting; fog; all property setters |
| `AlphaTestEffect` | ~90 % | All 8 `CompareFunction` modes pixel-tested; fog not wired |
| `DualTextureEffect` | ~90 % | Both texture slots + diffuse multiplier pixel-tested |
| `EnvironmentMapEffect` | ~90 % | EmissiveColor, EnvMapAmount, EnvMapSpecular pixel-tested |
| `SkinnedEffect` | ~85 % | Bone identity/translate/blend pixel-tested; full skinned pipeline |
| `ShaderEffect` (custom GLSL/SPIR-V) | ~80 % | EasyGL GLSL and Vulkan SPIR-V tested; no HLSL path |
| `PackedVector` (all 17 types) | ~100 % | Full Pack/Unpack with correct rounding; golden-value + edge-case tests |
| `SpriteFont` / `Model` | ~80 % | Functional for typical use; some edge-case APIs stubs |
| `SoundEffect / SoundEffectInstance` | ~95 % | SDL3_mixer backend; real filters, instance-tracking cascade; 3D is pan+attenuation+Doppler (no HRTF/elevation, documented) |
| `MediaPlayer / VideoPlayer` | ~95 % | FFmpeg video; CNA audio facade; local Album/Artist/Genre/Picture/Playlist library implemented |
| `ContentManager` | ~65 % | File-extension readers; no XNB; no ServiceProvider property |
| `StorageDevice / StorageContainer` | ~90 % | Native filesystem; full XNA API shape |
| `GamePad / Keyboard / Mouse` (XNA 4.0 core) | ~100 % behavior | SDL3 renderer; FNA-faithful `GetHashCode`/`ToString`/ordering, keycode/scancode maps, dead-zone math, button/axis mapping — all wired and tested (`feature/input` Phases I3–I5, I9–I10). `Mouse::SetPosition` now converts logical→window for scaled/letterboxed windows (a-0001, task 846) — no remaining input-layer gap; residual items are platform/hardware-gated only. |
| `TextInputEXT` / `Mouse`+`GamePad` EXT / `Keyboard` scancode EXT | ~95 % behavior | FNA extensions, all implemented and FNA-faithful (`feature/input` Phases I1, I3–I5, I9): `TextInputEXT` is `char16_t`/UTF-16 with Unicode/IME tests; relative mouse, `ClickedEXT`, rumble, `GetGUIDEXT` (format fixed, task 816), gyro/accel. Untested slice is hardware/IME-gated. |
| `MouseCursor` (MonoGame-inspired, `CNAEXT`) | ~100 % of exposed surface | 12 stock cursors, `FromTexture2D`, `IDisposable` singleton-safe dispose; no XNA/FNA equivalent. |
| `Input::Touch` | ~98 % behavior | Gesture pipeline (Tap…PinchComplete) byte-faithful FNA port, wired end-to-end and tested with a deterministic clock (`feature/input` Phase I2, I9). Documented deviations only: event-driven vs. poll-based `GetState()`. `MaximumTouchCount` reports 4 and `GetState()` caps at `MAX_TOUCHES` (8), both matching FNA (DEC-09/DEC-10). |
| `GamerServices` | ~85 % | **Updated (`feature/net`):** real implementation across Achievements/Avatar/Friends/Presence/Leaderboards/Privileges/Profile/SignedInGamer; see §9 for the per-feature breakdown. `Guide` itself remains a minimal stub (no system UI exists to show). |
| `Audio (XACT)` — AudioEngine/SoundBank/WaveBank/Cue | ~97 % | Real `.xgs`/`.xsb`/`.xwb` parser + SDL3_mixer playback; category/lifecycle/3D/instance-limit+fade (both category- and cue-level)/continuous RPC volume+pitch all real, including the built-in `AttackTime`/`ReleaseTime` envelope variables (`P10-RPC-002/003/004`); gaps are documented accepted deviations (no HRTF/elevation, no DSP-preset RPC destination), not missing implementation |
| `Framework.Net` (NetworkSession, etc.) | ~80 % | **Updated (`feature/net`):** real ENet-backed transport, not excluded. `SystemLink` (LAN-style local play, host migration, simulated latency/packet-loss) is fully implemented; `PlayerMatch`/`Ranked`/invites remain documented stubs (no matchmaking renderer to implement them against). See §9 for the per-feature breakdown. |
| **Overall (EasyGL renderer, 2D+3D game)** | **~85 %** | Main gap now: XNB content pipeline (GamerServices/Net no longer main gaps — see updated rows above). (Touch and XACT were main gaps as of this table's original estimate; Touch closed by `feature/input` Phase I2, XACT closed by `feature/audio` — see the `Input::Touch` and Audio rows above.) **Note (2026-07-09, Task 739; GamerServices/Net rows updated `feature/net`):** this is an old, informal, blended (2D+3D+GamerServices+content-pipeline) estimate, not re-derived from a checklist — per this file's own stated policy, informal percentages are "color commentary," not a gate. For the Graphics-only subsystem specifically, `docs/graphics-compatibility-report.md`'s Task 500 milestone (a real, test-execution-verified ~90%, not estimated) supersedes this row for that scope; this ~85% figure has not been recomputed and should not be read as still-current without redoing the underlying count. |

---

## 9. GamerServices / Net support matrix (`feature/net`, Task 9.4)

Per-feature status for the two namespaces §5 used to describe as "not planned"/"intentionally
excluded" (decision 1a superseded that framing — see §5). Categories: **Implemented** (real
behavior matching the Xbox 360 XNA 4.0 reference) / **Locally persisted** (real disk-backed state
via CNA's own storage layer, not a live online service — none exists to connect to, and none is
planned) / **CNA extension** (`CNAEXT`/`*EXT`, beyond the XNA 4.0 API surface) / **No-op** (present,
callable, does nothing) / **Documented stub** (throws, matching either FNA's own acknowledged stub
behavior or a genuinely unimplemented feature).

### GamerServices

| Feature | Status | Notes |
|---|---|---|
| `SignedInGamer` / `Gamer` / `GamerCollection` / `SignedInGamerCollection` | Implemented | Real signed-in-gamer state and collection semantics. |
| `Achievement` / `AchievementCollection` / `AwardAchievement` | Locally persisted | Phase 4 (`LocalGamerServicesStore`, `StorageDevice::GetStorageRootEXT()`-backed) — real disk persistence, not in-memory-only. |
| `LeaderboardReader` / `LeaderboardWriter` / `LeaderboardEntry` / `LeaderboardIdentity` / `LeaderboardKey` / `LeaderboardOutcome` | Locally persisted | Same Phase 4 store as Achievements; previously threw `NotSupportedException` on every read/write path. |
| `GamerPresence` / `GamerPresenceMode` | Implemented | Real presence state. |
| `GamerPrivilegeSetting` / `GamerPrivileges` / `GamerPrivilegeException` | Implemented | |
| `FriendCollection` / `FriendGamer` | Documented stub for population | The classes themselves are real, but `SignedInGamer::GetFriends()` always returns an empty `FriendCollection` (`CreateInternal({})`) - no friend-list population source exists. Self-documented in `FriendCollection.hpp`'s own comment. Found during Task 11.1's final audit; not a Phase 4 persistence gap (out of that task's scope), just an honestly-still-open one. |
| `GamerProfile` / `GamerZone` / `GameDefaults` | Implemented | |
| `Guide.Show` | No-op / documented stub | No system UI exists to show (consistent with FNA's own minimal PC `Guide`). |
| `Guide.BeginShowKeyboardInput` | Implemented | Phase 3 (decision 3a) — real captured text through CNA's `TextInputEXT` input layer, not a stub value. |
| `Guide.BeginShowMessageBox` | Implemented (CNA extension for the visual overlay) | Phase 3 — real message-box overlay. |
| `Avatar*` (`AvatarRenderer`, `AvatarAppearanceEXT`, wardrobe attach/detach, animation) | Implemented, real-rendering `*EXT` | See `docs/avatar-real-rendering-ext.md`. FNA's own faithful-XNA-surface `AvatarRenderer` (`State`/`ParentBones`/`BindPose`) is preserved separately and remains `Unavailable`/throws exactly as real off-Xbox XNA does — see `demo_avatar_bone_state_boundary`. |
| `GamerServicesDispatcher` / `GamerServicesComponent` / `GamerServicesNotAvailableException` | Implemented | |

### Net

| Feature | Status | Notes |
|---|---|---|
| `NetworkSession` create/find/join (`NetworkSessionType::SystemLink`) | Implemented | Real ENet-backed transport and discovery, not a stub. |
| `NetworkSession` host migration | Implemented | Phase 5 — deterministic lowest-wire-id promotion, LAN rediscovery, bounded retry for cross-process discovery-port delivery. |
| `SimulatedLatency` / `SimulatedPacketLoss` | Implemented | Phase 6 — a real receive-side delayed-delivery queue / probabilistic drop on real ENet traffic, deterministic under test (injectable clock/seeded RNG); scoped to AppData delivered to local gamers, not session-management/relay traffic. |
| `LocalNetworkGamer.SendData`/`ReceiveData`, `PacketReader`/`PacketWriter` | Implemented | Real serialization over the real transport. |
| `NetworkSession.Dispose()` / lifecycle | Implemented | Phases 2, 12-14 — several confirmed real bugs (double-dispose use-after-free, async callbacks never invoked, enumerator null-deref after Dispose) found and fixed. |
| `NetworkSessionType::PlayerMatch` / `Ranked` | Documented stub | No matchmaking renderer exists to implement them against; out of scope for a local/LAN-focused transport. |
| Session invites (`InviteAcceptedEventArgs`, invite-based join) | Documented stub | Same reason as PlayerMatch/Ranked — no online invite renderer exists. |

---

## 10. Recommended Implementation Order

1. **Math / common framework types** — ✅ Done: Color, Vector2/3/4, Matrix, Quaternion, BoundingBox/Sphere/Frustum, Ray, Plane, Curve, MathHelper, Point, Rectangle, GameTime.

2. **Input** — ✅ Done: GamePad, Keyboard, Mouse, Touch, TextInputEXT, MouseCursor. All have real,
   FNA-faithful runtime behavior wired to SDL3 (`feature/input` branch Phases I1–I9,
   `plan_input.md` tasks 700–840), not stubs. Coverage is assessed **by category, not blended**
   (per the input review): **XNA 4.0 core** ~99% behavior / ~99% tested (complete & faithful);
   **FNA `*EXT`** ~95% (all implemented; untested slice hardware/IME-gated); **MonoGame
   `MouseCursor`** (`CNAEXT`) complete for the exposed surface; **platform-dependent** items
   (Wayland global-mouse, live sensors/rumble, gamepad hotplug slot-assignment, `SetPosition`
   letterbox) documented, not headless-verifiable. See `plan_input.md`'s "final split" table,
   `AUDIT.md`, `docs/platform-input-notes.md`, and `docs/demo-input-checklist.md` for detail.

3. **Audio** — ✅ Done: SoundEffect/Instance, and XACT (AudioEngine/SoundBank/WaveBank/Cue) via a
   real hand-written parser + SDL3_mixer. Remaining gaps are documented accepted deviations
   (`CHECKLIST.md`), not missing implementation.

4. **Graphics 2D** — ✅ Done: SpriteBatch, Texture2D, SpriteFont, BlendState, RasterizerState, SamplerState.

5. **Graphics 3D** — ✅ Done: VertexBuffer/IndexBuffer, Model, all 5 stock effects (EasyGL pixel-tested), all 17 PackedVector types fully implemented.

6. **Content / Media / Storage** — ✅ Partial: ContentManager (file-extension approach), MediaPlayer, VideoPlayer done. `ResourceContentManager` stub needed.

7. **GamerServices / Net** — ✅ Done: real implementation across GamerServices (Achievements,
   Avatar real-rendering extension, Friends, Presence, Leaderboards, Privileges, Profile,
   SignedInGamer, `GamerServicesComponent`/`GamerServicesNotAvailableException`) and Net (real
   ENet-backed `SystemLink` transport, host migration, simulated latency/packet-loss). See §9 for
   the per-feature breakdown; `Guide.Show` (no system UI) and `PlayerMatch`/`Ranked`/invites (no
   matchmaking renderer) remain documented stubs.

---

## 11. Summary

### What was added by this audit and subsequent tasks

- `docs/xna-4-api-coverage.md` (this file; updated Tasks 196, 200)
- `include/Microsoft/Xna/Framework/Content/ResourceContentManager.hpp` — stub
- `src/Content/Xna/ResourceContentManager.cpp` — stub
- `include/Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp` — stub
- `src/GamerServices/Xna/GamerServicesComponent.cpp` — stub
- `include/Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp` — stub
- All 17 PackedVector types fully implemented with correct FNA Pack/Unpack math (Tasks 197–199)
- `tests/PackedVectorGolden.md` — FNA bit-packing reference table for all 17 types
- §7 Stock Effect Renderer Parity table (Task 196)
- §8 Overall Coverage Estimate table (Task 200)

### What remains missing or incomplete

- `Design` namespace TypeConverter classes (intentionally excluded)
- `ContentReader` XNB-based class (deferred; CNA uses non-XNB approach)
- `ContentSerializerAttribute` family (intentionally excluded)
- **Updated 2026-07-09 (Task 481):** the 2 items previously listed here ("Vulkan pixel tests for
  BasicEffect/AlphaTestEffect/SkinnedEffect", "Bgfx 3D state blocks all Bgfx 3D pixel tests") are
  now DONE — see Phases 71–73 in `plan_graphics.md` and `docs/graphics-renderer-feature-matrix.md`.
  Current real Graphics gaps, all individually tracked (not silently missing). **Updated
  2026-07-09**: Vulkan's `BlendState` (868), Bgfx's narrower `BlendState` gap (923), EasyGL's
  `Anisotropic` fallback (918), `Model`'s missing `rootBoneIndex` (916), `IndexElementSize`'s
  numeric mismatch (921), and `SpriteBatch::Draw`'s missing 7th overload (922) are all now
  CLOSED — removed from this list. Remaining:
  - 4 BLOCKED tasks needing a project-owner architecture decision: 686/687 (SDL_Renderer
    `Wrap`/`Mirror` via `SpriteBatch`), 725 (SDL_Renderer `Texture3D`/`TextureCube`), 732 (EasyGL
    non-`Color` `SurfaceFormat` GPU forwarding). Plus 2 more found since: 863 (`Texture3D`
    shader-sampling architecture) and 869 (`GraphicsDevice` state value-vs-reference semantics) —
    both NEEDS_HUMAN, neither picked. **447 (Vulkan OcclusionQuery) was resolved 2026-07-10** —
    removed from this list, see `docs/occlusionquery-support.md`.
  - `GraphicsDevice.ReferenceStencil` has no renderer connection on EasyGL/Bgfx (Vulkan confirmed
    already-fixed, corrected 2026-07-09, Task 872); `Clear` ignores `ClearOptions::Stencil` on all
    3 3D renderers (Task 871). Both confirmed real, scoped (touches new renderer virtual methods
    across the remaining hardware renderers plus a genuinely new
    stencil-verification pixel test), not yet started.
  - A first-ever full row-by-row triage of `plan_graphics.md` Phases 72/73 (2026-07-09) found
    Bgfx is dramatically less pixel-test-covered than Vulkan for `DepthStencilState`/`SpriteFont`/
    `Model`/`SpriteBatch` behavior/most `BlendState` presets (zero Bgfx tests in these categories)
    — now the single largest known real gap in this project's pixel-verification coverage.
  - A handful of narrow, named per-effect secondary-feature gaps on Vulkan/Bgfx (secondary
    directional lights, specular, vertex-color-enabled variants, `WeightsPerVertex` GPU
    enforcement) — see the feature matrix's "Stock Effects" table for the full list with task
    numbers.
  - `Model`'s content-pipeline loader (`ModelTypeReader`) has real gaps versus FNA's `.xnb` format
    (no bone hierarchy, no `ParentBone` wiring) — `docs/model-content-pipeline-support.md`.

### What is intentionally excluded

- `Design` namespace — requires `System.ComponentModel` which has no C++ equivalent
- `ContentReader` and XNB pipeline — CNA uses file-extension approach, not XNB
- All FNA-internal implementation classes

**No longer excluded (decision 1a, `feature/net`):** GamerServices and Avatar API (Xbox 360
exclusive, but implemented anyway against the real Xbox 360 reference behavior — see §9) and Net
(`Microsoft.Xna.Framework.Net`, real ENet-backed `SystemLink` transport — see §9). `PlayerMatch`/
`Ranked`/session invites remain stubs, but for a different reason: no matchmaking/invite renderer
exists to implement them against, not because the namespace itself is excluded.

### Build status

See build run in task notes. Build must remain clean after each stub addition.

### Recommended next steps

**Updated 2026-07-09** — items 1–2 (Vulkan pixel tests, Bgfx 3D state) from the original version
of this list were DONE by Phases 71–73; item 2 below (`IndexElementSize`) and Vulkan's `BlendState`
half of item 3 are now ALSO done. **Updated 2026-07-10**: item 3 (Phase 72) is now fully closed
too, and item 1's BLOCKED-task count dropped from 7 to 6 (447 resolved). Current real next steps:

1. Resolve the 6 currently-BLOCKED/NEEDS_HUMAN architecture decisions (686, 687, 725, 732,
   863, 869 — see `docs/graphics-renderer-feature-matrix.md`'s own BLOCKED-task table) — each needs
   a project-owner call, not more investigation.
2. Fix `ClearOptions::Stencil` (all 3 3D renderers, Task 871) and `ReferenceStencil` (EasyGL/Bgfx
   only — Vulkan confirmed already-fixed 2026-07-09, Task 872) — confirmed real, scoped, not yet
   started.
3. ~~Close Phase 72 (Bgfx)'s 38 confirmed real pixel-test gaps~~ — **DONE 2026-07-10**, see the
   Phase 72 update above; Phase 73 (Vulkan) is also now fully closed, including Task 854.
4. Close Phase 73 (Vulkan)'s 9 confirmed real gaps — `SpriteBatch`/`SpriteFont`/`Model` pixel
   tests plus `Texture2D`/`Texture3D` partial-region/NPOT tests.
5. ~~Add compile-compatibility stubs for `Gamer` / `SignedInGamer` / `GamerCollection` if target games need them.~~ — **DONE** (`feature/net`): all three are real, implemented classes, not stubs — see §9.
6. Audit `GraphicsDevice` public methods against FNA for any missing overloads or validation differences.
