# SDL_Renderer 2D Renderer — Final Completeness Status

`SDL_Renderer` is CNA's 2D-only graphics renderer: no 3D pipeline, no programmable shader stage, no
depth/stencil buffer, no MSAA. It exists to run genuinely 2D-only XNA games (`SpriteBatch`,
`SpriteFont`, `Texture2D`, `RenderTarget2D` used as plain off-screen sprite surfaces) via SDL3's
2D texture-blit API, with every unsupported 3D-only feature required to fail loudly (throw) rather
than silently misbehave.

This document is the final compatibility/completeness status after the full SDL_Renderer audit,
`plan_graphics.md` Phase 70 ("SDL_Renderer: 2D renderer verified perfection"), Tasks 666–731. Every
row below cites the task(s) that verified it — see `plan_graphics.md`'s own task table for full
FNA-comparison detail, discriminating-power proof, and exact fix code where applicable.

**Status legend**

- ✅ — fully supported, matches FNA/XNA behavior exactly (or matches it as closely as a 2D-only
  renderer reasonably can — e.g. `MultiSampleCount` always reporting 0 back is still the *correct*
  XNA-level contract for a renderer that legitimately has no MSAA).
- ⚠️-emulated — works, but via a renderer-specific accommodation rather than byte-identical native
  behavior (e.g. a fixed API quirk being reinterpreted as the closest achievable semantic, or a
  documented deviation).
- ❌-throws-by-design — intentionally unsupported here; throws a clear, specific exception rather
  than silently no-op'ing or producing wrong output. This is the deliberate house style for
  genuinely-3D-only entry points on this renderer (`ThrowNo3D`) and for 2D features that cannot be
  honored without misleading the caller (e.g. a custom shader `Effect`).
- ⛔ BLOCKED — a real architecture/behavior decision that was intentionally **not** guessed at;
  still awaiting a project-owner call. Covered in its own subsection below, not silently marked
  with any of the 3 statuses above.

---

## 1. SpriteBatch

| Feature | Status | Rationale |
|---|---|---|
| All 9 `Draw` overloads | ✅ | Audited and pixel-verified individually (Task 666); no renderer bug found. |
| `SpriteSortMode::Deferred` | ✅ | Preserves submission order, ignores `layerDepth` (Task 667). |
| `SpriteSortMode::Texture` | ✅ | Correct texture-group reordering + correct per-group texture binding (Task 668). |
| `SpriteSortMode::FrontToBack` / `BackToFront` | ✅ | Both pixel-verified (Task 669). |
| `SpriteSortMode::Immediate` | ✅ | Genuinely flushes per-draw, interleaves correctly with plain `GraphicsDevice` calls between draws in the same session (Task 670). |
| Rotation around `origin` | ✅ (fixed) | Real bug found and fixed: `SDL_RenderTextureRotated`'s pivot convention differs from XNA's; corrected by offsetting the destination rect so the pivot lands exactly on `destinationRectangle.X/Y` (Task 671). Fixes all 9 `Draw` overloads at once. |
| Scalar / `Vector2` scale overloads | ✅ | Both verified correct (Task 672). |
| Source-rectangle cropping | ✅ | Verified correct, genuinely renderer-specific (`srcrect` passthrough) (Task 673). |
| `SpriteEffects::FlipHorizontally`/`FlipVertically` | ✅ | Verified correct via `SDL_FlipMode` mapping (Task 674). |
| `transformMatrix` in `Begin()` | ✅ (fixed) | Real bug found and fixed: was completely silently ignored (shared no-op default, never overridden). Fixed via a new `SDL_RenderTextureAffine`-based draw path used only when the matrix is non-Identity; the existing rotation/flip/scale path is unchanged for the common Identity case (Task 675). |
| Custom `Effect` via `Begin(effect)` | ❌-throws-by-design | Decided: throw. SDL_Renderer has no programmable shader stage, so silently ignoring a custom `Effect` would misrender with no error. A `nullptr` effect (the default/common case) remains a no-op (Task 676). |
| `Begin`/`End` sequencing guards (double-`Begin`, `End`/`Draw` before `Begin`) | ✅ | Already correct — guards live entirely in shared, renderer-agnostic code (Task 677). |

## 2. Texture2D

| Feature | Status | Rationale |
|---|---|---|
| `SetData`/`GetData` full-array round-trip | ✅ | `GetData` is a pure CPU-side cache read (renderer-independent by construction); the real GPU texture SDL_Renderer creates also renders the correct pixels (Task 678). |
| `SetData` partial-rectangle region | ✅ | Real GPU texture correctly reflects a sub-rect write (full-buffer re-upload via `SDL_UpdateTexture`) (Task 679). |
| `SetData` `startIndex`/`elementCount` slice | ✅ | Slicing arithmetic is shared/renderer-independent; real GPU texture correctly reflects it (Task 680). |
| Mip-level (`level>0`) `SetData` | ❌-throws-by-design | Decided: throw. SDL_Renderer's blit pipeline has no native mip chain or per-level LOD sampling at all, so even storing per-level pixel data would be silently misleading — it could never actually be sampled. `level=0` `SetData`/`GetData` and `level>0` `GetData` (pure CPU cache read) are unaffected (Task 681). See [§11](#11-blocked-decisions--still-open) note: this is the resolved sibling of the still-open Task 725 `Texture3D`/`TextureCube` question — same shape of problem, different outcome because the blast radius here was small. |
| `FromStream` (PNG/JPEG/BMP/DDS) decode + render | ✅ | Same GPU-upload call site already proven correct by Task 678; verified again end-to-end (Task 682). |
| `SaveAsPng`/`SaveAsJpeg` round-trip | ✅ | Operates entirely on the CPU-side pixel cache, verified correct for a texture created/updated through the real renderer (Task 683). |
| NPOT (non-power-of-two) texture upload + sample | ✅ | Verified for both 3×5 and 7×11; `SDL_CreateTexture`/`SDL_UpdateTexture` abstract POT/NPOT handling away entirely (Task 684). |
| `TextureAddressMode::Clamp` (via `SpriteBatch`) | ⚠️-emulated | Produces the correct visual result, but not because any `SamplerState` is genuinely honored — `SDL_RenderTexture`'s fixed out-of-bounds `srcrect` handling happens to already match Clamp semantics. `SDL_SetRenderTextureAddressMode` exists but only affects `SDL_RenderGeometry`, which this renderer's `Draw()` never uses (Task 685). |
| `TextureAddressMode::Wrap` / `Mirror` (via `SpriteBatch`) | ⛔ BLOCKED | See [§11](#11-blocked-decisions--still-open) — Tasks 686/687. |
| `TextureFilter::Point` vs `Linear` (magnification) | ✅ (fixed) | Real bug found and fixed in Task 701's broader audit (see below); Task 688's own narrower check already confirmed the basic mapping direction. |
| `Texture2D::Dispose()` idempotency / shared-ownership safety | ✅ | Safe under both a normal and an ASan/UBSan build; correctly refcount-aware for copied `Texture2D` value-type instances (Task 689). |

## 3. SpriteFont

All of Tasks 690–693 are first-of-their-kind tests (their nominal EasyGL counterparts, Tasks
424–427, were themselves not yet implemented at the time), built on a hand-built minimal
glyph/cropping/kerning fixture since CNA has no XNB content pipeline.

| Feature | Status | Rationale |
|---|---|---|
| Single glyph at a known position/size | ✅ | Exact position and exact size both verified, not just "somewhere near" (Task 690). |
| Multiple glyphs with spacing/kerning | ✅ | Horizontal-advance math (`spacing` + per-glyph kerning bearings) verified correct (Task 691). |
| `\n` newline advance | ✅ | Advances by `lineSpacing_`, not glyph height — deliberately chose a fixture where these differ so this is a real discriminator (Task 692). |
| Unknown-character fallback (`defaultCharacter`) | ✅ | Falls back to the configured default glyph, doesn't throw, doesn't render the wrong glyph (Task 693). |
| `SpriteEffects` flip + rotation/origin/scale with `DrawString` | ✅ (fixed) | **Real bug found and fixed in SHARED code — affects every renderer, not just SDL_Renderer.** CNA's `DrawString` previously flipped each glyph's own texture sampling but never mirrored glyph *order/position* as a whole string, unlike FNA's real `axisDirection`/`axisIsMirrored` formula. Fixed in `SpriteBatch.cpp`; proven `effects=None`-neutral (algebraically and via a 62-test EasyGL spot-check) (Task 694). |

## 4. BlendState

| Feature | Status | Rationale |
|---|---|---|
| `Blend`/`BlendFunction` → `SDL_BlendFactor`/`SDL_BlendOperation` general mapping | ✅ (fixed — 2 real bugs) | **Bug 1:** the old mapping only special-cased `Opaque`/`Additive` and silently fell back to plain `SDL_BLENDMODE_BLEND` for everything else, making `AlphaBlend` (premultiplied) and `NonPremultiplied` (straight-alpha) indistinguishable. **Bug 2**, uncovered only once Bug 1 was fixed: `SpriteBatch::Begin()` unconditionally reset the blend mode to `SDL_BLENDMODE_BLEND` on every call, clobbering whatever `ApplyBlendState` had just set. Fixed by replacing the special-case mapping with a complete `SDL_ComposeCustomBlendMode`-based table and removing the clobbering reset (Task 695). |
| `BlendState::Opaque` | ✅ | Dedicated pixel test; fully overwrites destination, ignores alpha (Task 696). |
| `BlendState::AlphaBlend` (premultiplied) | ✅ | Dedicated pixel test with genuinely premultiplied source data; correct textbook blend (Task 697). |
| `BlendState::NonPremultiplied` (straight alpha) | ✅ | Dedicated pixel test; correct textbook blend (Task 698). |
| `BlendState::Additive`, incl. saturation/clamping | ✅ | Verified both non-saturating and saturating (clamps at 255, no wraparound) (Task 699). |
| Custom (non-preset) `BlendState` — supported factors/operations | ✅ | 10 of XNA's 13 `Blend` values and all 5 `BlendFunction` values have exact SDL equivalents and work correctly, confirmed with a genuine non-preset "modulate" blend and an explicit `Subtract` test (Task 700). |
| Custom `BlendState` — `Blend::BlendFactor`/`InverseBlendFactor`/`SourceAlphaSaturation` | ❌-throws-by-design | No SDL equivalent exists (constant-color blend factor / saturation term); throws rather than silently substituting a wrong factor (Task 700). |
| Custom `BlendState` — non-`Add` `BlendFunction` (`Subtract`/`RevSubtract`/`Min`/`Max`) | ⚠️-emulated | Confirmed working on this project's own OpenGL sandbox, but SDL3's own docs only formally guarantee `ADD`-with-all-factors support on the `opengl` driver (broader guarantees exist for `direct3d`/`direct3d11`/`opengles2`) — documented as a "works here, not universally guaranteed" caveat rather than a throw, since it does function correctly (Task 700). |

## 5. SamplerState

| Feature | Status | Rationale |
|---|---|---|
| `TextureFilter` → `SDL_ScaleMode` (all 9 values) | ✅ (fixed) | Real bug found and fixed: only `TextureFilter==Linear` (value 0) mapped to `SDL_SCALEMODE_LINEAR`; 4 of the other 8 values (all with a Linear *magnification* component — `Anisotropic`, `LinearMipPoint`, `MinPointMagLinearMipLinear`, `MinPointMagLinearMipPoint`) were silently downgraded to `SDL_SCALEMODE_NEAREST`. Fixed by switching on the magnification component specifically, since this renderer has no min/mag/mip distinction of its own (Task 701). |
| `TextureAddressMode` mapping | See Texture2D §2 | No new work needed in Task 701 itself — `Clamp` (⚠️-emulated, Task 685) and `Wrap`/`Mirror` (⛔ BLOCKED, Tasks 686/687) already cover this. |
| Default sampler state (`SpriteBatch::Begin()` with no explicit `SamplerState`) | ✅ | Confirmed byte-for-byte identical to an explicit `SamplerState::LinearClamp`, at 8 independent sample points (Task 702). |
| Per-draw sampler state switching across consecutive `Begin`/`End` cycles | ✅ | Confirmed switching works in *both* directions (not just Point→Linear, but also Linear→Point back), ruling out a "sticky at first value" bug (Task 703). |

## 6. RenderTarget2D

| Feature | Status | Rationale |
|---|---|---|
| Construction (2-arg and full 8-arg overloads) | ✅ (fixed) | **Real bug found and fixed in SHARED code (`GraphicsDevice.cpp`) — affects every renderer.** `SetRenderTarget`/`SetRenderTargets` unconditionally cleared a depth buffer on bind (matching FNA's `DiscardContents` default), even for `DepthFormat::None` targets — crashed immediately on SDL_Renderer (no depth buffer exists at all here) for any plain render target. Fixed by gating the depth-clear on the target actually requesting a depth format (Task 704). |
| Sampling a `RenderTarget2D` as `Texture2D` after unbinding | ✅ (fixed) | **Real memory-safety bug found and fixed** — undefined behavior (unchecked downcast between two unrelated sibling renderer classes), latent since inception, silently "working" only by ABI coincidence on this project's compiler; proven via a UBSan scratchpad build. Fixed by switching to the existing safe virtual accessors (Task 705). |
| `RenderTargetUsage::DiscardContents` vs `PreserveContents` | ✅ | Genuinely, observably different: `DiscardContents` auto-clears to black on every bind; `PreserveContents` leaves prior content untouched (Task 706). |
| `GetBackBufferData` after `SetRenderTarget(nullptr)` restores the backbuffer | ✅ | Confirmed correct at both the XNA `Viewport` level and the real renderer readback level; these are two genuinely independent "reverts to backbuffer size" mechanisms (Task 707). |
| Requested `DepthFormat` on a render target | ⚠️-emulated | Decision: silently ignore rather than throw. SDL_Renderer's sprite pipeline never depth-tests under any circumstance regardless of which target is bound, so a caller requesting a depth format purely for cross-renderer portability (without ever issuing a real depth-testing draw) hits no functional difference and shouldn't be penalized. `DepthStencilFormat` still echoes back whatever was requested (matches FNA's plain field-store semantics) even though no real storage is ever allocated. Fixing this decision also required a real shared-code bug fix: a new `IRenderTargetRenderer::HasRealDepthBuffer()` query, since Task 704's own fix incorrectly used the *requested* format rather than what was actually allocated (Task 708). |
| `SetRenderTargets` with 2+ bindings (MRT) | ❌-throws-by-design | Real bug found and fixed: previously silently bound only the first target and dropped the rest with no error. Now throws clearly for `count > 1`, matching this renderer's single-active-render-target reality (Task 709). This closes the RenderTarget2D section. |

## 7. Viewport / PresentationParameters / GraphicsDeviceManager

| Feature | Status | Rationale |
|---|---|---|
| `Viewport` get/set round-trip, incl. non-zero X/Y offset | ✅ | Pure renderer-agnostic math, confirmed tied correctly to the real, live device viewport (Task 710). |
| `Viewport::Project`/`Unproject` (2D orthographic case) | ✅ | Verified with a genuine 2D orthographic projection and a non-zero viewport offset — an previously-uncovered combination (Task 710). |
| Backbuffer resize via `PresentationParameters`/`Reset()` | ✅ | Works correctly; real window resize under X11 is asynchronous, so a same-frame readback right after `Reset()` correctly throws (a methodology finding fixed in the *test*, not production code) (Task 711). |
| Fullscreen toggle | ⚠️-emulated | `PresentationParameters.IsFullScreen` round-trips correctly and the toggle never throws, but the real OS-level fullscreen state is not independently verified and Xvfb likely does not honor fullscreen requests anyway (matches Task 902's precedent) (Task 712). |
| `PresentInterval` (vsync) mapping | ✅ (fixed) | Real bug found and fixed: `PresentInterval::Two` was silently collapsed to `One`, discarding its documented half-refresh-rate semantics. Now passed through to `SDL_SetRenderVSync` directly with a graceful fallback. Not independently pixel-observable on this sandbox's OpenGL driver, which rejects `vsync=2` outright regardless (Task 713). |
| `MultiSampleCount` | ⚠️-emulated | Decision: accept-and-ignore-with-log, mirroring the `DepthFormat` decision shape (Task 708). SDL_Renderer's 2D draws have no anti-aliasing seams to smooth in the first place; the shared default already correctly returned 0, now with an explicit diagnostic log for non-zero requests (Task 714). |
| `DeviceResetting`/`DeviceReset` events | ✅ | Fire exactly once each, in the correct order, with the correct OLD/NEW `PresentationParameters` state visible to each handler (Task 715). This closes the Viewport/PresentationParameters section. |

## 8. GraphicsDevice / resource lifecycle

| Feature | Status | Rationale |
|---|---|---|
| `Clear` — all 8 `ClearOptions` combinations | ✅ / ⚠️-emulated (`Stencil`) | `Target` alone and combinations without `DepthBuffer` behave correctly; any combination including `DepthBuffer` throws (no depth buffer exists at all — expected). `Stencil` is a confirmed, cross-renderer, already-tracked no-op (Task 871) — reconfirmed here, not fixed (out of this task's scope) (Task 716). |
| Disposed-resource guards | ✅ (fixed — 3 real bugs) | **Bug 1:** `SpriteBatch::pushSprite` had no disposed-texture guard at all — drawing a disposed `Texture2D` was a guaranteed null-dereference crash. **Bug 2:** `SetRenderTargets` (plural) was missing the disposed-check its singular sibling already had. **Bug 3:** `RenderTarget2D::Dispose()` left its cached `rtRenderer_` pointer dangling (use-after-free). All fixed in shared code; matches FNA's own genuinely narrow disposed-guard policy (guards at `GraphicsDevice` consumption points, not on every instance method) (Task 717). |
| Double-`Dispose()` safety | ✅ | Confirmed safe (idempotent) across `RenderTarget2D`/`BlendState`/`SamplerState`/`SpriteBatch`, including re-confirming Task 717's `rtRenderer_` fix is itself idempotent (Task 718). |
| Resource leak-check (`Texture2D`/`RenderTarget2D`, 80 create/dispose cycles) | ✅ | `GetTrackedResourceCount`/`HasRenderer()` both return cleanly to baseline; mirrors Task 219's existing EasyGL leak-check (Task 719). This closes the GraphicsDevice-lifecycle section. |

## 9. Systematic "3D throws correctly" sweep

Every one of these is intentionally unsupported on this 2D-only renderer; the audit's job was
confirming each throws the *correct, specific* exception rather than a generic one, or crashing.

| Feature | Status | Rationale |
|---|---|---|
| `DrawPrimitives`/`DrawIndexedPrimitives`/`DrawInstancedPrimitives` | ❌-throws-by-design | Since `VertexBuffer`/`IndexBuffer` construction itself throws on this renderer, a valid instance can never exist bound — these always throw at the shared `currentVertexBuffer_ == nullptr` check first, not SDL_Renderer's own draw-level `ThrowNo3D` (Task 720). |
| `DrawUserPrimitives` (5 typed + `VertexDeclaration` overloads) | ❌-throws-by-design | Two independently-reachable paths per overload verified: shared "no effect applied" when no `Effect` is set; SDL_Renderer's own "does not support 3D: CreateVertexBuffer" once one is (Task 721). |
| `DrawUserIndexedPrimitives` (10 overloads) | ❌-throws-by-design | Same two-path shape as Task 721; confirmed `CreateVertexBuffer` always throws before `CreateIndexBuffer16`/`32` is ever reached, for both 16-bit and 32-bit index overloads alike (Task 722). |
| `VertexBuffer`/`IndexBuffer`/Dynamic variants — all 8 construction overloads | ❌-throws-by-design | Re-verified with Task-261-style rigor; confirmed `CreateIndexBuffer32` isn't separately overridden (delegates to `CreateIndexBuffer16`, identical message for both widths) and the `dynamic` constructor flag is completely unused; throw fires unconditionally, before any argument validation (Task 723). |
| `VertexDeclaration` construction | ✅ | Does **not** throw — pure data, never tied to any `GraphicsDevice`/renderer at all, on any renderer. A draw call using such a declaration still throws, but only at the draw (Task 724). |
| `Texture3D`/`TextureCube` construction | ⛔ BLOCKED | See [§11](#11-blocked-decisions--still-open) — Task 725. |
| 5 stock 3D effects (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`) — construction/setters/`Apply()` | ✅ | Do **not** throw — matches FNA's "renderer-agnostic data object until actually used to draw" model. Confirmed actually drawing with each still throws via the `DrawUserPrimitives` path (Task 721) (Task 726). |
| `OcclusionQuery` construction | ❌-throws-by-design (fixed) | **Real bug found and fixed.** `CreateOcclusionQuery` was never overridden, so construction silently succeeded with a permanently-null renderer and `Begin()`/`End()` silently no-op'd. Fixed by adding the missing `ThrowNo3D` override — safe since this had zero existing SDL_Renderer test coverage (unlike Task 725's situation) (Task 727). |
| `Model::Draw` | ❌-throws-by-design | Throws via the same shared `DrawIndexedPrimitives` "no vertex buffer bound" path Task 720 established; confirmed zero prior SDL_Renderer test coverage of `Model::Draw` existed, so this was safe new coverage (Task 728). |
| `RasterizerState`/`DepthStencilState` construction/assignment | ✅ | Do **not** throw — `ApplyRasterizerState`/`ApplyDepthStencilState`'s shared default is an *explicitly documented, deliberate* no-op in `IGraphicsRenderer.hpp`, unlike Task 725's accidental-omission situation. The assigned state is still genuinely stored and reported back correctly (pure data) even though GPU-side application is a no-op here (Task 729). This closes the entire 720–729 3D-drawing-call exception-behavior range. |

## 10. Compatibility proof (Task 730)

Real end-to-end compatibility proof via genuine multi-frame `Update()`+`Draw()` sample programs,
not isolated API/exception checks:

| Sample | Status | Notes |
|---|---|---|
| `cna_demo_2d` (existing cross-renderer 2D sprite demo) | ✅ (fixed) | Already had `Vulkan_Demo2D_SmokeTest`/`Bgfx_Demo2D_SmokeTest` (Tasks 88/89) but no SDL_Renderer registration despite running correctly — closed via new `SDL_Renderer_Demo2D_SmokeTest`. |
| Bouncing-sprite physics | ✅ | New minimal sample; 5-frame bounce cycle with an independently-computed expected position, verified via pixel readback. |
| Keyboard-driven sprite | ✅ | New minimal sample; uses the established `InputManager::SetKeyState` headless-input-injection seam. |
| Two-glyph `SpriteFont::DrawString` text | ✅ | New minimal sample; proves multi-glyph kerning-driven layout, not just one enlarged/overlapping glyph. |
| Animated spritesheet (`Update()`-driven `sourceRectangle` frame selection) | ✅ | New minimal sample; confirms the *actually-drawn* sprite (not the source texture) shows the correct frame. |

No bug found in this task beyond the closed `cna_demo_2d` registration gap. All 5 samples registered
as SDL_Renderer `ctest` entries and passing.

---

## 11. BLOCKED decisions — still open

Two architecture/behavior decisions were deliberately **not** guessed at and remain open pending a
project-owner call. Both are documented in full in `plan_graphics.md` (rows 686/687 and 725) and in
`NEXT.md` §5 ("Known bugs and limitations").

### Tasks 686/687 — `TextureAddressMode::Wrap`/`Mirror` via `SpriteBatch`

**Current state**: `SdlSpriteBatchRenderer::Draw` uses `SDL_RenderTexture`/`SDL_RenderTextureRotated`/
`SDL_RenderTextureAffine`, whose `srcrect` out-of-bounds handling has one fixed (Clamp-like) edge
behavior regardless of the requested `SamplerState`. SDL3's real `SDL_SetRenderTextureAddressMode`
API exists but only affects `SDL_RenderGeometry` calls, which this renderer's `Draw()` path never
uses — confirmed empirically, not just from documentation (Task 685). A genuine native fix needs
`Draw()` rewritten to build explicit `SDL_Vertex`/UV geometry and use `SDL_RenderGeometry` instead.

**Options on the table** (none picked):
- **(a)** Throw unconditionally whenever a Wrap/Mirror `SamplerState` is requested via
  `SpriteBatch::Begin`. Small, safe — but risks breaking a game that passes Wrap/Mirror
  defensively without any draw ever actually sampling past the texture's edge (Wrap vs. Clamp only
  differs then).
- **(b)** Rewrite `SdlSpriteBatchRenderer::Draw` to use `SDL_RenderGeometry`. A real, behaviorally
  correct fix (and would unlock `Mirror` in the same pass) — but a materially larger, riskier
  change touching code already extensively verified correct across Tasks 671–675/685 (rotation,
  flip, scale, source-rect cropping, transformMatrix, origin placement).
- **(c)** A hybrid: only throw when a specific `Draw()` call's `sourceRectangle` actually exceeds
  the texture's own bounds (the only case where Wrap/Mirror vs. Clamp can ever visibly differ),
  passing through unchanged otherwise.

### Task 725 — `Texture3D`/`TextureCube` construction

**Current state**: `SdlRenderer` never overrides `CreateTexture3D`/`CreateTextureCube`, so
both fall through to `IGraphicsRenderer`'s default (`return nullptr;`, no throw). Construction
currently succeeds silently with a permanently-null renderer; `SetData`/`GetData` both silently
no-op instead of throwing. Prototyping "throw at construction" (mirroring the `VertexBuffer`/
`IndexBuffer`/`ThrowNo3D` pattern, Tasks 720–723) revealed a **94-test blast radius**: 33 tests in
`Texture3DTests.cpp`, 42 in `TextureCubeTests.cpp`, 19 in `Texture3DTextureCubeRenderTargetTests.cpp`
construct these types directly with zero `CNA_RENDERER_SDL_RENDERER`/`GTEST_SKIP` guards, and all
currently pass on SDL_Renderer precisely *because* construction is silent today. The alternative
("throw on `SetData`/sampling instead") isn't blast-radius-free either —
`Texture3DTest.SetDataExactElementCountDoesNotThrow` (and likely siblings) explicitly expects
`SetData` to succeed.

**Options on the table** (none picked):
- **(a)** Throw at construction — most consistent with the `VertexBuffer`/`IndexBuffer` precedent,
  but needs an audit-and-guard pass across most of those 94 existing tests.
- **(b)** Throw on `SetData`/`GetData`/actual-draw instead — smaller but still non-zero blast
  radius; leaves construction itself silently misleading in the meantime.
- **(c)** Accept the current silent-no-op behavior as a documented 2D-only-renderer limitation —
  zero test changes needed, but matches FNA's own behavior model least well (FNA never silently
  constructs a dead-on-arrival texture).

**Contrast with Task 681** (mip-level `Texture2D::SetData`, resolved to throw): that decision had a
*small* blast radius (no existing tests exercised `level>0` `SetData` on SDL_Renderer at all), so
"throw" was safe to ship immediately. Task 725 has the same *shape* of problem but a much larger
blast radius, which is precisely why it remains BLOCKED rather than resolved the same way.

---

## Summary: what actually works today

| Area | Status |
|---|---|
| SpriteBatch (all draw variants, sorting, rotation/flip/scale/crop, transform matrix) | ✅ fully verified, 2 real bugs found and fixed |
| Custom shader `Effect` via `SpriteBatch::Begin` | ❌ throws by design (no shader stage exists) |
| Texture2D (SetData/GetData, FromStream, Save*, NPOT, dispose) | ✅ fully verified, mip-level `SetData` throws by design |
| TextureAddressMode | Clamp ⚠️ emulated · Wrap/Mirror ⛔ BLOCKED |
| SpriteFont (glyph placement, spacing, newline, fallback, flip) | ✅ fully verified, 1 real cross-renderer bug found and fixed |
| BlendState (all 4 presets + custom combinations) | ✅ fully verified, 2 real bugs found and fixed |
| SamplerState (filter mapping, defaults, per-draw switching) | ✅ fully verified, 1 real bug found and fixed |
| RenderTarget2D (construction, sampling, usage modes, MRT) | ✅ fully verified, 4 real bugs found and fixed; DepthFormat ⚠️ emulated, MRT ❌ throws by design |
| Viewport/PresentationParameters/GraphicsDeviceManager | ✅ fully verified, 1 real bug found and fixed; fullscreen/MultiSampleCount ⚠️ emulated |
| GraphicsDevice lifecycle (Clear, disposed guards, double-dispose, leaks) | ✅ fully verified, 3 real bugs found and fixed (disposed guards) |
| 3D-only entry points (draw calls, buffers, OcclusionQuery, Model::Draw) | ❌ throw by design (1 real gap found and fixed: OcclusionQuery) |
| Stock 3D effects (construction/setters/Apply, not drawing) | ✅ don't throw, matching FNA's renderer-agnostic-until-drawn model |
| RasterizerState/DepthStencilState | ✅ don't throw (documented, deliberate no-op) |
| 5 real compatibility-proof samples (Demo2D + 4 new) | ✅ all pass |
| Texture3D/TextureCube construction | ⛔ BLOCKED — project-owner decision needed |

**Total real bugs found and fixed across the whole audit (Tasks 666–730): 15** (2 in SpriteBatch
rotation/transform, 1 cross-renderer in SpriteFont, 2 in BlendState, 1 in SamplerState, 4 in
RenderTarget2D, 1 in Viewport/PresentInterval, 3 in disposed-resource guards, 1 in OcclusionQuery),
plus 1 mip-level decision resolved (Task 681) and 2 decisions still BLOCKED (Tasks 686/687, 725).
