# Audit: examples/easygl_sampler_state_effect_test.cpp

## Metadata

- Source file: `examples/easygl_sampler_state_effect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard (also reused, unmodified, as a Vulkan test source — see Cross-File
  Observations) — `GraphicsDevice.SamplerStates[]` honored by a stock 3D effect draw
- File type: C++ example/integration-test executable (`SamplerStateEffectTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::GraphicsDevice::applySamplerStatesToBackend`
  (`GraphicsDevice.cpp:1592-1604`), `CNA::Internal::Backends::EasyGL::EasyGLGraphicsBackend::ApplySamplerState`
  (`EasyGLGraphicsBackend.cpp:2055-2139`), `DualTextureEffect::FillGpuDrawParams` (`DualTextureEffect.cpp:248-275`)
- XNA/FNA relevance: `GraphicsDevice.SamplerStates[0]`, `SamplerState.PointWrap`, `TextureAddressMode.Wrap` vs.
  `.Clamp` — judged against FNA's `SamplerStateCollection`/`SamplerState` semantics (per-slot state, applied at
  draw time, not at assignment time).
- Main related tests: this file (Task 293); reused verbatim by `cmake/Tests/VulkanTests.cmake` (Task 293's Vulkan
  counterpart, `Vulkan_SamplerState_DualTextureEffect`) since it only exercises public XNA API.

## Purpose

Verifies that `GraphicsDevice.SamplerStates[0]` is actually *honored* by a stock 3D effect draw (`DualTextureEffect`),
not merely stored inertly. A 2x1 pattern texture (left texel Red, right texel Green) is sampled with a UV range of
`[0,2]` (i.e. the texture repeated twice); the readback point (destination x-fraction 0.625, raw `u=1.25`) is chosen
specifically so that `TextureAddressMode::Wrap` (`1.25 mod 1 = 0.25` → left/Red half) and `::Clamp` (`u` clamps to
`1.0` → right/Green edge) produce *different, distinguishable* colors — turning "was the sampler state applied at
all" into a simple Red-vs-Green readback rather than a subtler numeric comparison. Placement matches
`examples-tests-easygl`.

## Executive Verdict

**Healthy.** The sampler-state-is-applied-per-draw-call architecture, the slot-0-to-texture-unit-0 binding, and the
DualTexture doubling shader math were all independently traced and confirmed consistent with the test's own stated
expectations; no correctness defects found.

## Checklist Results

### API / XNA / FNA parity
`device.getSamplerStatesProperty()[0] = SamplerState::PointWrap` (line 76) uses `SamplerStateCollection::
operator[]`, which returns a mutable `SamplerState&` (`SamplerStateCollection.cpp:13`) — a plain assignment into
the collection's backing array, not an immediate apply-to-backend call. Confirmed `PointWrap`'s actual definition
(`SamplerState.cpp:11`): `Filter=Point, AddressU=AddressV=AddressW=Wrap` — correct values for what the test's
comment claims.

### Behavioral correctness
Traced how a `SamplerStateCollection` mutation ever reaches the GPU: `GraphicsDevice::applySamplerStatesToBackend()`
(`GraphicsDevice.cpp:1592-1604`) iterates all `MaxSamplers` slots and calls `backend_->ApplySamplerState(i, ...)`
for each — confirmed this is called from *every* `Draw*` overload in `GraphicsDevice.cpp` (16 call sites found via
grep, e.g. lines 583, 625, 683, 745, 811, 890...), meaning sampler state is correctly re-applied fresh before each
draw call, not just once at some earlier point — validating the test's implicit assumption that setting
`SamplerStates[0]` before `DrawUserPrimitives()` (called after the assignment, line 98) is sufficient.

Confirmed `EasyGLGraphicsBackend::ApplySamplerState(slot, filter, addressU, addressV, maxAnisotropy)`
(lines 2055-2139) creates/configures a GL sampler object per slot and calls `s.bind(slot)` (line 2138) — i.e. slot
0's `PointWrap` configuration is bound specifically to GL texture unit 0, which is exactly where `patternTex_`
(`fx.setTextureProperty(&patternTex_)`, bound as `uTexture`) is sampled from — confirmed via
`DualTextureEffect::FillGpuDrawParams` (`texture0 = &texture_->GetBackend()`) and the EasyGL 3D-draw texture-binding
code (`EasyGLGraphicsBackend.cpp:4167-4178`, which explicitly binds `texture1`/unit-1 *before* returning to unit 0 as
the "active" unit, leaving `patternTex_` correctly associated with unit 0/slot 0's sampler state).

`whiteTex_` (bound as `Texture2`/unit 1) is a 1x1 solid white texture, so its sampler state (left at whatever
default `SamplerStateCollection` slot 1 holds, untouched by this test) cannot affect the outcome regardless of
address mode — a single-texel texture samples identically under Wrap/Clamp/Mirror. This correctly isolates the
test to slot 0's `PointWrap` assignment, exactly as the header comment (lines 6-7) claims.

Confirmed `RasterizerState::CullNone` (line 97) is required given the quad's stated winding (comment, lines 95-96)
— consistent with the same "Task 896 finding" pattern used by every full-screen-quad test in this shard.

### Logic
`isRed`/`isGreen` (lines 106-107) use asymmetric-but-sound thresholds (`>=200`/`<=50` for the dominant/suppressed
channel respectively) — wide enough to tolerate the `DualTextureEffect` shader's `base.rgb *= 2.0` doubling
(`EasyGLGraphicsBackend.cpp:3051`, confirmed to match FNA's own `PSDualTexture` doubling) clamping fully-saturated
channel values back to 1.0, without needing the test itself to reason about the doubling.

### Memory/resource lifetime
`patternTex_`/`whiteTex_` are `Texture2D` *value* members (not `unique_ptr`), default-constructed at class scope
then reassigned via `Texture2D::CreateFromPixels(...)` in `Initialize()` — confirmed `Texture2D` has a default
constructor (`Texture2D.hpp:34`) and defaulted copy/move assignment (lines 70-73), so this reassignment pattern is
well-formed and not UB; `Texture2D`'s value-type-with-shared-backend-ownership design (implied by its being
copyable) means the temporary returned by `CreateFromPixels` is safely absorbed by move-assignment.

### C++ correctness
`static_cast<int>(alpha)` equivalent not needed here (unlike sibling files) — `pattern`/`white` are `vector<uint8_t>`
built directly with byte literals; no narrowing-conversion risk in this file.

### Performance
N/A — single-frame, tiny-texture test.

### Robustness
No malformed-input path; deterministic single-draw sequence.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL/MEDIUM/LOW findings — this file's design and every dependency it relies on (per-draw-call sampler
re-application, slot→unit binding, single-texel isolation of the second texture) were independently confirmed
correct.

## Cross-File Observations

- This file's source is deliberately **reused unmodified** as a Vulkan backend test
  (`cmake/Tests/VulkanTests.cmake:53-58`, `cna_test_vulkan_sampler_state_effect`), with an explicit comment there
  ("reuses the backend-agnostic EasyGL test source (public XNA API only)") — confirmed this file indeed only calls
  public `Microsoft::Xna::Framework::Graphics`/`Game` API, no EasyGL-specific symbol, making the reuse legitimate.
  Worth noting for maintainability: despite the `easygl_` filename prefix, this file is compiled into (at least) two
  separate backend test binaries — a reader searching by filename alone could miss that it's also the Vulkan
  regression test for the same behavior.
- Shares the "Task 896 finding" `RasterizerState::CullNone` requirement with essentially every other full-screen
  triangle-list quad test in this shard.

## Missing or Weak Tests

- No case exercises `SamplerStates[1]` (the second texture unit) with a non-trivial (non-1x1) texture to prove the
  per-slot independence this test's own design relies on (i.e. that slot 0 and slot 1 truly have independent
  `TextureAddressMode` state, not a single global one) — this test only ever proves slot 0 is honored, using slot
  1's irrelevance-by-construction (1x1 texture) as a convenience rather than proving slot-1 sampler state itself
  is independently applied.
- No case tests `SamplerState::Clamp` explicitly (only `PointWrap` is assigned; the `Clamp` branch of the
  Wrap-vs-Clamp comparison is only ever the implicit "if this test fails" fallback path, never itself the assigned/
  expected-passing state) — a companion test asserting `Clamp` correctly produces Green would close the loop
  symmetrically (a sibling `easygl_texture_address_mode_clamp_effect_test.cpp` was found referenced in
  `VulkanTests.cmake` and likely fills this role, though it is out of this batch's scope to confirm in full).

## Positive Findings

- A well-designed isolation test: the choice of a 2x1 repeat pattern plus a precise fractional readback point
  (`u=1.25`) turns "is the sampler state applied" into a simple, unambiguous Red-vs-Green binary outcome rather
  than a numeric tolerance comparison — a robust test design.
- Correctly reused, byte-for-byte, as a second backend's regression test via the CMake build system rather than
  being duplicated — genuine backend-agnostic test design in practice, not just in principle.

## Final Assessment

A well-constructed, correctly-isolated sampler-state regression test whose every underlying assumption (per-draw
reapplication, slot-to-unit binding, single-texel second-texture irrelevance) was independently confirmed against
the real `GraphicsDevice`/EasyGL backend source.
