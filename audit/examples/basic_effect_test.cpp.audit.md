# Audit: examples/basic_effect_test.cpp

## Metadata

- Source file: `examples/basic_effect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — backend-agnostic `BasicEffect` property/default
  round-trip test (Task 22).
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_basic_effect examples/basic_effect_test.cpp)` /
  `cna_register_backend_test(NAME EasyGL_BasicEffect_Properties …)`,
  `cmake/Tests/EasyGLTests.cmake:181-185`). Registered **only** for EasyGL — no other
  backend's CMake file references this filename, despite exercising only public
  `Microsoft::Xna::Framework::Graphics` API.
- XNA/FNA relevance: direct — `BasicEffect`'s full public property surface
  (`World`/`View`/`Projection`, `VertexColorEnabled`, `TextureEnabled`, `Texture`,
  `LightingEnabled`, `AmbientLightColor`, `DiffuseColor`, `EmissiveColor`, `SpecularColor`,
  `SpecularPower`, `Alpha`, `PreferPerPixelLighting`, `FogEnabled`/`FogColor`/`FogStart`/
  `FogEnd`, `DirectionalLight0/1/2`, `EnableDefaultLighting()`).
- FNA reference: `StockEffects/BasicEffect.cs` (field defaults, constructor behavior,
  `EnableDefaultLighting()`'s exact ambient/light values).
- Related production code: `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp`,
  `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`.

## Purpose

Same shape as `alpha_test_effect_test.cpp` (this batch): constructs a real `BasicEffect`
against a live `GraphicsDevice` inside `Initialize()` and checks every public getter's default
value plus every setter's round-trip, `EnableDefaultLighting()`'s effect on lighting state,
`GetTypeName()`, and the base `Effect` class's `Techniques`/`CurrentTechnique` surface.

## Checklist Results

### API / XNA / FNA parity
Cross-checked every getter/setter this file exercises against `BasicEffect.hpp`'s actual
current declarations and confirmed all but one follow this project's `getXProperty()`/
`setXProperty()` convention. **`VertexColorEnabled` does not**: `BasicEffect.hpp` line 48
declares it `bool VertexColorEnabled = false;` — a bare public field, with no
`getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` wrapper anywhere in the
class (confirmed by reading the full header; every other boolean/float/Vector3/Matrix
property on this exact class *does* have the wrapper pair). This file's own test code reflects
that inconsistency directly and unremarkably: lines 71-75 read `fx.VertexColorEnabled ==
false` / assign `fx.VertexColorEnabled = true;` as plain field access, with no
`getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` call anywhere in the file
— whereas the structurally-identical `AlphaTestEffect` (`alpha_test_effect_test.cpp`, this
batch) exercises the *same conceptual property* via `fx.getVertexColorEnabledProperty()`/
`fx.setVertexColorEnabledProperty(true)`, because `AlphaTestEffect.hpp` *does* wrap it
correctly (see that file's own header, lines 199-206). See F1.

### Behavioral correctness
Verified every asserted default against `BasicEffect.hpp`'s field initializers and
`BasicEffect.cpp`'s constructor: `World`/`View`/`Projection` default identity (header lines
41-45, in-class initializers), `VertexColorEnabled` default `false` (line 48),
`TextureEnabled` default `false` (`textureEnabled_ = false`, line 369), `Texture` default
`nullptr` (line 370), `LightingEnabled` default `false` (line 367),
`AmbientLightColor` default zero (`Vector3::Zero`, line 365), `DiffuseColor` default white
(`Vector3{1,1,1}`, line 361), `EmissiveColor` default zero (line 362), `SpecularColor` default
white (line 363), `SpecularPower` default `16.0f` (line 364), `Alpha` default `1.0f` (line
366), `PreferPerPixelLighting` default `false` (line 368), Fog defaults (`fogEnabled_=false`,
`fogColor_=Zero`, `fogStart_=0`, `fogEnd_=1`, lines 372-375) — all match FNA's own field
initializers (`BasicEffect.cs` lines 36-60) exactly. **`DirectionalLight0` default-enabled**
(line 156's check, `"DirectionalLight0 default enabled (matches FNA's BasicEffect ctor)"`)
was independently cross-checked against both `BasicEffect.cpp`'s constructor (line 11:
`DirectionalLight0.setEnabledProperty(true);`) and FNA's own constructor (`BasicEffect.cs`
line 365: `DirectionalLight0.Enabled = true;`) — genuinely correct and a non-obvious XNA
behavior worth exactly this kind of explicit test (light0 is the only one of the three
enabled by default; `DirectionalLight1`/`DirectionalLight2` correctly assert `false` at
lines 169-170). `EnableDefaultLighting()`'s block (lines 173-181) correctly checks all three
lights become enabled and ambient becomes non-zero, matching `BasicEffect::EnableDefaultLighting()`
(`BasicEffect.cpp` lines 186-205), which sets FNA's exact canonical key/fill/back-light rig.

### Robustness
No invalid-argument/boundary cases exercised — appropriate, matching FNA's own lack of
validation on these setters (e.g. no clamping on `SpecularPower`, `Alpha`, or any color
channel).

### Testing
Thorough property-surface coverage; does not exercise `Clone()` (see F2 — same gap noted for
`AlphaTestEffect` in this batch) nor `OnApply()`'s derived GPU parameter computation
(`FillGpuDrawParams()`) — correctly out of scope for a property-only test.

### Cross-file consistency
Internally consistent with `BasicEffect.hpp`/`.cpp`'s current implementation in every value
checked. The one inconsistency found (F1) is a property of the **production header**, not an
authoring mistake in this test file — the test simply, correctly, reflects however the class
is actually declared today.

## Detailed Findings

### F1 — `BasicEffect::VertexColorEnabled` is a bare public field, breaking this project's own explicit C# property convention — directly exercised (and thus directly exposed) by this file

- Severity: MEDIUM
- Confidence: HIGH (read `BasicEffect.hpp` in full; confirmed no
  `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` exists anywhere on the
  class, and confirmed the sibling `AlphaTestEffect` correctly wraps the conceptually
  identical property)
- Category: API design / project-convention violation
- Location/symbol: `BasicEffect.hpp` line 48 (`bool VertexColorEnabled = false;`); exercised
  directly by this file at lines 71-75 (`fx.VertexColorEnabled == false`,
  `fx.VertexColorEnabled = true;`, `fx.VertexColorEnabled = false;`)
- Evidence: CLAUDE.md's own explicit convention: *"C# properties use the established CNA
  convention: `getXProperty()` / `setXProperty()`… Do not replace C# properties with public
  fields unless the type already establishes that style."* Every other property on
  `BasicEffect` (18 distinct getter/setter pairs, by count of this file's own assertions
  alone) follows that convention; `VertexColorEnabled` is the sole exception on this class.
  FNA's own C# source (`BasicEffect.cs` lines 337-349) declares `VertexColorEnabled` as a
  real property with a non-trivial setter (`dirtyFlags |= EffectDirtyFlags.ShaderIndex` on
  change) — so the field-vs-property choice here is a CNA-side authoring inconsistency, not
  something forced by matching a simpler FNA shape. This is the same defect already
  identified independently, from the *consumer* side, by two prior audits in this project
  (`bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp` and
  `vulkan_basiceffect_vertexcolor_enabled_test.cpp`, per `AUDIT_CROSS_CUTTING_FINDINGS.md`'s
  "API design" section) — this file is a **third, independent confirmation**, and the first
  one in a generic (non-backend-named) test that exercises `BasicEffect` directly rather than
  through a specific rendering backend's own test.
- Why it matters: (1) it silently loses the FNA-documented `dirtyFlags |=
  EffectDirtyFlags.ShaderIndex` side effect on the C++ side entirely — a raw field write has
  no hook, so if `OnApply()`'s shader-variant selection is ever changed to depend on this
  flag's dirty-tracking rather than reading the field directly every frame (as it currently
  does, per `FillGpuDrawParams()` line 56: `p.vertexColorEnabled = VertexColorEnabled;`, read
  fresh each call — masking this specific consequence *today*, but fragile), a future
  refactor could silently break the shader-index recomputation for this one property while
  every other property's setter continues correctly marking itself dirty; (2) it is a
  genuine, user-visible API inconsistency — a caller who is used to
  `fx.setLightingEnabledProperty(true)` has no way to guess that the very next line should be
  `fx.VertexColorEnabled = true;` instead of `fx.setVertexColorEnabledProperty(true)`, without
  reading the header; (3) it means this specific test file cannot be a template for adding a
  matching case to any of the multi-backend `basiceffect_vertexcolor_enabled_test.cpp`-style
  files without either perpetuating the inconsistent access pattern or silently diverging
  from what this generic property test does.
- FNA/XNA comparison: FNA's `VertexColorEnabled` is a real C# property (getter delegates to a
  private field; setter has a dirty-flag side effect) — CNA's public-field version is
  behaviorally equivalent *only* because `FillGpuDrawParams()` happens to re-read the field
  directly every frame rather than relying on a dirty flag for this specific property; it is
  not equivalent in shape or in the C++-convention sense CLAUDE.md establishes.
- Suggested future action (not implemented by this audit): add
  `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty(bool)` to `BasicEffect`
  matching every sibling property, deprecate/remove the public field, and update this file
  (plus the two backend-specific tests already flagged in
  `AUDIT_CROSS_CUTTING_FINDINGS.md`) to use the wrapper — a small, mechanical, low-risk fix
  given `FillGpuDrawParams()` already reads the underlying state fresh per call.

### F2 — `Clone()` has no test coverage in this file (same gap as `AlphaTestEffect`, this batch)

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage
- Location/symbol: `BasicEffect::Clone()` (`BasicEffect.cpp` lines 41-44), private copy
  constructor (lines 14-39)
- Why it matters: the copy constructor copies `VertexColorEnabled` directly (line 19,
  `, VertexColorEnabled(cloneSource.VertexColorEnabled)`) and every other field individually
  — a straightforward, low-risk implementation, but with zero test coverage confirming clone
  independence (e.g. that mutating the clone's `DiffuseColor` doesn't affect the original's).
- Suggested future action (not implemented by this audit): same as F1 in
  `alpha_test_effect_test.cpp.audit.md` — add a clone-independence case.

## Cross-File Observations

- Direct structural sibling of `alpha_test_effect_test.cpp` (this batch) — same
  `check()`/`veq()` helpers, same overall shape. The contrast between the two files'
  `VertexColorEnabled` access patterns (`fx.getVertexColorEnabledProperty()` for
  `AlphaTestEffect` vs. `fx.VertexColorEnabled` for `BasicEffect`) is the clearest, most
  concrete illustration available in this batch of the cross-cutting API-design finding
  already tracked in `AUDIT_CROSS_CUTTING_FINDINGS.md` — both tests are otherwise
  near-identical in intent and structure, making the divergence unmistakably a production-code
  property, not a difference in testing style between the two files.
- `EnableDefaultLighting()`'s exact numeric rig (ambient `{0.0533,0.0988,0.1820}`, light0
  diffuse `{1.0,0.9608,0.8078}`, etc.) is shared verbatim between `BasicEffect.cpp` and
  `SkinnedEffect.cpp` (confirmed while cross-referencing this batch's avatar test files) —
  both correctly mirror FNA's identical canonical three-point lighting rig; worth noting this
  is intentionally duplicated, not a missed-refactor DRY violation, since `BasicEffect` and
  `SkinnedEffect` share no common base class for this method.

## Missing or Weak Tests

- F1 — no test can currently distinguish "the getter/setter convention is intentionally
  skipped for this one property" from "this was simply missed"; the header carries no comment
  explaining the divergence either.
- F2 — `Clone()` independence, as with `AlphaTestEffect`.
- No test constructs `BasicEffect` with a real (non-null) texture and confirms
  `getTextureProperty()` round-trips it — only the `nullptr` case is exercised (lines 85-88),
  mirroring the identical minor gap noted for `AlphaTestEffect`.

## Positive Findings

- Every asserted default and every `EnableDefaultLighting()` value was independently verified
  against the current production header/impl, not merely trusted — all correct, and the
  non-obvious "`DirectionalLight0` alone starts enabled" behavior is explicitly, correctly
  tested and independently confirmed to match FNA precisely.
- This file's own exercising of the inconsistent `VertexColorEnabled` field access (rather
  than papering over it) is itself useful evidence for F1 — it makes the production-code
  inconsistency directly visible to anyone reading the test, rather than hiding it behind a
  uniform-looking call site.

## Final Assessment

Needs attention. The property/default coverage itself is accurate and independently verified
against FNA and the current production code with no behavioral defects found; the file is
downgraded from "Healthy" specifically because it is the clearest, most direct piece of
evidence in this audit batch for the pre-existing, cross-cutting `BasicEffect::VertexColorEnabled`
bare-public-field inconsistency (F1) — a production-code issue this test file surfaces rather
than causes.
