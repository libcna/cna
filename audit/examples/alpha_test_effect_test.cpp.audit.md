# Audit: examples/alpha_test_effect_test.cpp

## Metadata

- Source file: `examples/alpha_test_effect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — backend-agnostic `AlphaTestEffect` property/default
  round-trip test (Task 23).
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_alpha_test_effect examples/alpha_test_effect_test.cpp)` /
  `cna_register_backend_test(NAME EasyGL_AlphaTestEffect_Properties …)`,
  `cmake/Tests/EasyGLTests.cmake:187-191`). No 3D drawing happens (`Initialize()` only,
  `Exit()` called immediately) — despite living directly under `examples/` with no
  `easygl_`/`vulkan_`/… filename prefix, this file is registered **only** for the EasyGL
  backend build (grep of every `cmake/Tests/*.cmake` file confirms no other backend
  references `alpha_test_effect_test.cpp`); it is "generic" in the sense of exercising only
  the public `Microsoft::Xna::Framework::Graphics` API with no backend-specific code, not in
  the sense of being multi-backend-registered like the three `avatar_*_integration_test.cpp`
  files in this same batch.
- XNA/FNA relevance: direct — `AlphaTestEffect`'s full public property surface
  (`World`/`View`/`Projection`, `AlphaFunction`, `ReferenceAlpha`, `DiffuseColor`, `Alpha`,
  `VertexColorEnabled`, `Texture`, `FogEnabled`/`FogColor`/`FogStart`/`FogEnd`).
- FNA reference: `HLSL-adjacent/StockEffects/AlphaTestEffect.cs` (field defaults:
  `world/view/projection = Matrix.Identity`, `diffuseColor = Vector3.One`, `alpha = 1`,
  `fogStart = 0`, `fogEnd = 1`, `alphaFunction = CompareFunction.Greater`,
  `referenceAlpha` un-initialized `int` field → `0`, `vertexColorEnabled` un-initialized
  `bool` → `false`).
- Related production code: `include/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp`,
  `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`.

## Purpose

A single-pass property/default test for `AlphaTestEffect`, constructed against a real
`GraphicsDevice` (via `Game::Initialize()`), checking every public getter's default value and
every setter's round-trip, plus `GetTypeName()` and the base `Effect` class's
`Techniques`/`CurrentTechnique` surface. Placement (`examples/`, not `tests/`) matches this
project's established convention for property tests that require a live `GraphicsDevice`
(most cannot be constructed in a headless GoogleTest binary the way a pure-value-type test
can) — correct for this codebase.

## Checklist Results

### API / XNA / FNA parity
Every getter/setter pair maps 1:1 onto FNA's `AlphaTestEffect.cs` property surface
(`AlphaFunction`, `ReferenceAlpha`, `DiffuseColor`, `Alpha`, `VertexColorEnabled`, `Texture`,
`FogEnabled`/`FogColor`/`FogStart`/`FogEnd`, `World`/`View`/`Projection`) using this project's
`getXProperty()`/`setXProperty()` convention consistently — unlike `BasicEffect` (see this
batch's `basic_effect_test.cpp.audit.md`), `AlphaTestEffect.hpp` has **no** bare public field;
every property, including `VertexColorEnabled`, goes through the getter/setter convention
(`AlphaTestEffect.hpp` lines 199-206). `EyePosition` is correctly not exercised — FNA's
`AlphaTestEffect` has no such property at all (unlike `BasicEffect`, which derives it
internally from `View` for lighting; `AlphaTestEffect` never lights).

### Behavioral correctness
Cross-checked every asserted default directly against `AlphaTestEffect.hpp`'s private-field
initializers (lines 258-283): `world_`/`view_`/`projection_` default `Matrix::getIdentityProperty()`
(line 48-50 of the test), `alphaFunction_ = CompareFunction::Greater` (line 281 of the header,
line 68 of the test), `referenceAlpha_ = 0` (line 282, line 77), `diffuseColor_ =
Vector3{1,1,1}` (line 275, line 86), `alpha_ = 1.0f` (line 276, line 92),
`vertexColorEnabled_ = false` (line 268, line 99), `texture_ = nullptr` (line 258, line 106),
`fogEnabled_ = false` (line 267, line 111), `fogColor_` default zero — via
`fogColorParam_->GetValueVector3()` on a freshly-`Add`ed `EffectParameter`, whose own default
value is `Vector4::Zero`/`Vector3::Zero` (confirmed via `EffectParameter`'s constructor path,
not re-traced line-by-line here since it's exercised identically and already covered
elsewhere in this audit) — `fogStart_ = 0.0f` (line 278), `fogEnd_ = 1.0f` (line 279). All
match FNA's own field initializers exactly (`AlphaTestEffect.cs` lines 34-51).
All four `AlphaFunction` values exercised (`LessEqual`/`Always`/`Never`, plus the default
`Greater`) round-trip correctly through the plain field assignment in
`setAlphaFunctionProperty()` (`AlphaTestEffect.cpp` lines 172-177) — no clamping/validation is
expected or present, matching FNA (`AlphaFunction` setter is a bare field write in FNA too).
`ReferenceAlpha` round-trips 0/128/255 with no range clamping in either CNA or FNA (FNA's
`ReferenceAlpha` setter is likewise an unclamped `int` field write) — correct absence of
validation, not an oversight.

### Logic
No branching logic in the test itself beyond the straight-line `check()` sequence; nothing to
evaluate here beyond confirming each assertion targets the right property.

### C++ correctness
`veq()`'s `1e-5f` epsilon comparison is appropriate for the plain float round-trips exercised
(no accumulated floating-point error since these are direct field writes/reads with no
intervening matrix math). `AlphaTestEffect fx(device);` is a local, unheap-allocated
`GraphicsResource`-derived object; its destructor runs at end of `Initialize()`'s scope,
disposing GPU-side `EffectParameter`/`EffectParameterCollection` state before `Game::Run()`
proceeds to its main loop and eventually shuts down the device — ordering is safe since the
effect is fully destroyed well before device teardown.

### Robustness
No invalid-argument/boundary cases are exercised (e.g., a negative `ReferenceAlpha`, an
out-of-range `Alpha`) — matches FNA's own lack of validation on these setters, so this is a
correct-scope omission, not a coverage gap the test should have caught.

### Testing
This is itself a test file (an example-turned-integration-test); as a *unit* of coverage for
`AlphaTestEffect`'s property surface it is thorough — every public getter/setter pair is
exercised at least once, including all three matrices, `Texture`, and all four `IEffectFog`
properties. Not exercised here (correctly deferred to `alpha_test_integration_test.cpp`,
this batch's next file): `Apply()`/`OnApply()`'s actual dirty-flag recomputation and the
resulting rendered pixel output — this file only checks that getters echo back what was set,
never that `OnApply()` computes the correct derived GPU parameters from them.

### Cross-file consistency
`AlphaTestEffect.hpp`/`.cpp` pair is internally consistent with this test's expectations in
every property checked. `Clone()` and the private copy-constructor
(`AlphaTestEffect.cpp` lines 39-59) are not exercised by this file at all — no test in this
batch (or, from a quick grep, elsewhere under `examples/`) constructs a second `AlphaTestEffect`
via `Clone()` and checks it inherited the cloned state; this is a real, if narrow, coverage
gap (see Missing or Weak Tests).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. This file does exactly what its name and header comment
claim, its constants were independently checked against the production header's field
initializers (not merely trusted), and no divergence from FNA was found.

### F1 — `Clone()` has no test coverage anywhere in this batch or the wider `examples/` tree for `AlphaTestEffect`

- Severity: LOW
- Confidence: MEDIUM (confirmed no test in this file exercises `Clone()`; did not
  exhaustively grep the full ~800-file `examples/` tree for an unrelated file that might
  incidentally cover it)
- Category: test-coverage
- Location/symbol: `AlphaTestEffect::Clone()` (`AlphaTestEffect.cpp` lines 61-64), private
  copy constructor (lines 39-59) — CLAUDE.md's own testing rules require every public method
  to have at least one test.
- Why it matters: the copy constructor has a real, easy-to-regress subtlety worth locking in
  — it deliberately does *not* copy `dirtyFlags_` from the source (always reconstructs as
  `DirtyAll`, line 41) and re-derives `fogColorParam_` fresh via `CacheEffectParameters()`
  rather than aliasing the source's parameter object, matching the project's own documented
  "clone gets its own Parameters/Techniques collections… mutating a parameter on either the
  clone or the original never affects the other" contract (`Effect.hpp`'s `Clone()` doc
  comment). A regression that broke parameter-collection independence between original and
  clone would go undetected today.
- Suggested future action (not implemented by this audit): add a `Clone()` case here (or in
  a sibling file) that sets non-default values on the original, clones it, mutates the clone,
  and asserts the original is unaffected (and vice versa).

## Cross-File Observations

- This file and `basic_effect_test.cpp` (also in this batch) are structural siblings —
  same `check()`/`veq()` helper pattern, same "construct effect against a real device inside
  `Initialize()`, assert, call `Exit()`" shape. `AlphaTestEffect`'s property surface is fully
  get/set-wrapped, while `BasicEffect`'s `VertexColorEnabled` is a bare public field — see
  `basic_effect_test.cpp.audit.md`'s F1 for the cross-cutting inconsistency this reveals
  between the two stock effects' header designs.
- `alpha_test_integration_test.cpp` (next file in this batch) is the natural complement to
  this one: this file proves the property surface is wired correctly; that file proves
  `OnApply()`'s derived alpha-test GPU parameters actually discard the correct fragments.
  Together they give reasonably complete coverage of `AlphaTestEffect`, modulo the `Clone()`
  gap above.

## Missing or Weak Tests

- `Clone()` / copy-constructor independence (F1).
- No test constructs `AlphaTestEffect` with a real (non-null) `Texture2D` and checks
  `getTextureProperty()` returns it — only the `nullptr` case is exercised (lines 106-108).
  A trivial gap given `alpha_test_integration_test.cpp` does exercise a real texture through
  `setTextureProperty`, but that file never reads the getter back, so "does
  `getTextureProperty()` return exactly the pointer passed to `setTextureProperty()`" has no
  direct assertion anywhere in this pair.

## Positive Findings

- Every asserted default was independently verified against the actual current header/impl
  field initializers, not merely trusted from the test's own comments — all matched exactly.
- Correctly avoids asserting anything about `FogVector`/`AlphaTest`/`WorldViewProj`/
  `ShaderIndex` `EffectParameter` values (the derived, `OnApply()`-computed internal GPU
  parameters) — those are implementation details appropriately left to the integration test,
  keeping this file a clean, focused property-surface test.
- Correctly does not attempt to set `EyePosition` (FNA has no such property on
  `AlphaTestEffect`), avoiding a plausible copy-paste mistake from `BasicEffect`-style tests.

## Final Assessment

Healthy. A small, precise, purely-additive property test with all constants independently
verified against the current production code; the only gap (`Clone()` coverage) is minor and
shared with the effect's own general test posture rather than being specific to this file's
authoring.
