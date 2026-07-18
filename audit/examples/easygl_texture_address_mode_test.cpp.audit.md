# Audit: examples/easygl_texture_address_mode_test.cpp

## Metadata

- Source file: `examples/easygl_texture_address_mode_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `SpriteBatch` `TextureAddressMode` pixel test
- File type: C++ example/integration-test executable (`TextureAddressModeTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::SpriteBatch::Begin`
  (`SpriteBatch.cpp:119-121`, forwards `SamplerState` to the backend),
  `CNA::Internal::Backends::EasyGL::EasyGLSpriteBatchBackend::SetSamplerFilter`/`SetSamplerAddressMode`
  (`EasyGLGraphicsBackend.cpp:1065-1074`) and `EasyGLGraphicsBackend::ApplySamplerState`
  (`EasyGLGraphicsBackend.cpp:2055-2139`)
- XNA/FNA relevance: `SpriteBatch.Begin(SpriteSortMode, BlendState, SamplerState, ...)`,
  `SamplerState.PointWrap`/`PointClamp`, `TextureAddressMode.Wrap`/`Clamp`. Judged against
  `FNA/src/Graphics/SpriteBatch.cs` (sampler state applied at `Begin`) and
  `FNA/src/Graphics/States/SamplerState.cs` (static presets).
- Main related tests: this file (Task 269 — the original SpriteBatch sampler-state fix and its proof); three
  sibling files in this batch extend the same code path to Mirror (`_mirror_test.cpp`) and to
  `DrawUserPrimitives`+`Effect` instead of `SpriteBatch` (`_clamp_effect_test.cpp`, `_mirror_effect_test.cpp`).

## Purpose

Verifies that `SpriteBatch::Begin`'s `SamplerState` argument (`Filter`/`AddressU`/`AddressV`) genuinely
affects EasyGL rendering by drawing a 2×1 (Red|Blue) texture with a `sourceRectangle` twice the texture's
width (so sampled U spans `[0,2]`, the classic XNA tiling/scrolling technique) under both `PointWrap` and
`PointClamp`, and reading back a single pixel at `U=1.25` where the two modes disagree. Placement matches the
shard convention.

## Executive Verdict

**Healthy** — the U=1.25 sample point and both expected colors were independently re-derived from first
principles (texel-center geometry under point filtering) and confirmed correct; traced the full
`SpriteBatch::Begin → SetSamplerFilter/SetSamplerAddressMode → FlushBatch → ApplySamplerState` call chain in
production code and confirmed slot 0 is genuinely bound with the requested sampler before the draw, so this
test is a real regression guard for the Task 269 fix, not a test that would pass regardless of whether the
sampler state is honored. One real, shared coverage gap across this file and its three siblings (V-axis
addressing is set but never exercised) is worth tracking.

## Checklist Results

### API / XNA / FNA parity
`SpriteBatch::Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, sampler, nullptr, nullptr)` (line 83) and
`SamplerState::PointWrap`/`PointClamp` (lines 85-86) are real XNA members. `Texture2D::CreateFromPixels` (line
54, 69) is a `NOXNA` helper (per `Texture2D.hpp`), correctly not presented as an XNA member. `Draw(texture,
destinationRectangle, sourceRectangle, color)` (line 69) is the real 4-arg `SpriteBatch::Draw` overload.

### Behavioral correctness — independently re-derived expected colors
Traced `SpriteBatch::Begin` (`SpriteBatch.cpp:119-121`): `backend_->SetSamplerFilter(...)` and
`SetSamplerAddressMode(...)` are called with the *effective* sampler (the one passed to `Begin`, defaulting
otherwise) — confirmed this is not a dead parameter. Traced
`EasyGLSpriteBatchBackend::SetSamplerFilter`/`SetSamplerAddressMode` (`EasyGLGraphicsBackend.cpp:1065-1074`):
these only stash `pendingFilter_`/`pendingAddressU_`/`pendingAddressV_` as instance state; the actual GL state
change happens in `FlushBatch()` (`EasyGLGraphicsBackend.cpp:1145`):
`graphicsBackend_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1)` — slot 0,
matching the single-texture-unit assumption this test (and its siblings) rely on. Traced
`EasyGLGraphicsBackend::ApplySamplerState` (lines 2055-2139): confirmed the `TextureAddressMode → GL wrap`
mapping (`Wrap=0→Repeat, Clamp=1→ClampToEdge, Mirror=2→MirroredRepeat`, lines 2127-2134) matches
`TextureAddressMode.hpp`'s actual enum order (`Wrap=0, Clamp=1, Mirror=2`) exactly — this is the real,
load-bearing mapping this test is a regression guard for, not an assumption.

Re-derived the expected pixel colors from texel-center geometry: a 2-texel-wide texture under point sampling
has texel 0 covering normalized `u∈[0,0.5)` and texel 1 covering `u∈[0.5,1.0)`. At the *raw* (pre-wrap/clamp)
`u=1.25`:
- **`PointWrap`**: `fract(1.25)=0.25` → falls in `[0,0.5)` → texel 0 → **Red**. Matches the test's
  `wrapPass` check (`wrapPixel.R==255 && wrapPixel.B==0`, line 88).
- **`PointClamp`**: raw `u=1.25` clamps to `1.0`; at exactly the `1.0` boundary, nearest-neighbor sampling
  under `GL_CLAMP_TO_EDGE` resolves to the last texel (texel 1, centered at `u=0.75`, distance `0.25`, versus
  texel 0 centered at `u=0.25`, distance `0.75`) → **Blue**. Matches the test's `clampPass` check
  (`clampPixel.R==0 && clampPixel.B==255`, line 89).

Both derivations independently confirm the test's own expected values, not merely restate them.

Also confirmed the `sourceRectangle` tiling claim in the header comment: `SpriteBatch::Draw`'s implementation
(`SpriteBatch.cpp`, `src`/`dw`/`dh` derived directly from the caller-supplied `Rectangle` with no clamping to
the texture's own bounds) genuinely allows a `sourceRectangle` wider than the texture, confirming the
`Rectangle(0,0,4,1)` on a 2-wide texture (line 69) really does produce `U∈[0,2]` as claimed, not an incorrect
assumption about SpriteBatch behavior.

### Logic
`SampleAtUOnePointTwoFive(SamplerState*)` (lines 60-76) is called twice (once per sampler) with a fresh
`Clear` before each draw (line 67) — correct isolation between the two sub-cases; no risk of a leftover pixel
from the first draw contaminating the second's readback.

### Memory/resource lifetime
`sb_` (`std::unique_ptr<SpriteBatch>`), `tex_` (`Texture2D`, value member) have straightforward, correctly-scoped
lifetimes; no dangling-pointer risk. `const_cast<SamplerState*>(&SamplerState::PointWrap)` (line 85) is a
`const`-cast of a `static const` singleton passed into a non-`const SamplerState*` parameter — technically
requires the callee to only read the value (a mutation through this cast on a shared static singleton would be
a real defect); confirmed `SpriteBatch::Begin`'s handling of the sampler pointer only reads it (`SpriteBatch.cpp`
constructs `effectiveSampler` by value/copy from `*sampler`, not a stored pointer) — safe in practice, but the
API shape (accepting `SamplerState*` rather than `const SamplerState*` for a read-only parameter) is what forces
every caller of a `static const` preset to `const_cast`, which is a minor API-ergonomics wart rather than a bug
in this test file specifically (shared by every sibling file in this batch that uses a static preset).

### C++ correctness
See Memory/resource lifetime re: `const_cast`. `result_` defaults to `1` ("fail until proven otherwise", line
41) — a defensively-correct default that fails safe if `Draw()` is somehow never reached.

### Performance
N/A — two single-frame draws, no hot path.

### Robustness
No invalid-input path exercised; correct scope for a positive-path sampler-state test.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL findings.

### F1 — `AddressV` is set on both presets but never actually exercised (only U-axis addressing is tested)

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `SamplerState::PointWrap`/`PointClamp` (both set `AddressU`, `AddressV`, `AddressW` to the
  same mode per `SamplerState.cpp:6-11`); quad V-coordinate usage: `Rectangle(0, 0, W, H)` destination /
  `Rectangle(0, 0, 4, 1)` source (line 69) — source height `1` equals the texture's real height (`1`), so V
  never exceeds `[0,1)` regardless of `AddressV`'s mode.
- Evidence: this test's texture is `2×1` (only 1 texel tall), and its `sourceRectangle` height (`1`) exactly
  matches the texture height, so the sampled V coordinate never leaves `[0,1)` — meaning `AddressV`'s value
  (`Wrap` vs. `Clamp`) can never produce a different rendered pixel in this test, even though both presets set
  it. A regression that broke `AddressV` handling specifically (while leaving `AddressU` correct) would pass
  this test undetected.
- Why it matters: the file (and its three siblings in this batch) is titled/documented as a "TextureAddressMode"
  test but only ever exercises the U axis; a reader could reasonably assume V-axis addressing is equally
  covered by the same fix this test guards (Task 269), when it is not.
- FNA/XNA comparison: N/A (test-coverage gap, not an FNA behavior question).
- Suggested future action: a 1×2 (vertically-varying) texture variant with a taller-than-texture
  `sourceRectangle`, mirroring this file's horizontal design on the V axis, would close the gap; not
  implemented by this audit.

## Cross-File Observations

- This exact V-axis coverage gap (F1) is shared identically by all three sibling `easygl_texture_address_mode_*`
  files in this batch (`_mirror_test.cpp`, `_clamp_effect_test.cpp`, `_mirror_effect_test.cpp`) — every one
  uses a texture that is only 1 texel tall with a `sourceRectangle`/quad V-span that never exceeds `[0,1)`.
  Recorded once here in detail; referenced, not re-derived, in the sibling reports.
- The `const_cast<SamplerState*>(&SamplerState::...)` pattern (see C++ correctness) recurs in this file and
  the `_mirror_test.cpp` sibling; the two `_effect_test.cpp` siblings avoid it by constructing a local
  non-`const` `SamplerState` instead, since FNA has no `PointMirror` static preset.
- This file (Task 269) is the original fix/proof this whole coverage family descends from; `_mirror_test.cpp`
  (Task 737) explicitly closes the "same fix, untested third mode" gap this file's own header does not
  claim to cover, and the two `_effect_test.cpp` files (Tasks 294/296) extend coverage from the `SpriteBatch`
  code path to the independent `DrawUserPrimitives`+`Effect` code path. Verified this is a deliberate,
  non-redundant coverage expansion, not duplicated testing — the two code paths apply `SamplerState` through
  entirely different production call chains (`SpriteBatch::Begin`→backend `Set*` calls vs.
  `GraphicsDevice::SamplerStates[]`→`applySamplerStatesToBackend()`).

## Missing or Weak Tests

- V-axis addressing is never exercised (F1).
- No case exercises `Linear` filtering (only `Point`, deliberately, to avoid bilinear blending across the
  texel boundary) — a reasonable, explicitly-justified scope limit, not an oversight.
- No case combines a non-default `AddressU`/`AddressV` with a *bound render target* (as opposed to the default
  backbuffer) — out of scope for this specific fix-proof test, but worth noting as an untested interaction
  elsewhere in the shard.

## Positive Findings

- Independently re-derived both expected pixel colors from texel-center/point-filtering geometry and
  confirmed they match the test's own assertions — this is a genuine pixel-behavior test, not a "compiles and
  doesn't crash" test.
- Traced the complete `SpriteBatch::Begin → SetSamplerFilter/SetSamplerAddressMode → FlushBatch →
  ApplySamplerState` call chain in production code and confirmed the `TextureAddressMode` enum-to-GL-wrap-mode
  mapping this test guards is real and load-bearing, not a no-op path that would make this test pass
  regardless of correctness.
- `result_` defaulting to fail-safe (`1`) is a small but correct defensive-testing habit.

## Final Assessment

A genuine, evidence-based regression test for the Task 269 `SpriteBatch` sampler-state fix: its expected pixel
colors were independently re-derived and confirmed correct, and the full production call chain it exercises
was traced end-to-end and found to be the actual, load-bearing code path (not a coincidentally-passing
no-op). The one real gap — V-axis addressing set but never exercised — is shared across this entire test
family and worth closing once, centrally.
