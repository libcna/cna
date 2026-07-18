# Audit: examples/sdlrenderer_stock3deffects_apply_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_stock3deffects_apply_test.cpp` (216 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — stock 3D `Effect`/`Apply()` no-throw-until-draw contract test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_sdl_stock3deffects_apply` /
  `SDL_Renderer_Stock3DEffectsApply`, `cmake/Tests/SdlRendererTests.cmake:393-395`), introduced by
  `ed29a2dd`/`384d8347` ("test(Task 726): verify stock 3D effects Apply()+setters never throw on SDL_Renderer").
- XNA/FNA relevance: direct — `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`,
  `SkinnedEffect` construction/property-setter/`Apply()` semantics; `GraphicsDevice.DrawUserPrimitives`'s
  3D-unsupported throw contract on this backend.
- FNA reference: general XNA `Effect.Apply()` semantics — effects are backend/data objects whose properties can
  always be set and applied regardless of whether the device can actually rasterize with them; only an actual
  draw call requires real GPU 3D capability.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/{BasicEffect,AlphaTestEffect,DualTextureEffect,
  EnvironmentMapEffect,SkinnedEffect}.cpp` (each `OnApply()`), `src/Microsoft/Xna/Framework/Graphics/Effect.cpp`
  (`Apply()`, lines 53-59), `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`DrawUserPrimitives`
  `VertexPositionColor` overload, lines 869-892), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`CreateVertexBuffer`, lines 795-798, throwing via `ThrowNo3D`).

## Purpose

`SdlStock3DEffectsApplyTest` (lines 45-207) exercises all 5 stock 3D effects identically: construct, set a
representative sample of each effect's own properties (including assigning a real `Texture2D`), call `Apply()`,
and assert none of that throws — then, for a *second*, freshly-constructed instance of the same effect type,
`Apply()` followed by an actual `DrawUserPrimitives` call, asserting that call *does* throw. A final check
confirms the device is still fully usable (`Clear`+`GetBackBufferData`) after all 10 sub-checks. The rationale
(stated in the header): each effect's `OnApply()` only ever touches `Texture2D::GetBackend()` for an *assigned*
texture, guarded by a null/enabled check, and `Texture2D` itself works fine on SDL_Renderer — so property setters
and `Apply()` can never reach a 3D-unsupported code path regardless of whether a texture is assigned.

## Executive Verdict

**Healthy.** Every specific factual claim in the header comment was independently traced through the actual
production source (all 5 effects' `OnApply()` bodies, `Effect::Apply()`, the `DrawUserPrimitives` call chain down
to `SdlGraphicsBackend::CreateVertexBuffer`) and confirmed accurate; the test genuinely proves the "effects are
backend-agnostic until a real draw" contract rather than merely "doesn't crash."

## Checklist Results

### API / XNA / FNA parity

Every property setter this test calls was confirmed to exist with the expected signature: `setWorldProperty`/
`setViewProperty`/`setProjectionProperty`/`setDiffuseColorProperty`/`setAlphaProperty`/`setLightingEnabledProperty`
/`setPreferPerPixelLightingProperty`/`setTextureEnabledProperty`/`setTextureProperty` (`BasicEffect.hpp`);
`setVertexColorEnabledProperty`/`setAlphaFunctionProperty`/`setReferenceAlphaProperty` (`AlphaTestEffect.hpp`);
`setTexture2Property`/`setVertexColorEnabledProperty` (`DualTextureEffect.hpp`); `setEnvironmentMapAmountProperty`
/`setFresnelFactorProperty` (`EnvironmentMapEffect.hpp`); `setWeightsPerVertexProperty` (`SkinnedEffect.hpp`) — all
confirmed present via direct header inspection, matching this project's established `getX/setX` property
convention and the XNA property names each maps to.

### Behavioral correctness

Traced `Effect::Apply()` (`Effect.cpp:53-59`): `if (isDisposed_) throw ...; OnApply(); if (device_) device_->
SetCurrentEffect(this);` — confirmed `OnApply()` runs before `SetCurrentEffect`, and every one of the 5 effects'
`OnApply()` bodies were individually inspected: `BasicEffect::OnApply()` (`BasicEffect.cpp:46-49`) is a pure no-op
(its own comment notes `SetCurrentEffect` was moved to `Effect::Apply()`); `AlphaTestEffect`, `DualTextureEffect`,
`EnvironmentMapEffect`, `SkinnedEffect`'s `OnApply()`s each only conditionally touch `texture_->GetBackend()` /
`texture2_->GetBackend()` / `environmentMap_->GetBackend()` guarded by a null check (confirmed at
`DualTextureEffect.cpp:257-258`, `AlphaTestEffect.cpp:322`, `EnvironmentMapEffect.cpp:407-408`,
`SkinnedEffect.cpp:328`) — none of these touch `GraphicsDevice::GetBackend()` or any 3D-only backend entry point
directly, confirming the header comment's central claim. `Texture2D::CreateFromPixels` (line 79) and
`Texture2D::GetBackend()` are both confirmed to work on SDL_Renderer (Texture2D is fully supported per the
`SdlGraphicsBackend.cpp` audit), so assigning a real texture to each effect cannot itself introduce a throw.

For the "drawing does throw" half: traced `dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpc, 0, 1)`
(the `VertexPositionColor` overload, `GraphicsDevice.cpp:869-892`) — confirmed `backend_->CreateVertexBuffer(n)`
(line 885) is called *before* `FillGpuDrawParams`/`ExtractMatrices`/`DrawPrimitivesEx`, and
`SdlGraphicsBackend::CreateVertexBuffer` (`SdlGraphicsBackend.cpp:795-798`) unconditionally calls the shared
`ThrowNo3D()` helper (`std::runtime_error`, a `std::exception` subclass) — so every one of the 5
`Throws([&]{ tryDraw(fx2); })` checks is guaranteed to throw at the identical, very first backend call, regardless
of which specific effect is bound as `currentEffect_`. This independently confirms the "drawing after Apply()
still throws" half of the test for all 5 effects via one common, correctly-identified choke point.

### Logic

`DoesNotThrow`/`Throws` (lines 58-70) are minimal, correct template helpers (`catch (const std::exception&)`);
since `ThrowNo3D` throws `std::runtime_error` (a `std::exception` subclass), both helpers correctly observe it.

### Memory/resource lifetime

`tex` (`Texture2D::CreateFromPixels`, line 79) is a single stack-local `Texture2D` value shared (by reference, via
`fx.setTextureProperty(&tex)` etc.) across all 5 effects' first (non-throwing) sub-block — each effect only stores
a raw, non-owning pointer to it (consistent with the `Texture2D*` setter signatures observed above), and `tex`
outlives every one of those uses within the same `Draw()` call — no dangling-pointer risk.

### C++ correctness

`static const VertexPositionColor vpc[3]` (lines 80-82) with all-zero positions is fine since `DrawUserPrimitives`
is expected to throw before ever reading vertex data (confirmed above: the throw happens at
`backend_->CreateVertexBuffer`, before the vertex array is even touched) — a degenerate all-coincident-vertex
triangle is an acceptable input specifically because this test never expects it to actually rasterize.

### Performance / Thread safety

N/A — single-frame diagnostic executable.

### Architecture

Clean and correctly generalized: `tryDraw` (line 84) is a single shared lambda reused identically for all 5
effects' throw-check, and `check()` centralizes PASS/FAIL bookkeeping/printing — good avoidance of the
copy-pasted-with-drift risk that could otherwise creep into 5 near-identical blocks.

### Maintainability

216 lines covering 5 effects × 2 sub-checks each + 1 final device-health check = 11 assertions; the
`std::setvbuf(stdout, nullptr, _IONBF, 0)` (line 212) is a deliberate, explained (via comment) choice to ensure
partial PASS/FAIL output survives a hypothetical mid-test crash — a good diagnostic practice consistent with
this shard's general engineering discipline.

### Portability

No `PresentationMode::NativeBackBuffer` requirement here (unlike the SpriteFont/texture-sort/dispose tests in this
batch) — correctly so, since this test never does a geometrically-precise pixel readback; its one
`GetBackBufferData` check (lines 186-190) only asserts "roughly full-brightness cyan at the viewport centre,"
which tolerates letterbox/scaling coordinate differences the other files in this batch specifically had to guard
against.

### Robustness

Every one of the 10 effect-related sub-checks and the final device-health check independently contributes to
`pass_`/`fail_` (not aggregated into a single boolean), and the summary line (`=== %d/%d PASS ===`, line 193) gives
a clear pass-count signal even under partial failure — a slightly more granular pattern than the
single-`result_`-flag approach used by the other 7 files in this batch, appropriate given this file asserts many
more independent behaviors (10 checks across 5 effect types) than the geometry-focused sibling files.

### Testing

This file is itself a test. It is a genuinely strong, single-purpose regression guard for a specific
architectural contract ("effects never touch 3D backend state until an actual draw call") across all 5 stock 3D
effects uniformly, with every specific factual claim in its own header independently confirmed against production
code by this audit.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `EnvironmentMapEffect`'s "no-throw" sub-check does not assign `EnvironmentMap` at all, so it cannot confirm `Texture2D::getBackend()`-style access for that specific property is equally safe as the assigned case

- Severity: INFO
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `fx.setEnvironmentMapProperty(nullptr)` (line 155)
- Evidence: the file's own header comment (lines 14-18) explicitly and correctly explains this is deliberate:
  constructing a real `TextureCube` on SDL_Renderer is a separate, currently-BLOCKED architectural gap (Task 725,
  independently corroborated by this audit's read of the `SdlGraphicsBackend.cpp` audit report's own F2 finding),
  so this test only needs to confirm the plain-pointer-store setter itself doesn't throw, which it does not
  require ever constructing a `TextureCube`.
- Why it matters: purely a scope note, not a defect — flagged here only to record that the "EnvironmentMapEffect
  never touches anything unsupported" claim is proven for the "map unset" case only, consistent with (and
  explicitly limited by) the still-open Task 725 blocker, not something this test could have done differently
  without that blocker being resolved first.
- Suggested future action (not implemented by this audit): none until Task 725 unblocks `TextureCube` construction
  on this backend; then this test could be extended to also cover an *assigned* `EnvironmentMap`.

## Cross-File Observations

- Directly corroborates the `SdlGraphicsBackend.cpp` audit's own F2 finding (Texture3D/TextureCube construction
  currently succeeding silently rather than throwing, Task 725, BLOCKED) by structuring around it rather than
  triggering it — this test's careful avoidance of ever constructing a `TextureCube` is itself evidence the test
  author was aware of that gap.
- The single shared choke point this test relies on (`SdlGraphicsBackend::CreateVertexBuffer` throwing before any
  effect-specific code runs) means this test would still pass even if a *specific* effect's `FillGpuDrawParams`
  had its own latent bug — that method is never reached before the throw. This is fine for this test's own
  narrowly-stated purpose (proving Apply() itself is 3D-backend-safe), but worth noting for anyone reading this
  file expecting it to also validate `FillGpuDrawParams`'s per-effect parameter-filling correctness (it does not,
  and does not claim to).

## Missing or Weak Tests

See F1 (deliberate, blocked-on-Task-725 scope limit, not an oversight).

## Positive Findings

- Every specific technical claim in the file's own header comment (each effect's `OnApply()` only conditionally
  touches `Texture2D::GetBackend()`, never `GraphicsDevice::GetBackend()` directly; drawing throws via the
  established Task 721 3D-unsupported path) was independently traced and confirmed correct against the real
  production source across 5 separate effect implementations plus the shared `GraphicsDevice`/backend call chain.
- Good re-use of shared helper templates (`DoesNotThrow`/`Throws`/`tryDraw`) across all 5 effects, avoiding
  copy-paste drift.
- The final "device remains fully functional" check is a valuable, non-obvious addition — confirms none of the
  10 preceding throw/no-throw exercises left the `GraphicsDevice`/backend in a corrupted state.

## Final Assessment

A precise, well-generalized regression test whose every factual claim was independently confirmed against
production code across all 5 stock 3D effects and the shared draw-call throw path; no defects found in either the
test or the code it exercises.
