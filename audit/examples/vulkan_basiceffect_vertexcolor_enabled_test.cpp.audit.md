# Audit: examples/vulkan_basiceffect_vertexcolor_enabled_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_vertexcolor_enabled_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `BasicEffect` `VertexColorEnabled=true` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered integration test (Task 365)
- XNA/FNA relevance: direct — `BasicEffect.VertexColorEnabled`/`DiffuseColor` multiply-together
  behavior.
- FNA reference: `HLSL/Common.fxh`'s `ComputeCommonVSOutput()` + `VSBasicVcNoFog`'s
  `vout.Diffuse *= vin.Color` (per this file's own header comment, verified below), `BasicEffect.cs`
  `VertexColorEnabled` property (setter marks `EffectDirtyFlags.ShaderIndex`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp`/`.cpp`,
  `src/CNA/Internal/Backends/Vulkan/shaders/colored3d.vert.glsl`.

## Purpose

Three-check pixel test proving `BasicEffect`'s `VertexColorEnabled=true` branch multiplies
`DiffuseColor` by the per-vertex colour component-wise, using values chosen so the correct product
`(160,40,30)` is numerically distinct from either input alone (`DiffuseColor`-only would read back as
`(204,102,153)`; `VertexColor`-only as `(200,100,50)`) — a real discrimination test, not just "does it
render something".

## Executive Verdict

**Needs attention** — the effect's actual multiply behavior is correctly tested and independently
confirmed correct against the current shader, but the test file itself (`fx.VertexColorEnabled =
true;`, line 94) directly exercises a genuine, verifiable API-surface inconsistency in the production
`BasicEffect` class: `VertexColorEnabled` is a bare public field with no `getX/setX` wrapper at all,
unlike every other property on the same class.

## Checklist Results

### API / XNA / FNA parity
`fx.VertexColorEnabled = true;` (line 94) is a **direct public-field write**, not the
`setVertexColorEnabledProperty(true)` call this project's own `CLAUDE.md` mandates for C# properties
("C# properties use the established CNA convention: `getXProperty()`/`setXProperty(…)`... Do not
replace C# properties with public fields unless the type already establishes that style"). Confirmed
by reading `BasicEffect.hpp` in full: `bool VertexColorEnabled = false;` (line 48) has **no**
accompanying `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` methods anywhere in
the header (`grep -n "VertexColorEnabled"` returns only the one field declaration). This is not merely
"the type already establishes that style" for public fields in general — `World`/`View`/`Projection`
are also public fields on this same class, but each one *additionally* gets a matching
`getWorldProperty()`/`setWorldProperty()`-style wrapper method (e.g. `getWorldProperty() const
override { return World; }`, line ~56). `VertexColorEnabled` is missing even that: it is the *only*
member on `BasicEffect` reachable exclusively through a bare field, with zero property-method access.
- Confirmed the sibling class does this correctly: `DualTextureEffect` exposes the identical XNA
  concept via `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty(bool)`
  (`DualTextureEffect.cpp:170-178`), proving the codebase's own established convention is achievable
  and simply wasn't applied to `BasicEffect`.
- Real FNA semantics being silently dropped: FNA's `VertexColorEnabled` setter is not a plain field
  write — it conditionally marks `dirtyFlags |= EffectDirtyFlags.ShaderIndex` only when the value
  actually changes (`BasicEffect.cs` lines 339-349). CNA's `BasicEffect` has no dirty-flag/ShaderIndex
  mechanism at all (`FillGpuDrawParams()` recomputes everything fresh every `Apply()`/draw), so this
  specific behavioral loss is currently inconsequential to runtime correctness — but it means the
  class's public shape no longer has anywhere to *put* such logic later without an API break, since
  callers across the codebase already write the field directly.
- `git blame`/`git log -p -- include/.../BasicEffect.hpp` confirms this has been a bare field since
  before the Task 361 fix that corrected its default value (`true`→`false`) — a longstanding gap, not
  new.

### Behavioral correctness
Independently re-derived the expected pixel value against the current shader. `colored3d.vert.glsl`:
`fragColor = (pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor : pc.diffuseColor;` — with
`vertexColorEnabled=true`, `fragColor = kVertexColor(200,100,50,200)_normalized × kDiffuse(0.8,0.4,0.6)`
(alpha channel of `DiffuseColor` here is `1.0`, from `BasicEffect`'s default `alpha_`, so RGB is a pure
component product): `200×0.8=160`, `100×0.4=40`, `50×0.6=30` — exact integers, matching `kExpected`
exactly with no rounding ambiguity. This confirms `FillGpuDrawParams()` (`BasicEffect.cpp:56`,
`p.vertexColorEnabled = VertexColorEnabled;`) correctly reads the field's current value (the write
itself works — this finding is about API-surface consistency, not a functional defect).

### Logic
The three checks (`matches(kExpected)`, `!matches(kDiffuseOnly)`, `!matches(kVertexOnly)`, lines
120-125) are a genuinely strong discrimination set: any implementation that used only one of the two
inputs, or added them instead of multiplying, would fail at least one of the three (`kDiffuseOnly` and
`kVertexOnly` are both plausible wrong-implementation outputs, not arbitrary decoys).

### C++ correctness
Same `RasterizerState::CullNone` Task-896 pattern as the sibling `_disabled_test.cpp` file (line 112),
independently confirmed necessary and correctly applied.

### Testing
Genuinely discriminating, well-designed test of the effect's *runtime* behavior. The one gap is that
the test necessarily exercises (and therefore silently normalizes) the field-write API-surface issue
described above, without anyone noticing since it happens to compile and work.

## Detailed Findings

### F1 — `BasicEffect.VertexColorEnabled` is a bare public field with no `getX/setX` wrapper, breaking this project's own C++ property convention and this class's own internal precedent
- Severity: MEDIUM
- Confidence: HIGH (read the full header, confirmed absence of any accessor method, confirmed the
  sibling `World`/`View`/`Projection` fields on the same class *do* get wrapper methods, confirmed
  `DualTextureEffect` implements the identical XNA concept correctly via `getX/setX`)
- Category: api-consistency / architecture
- Location/symbol: `bool VertexColorEnabled = false;` (`BasicEffect.hpp:48`); exercised at
  `fx.VertexColorEnabled = true;` (this file, line 94)
- Evidence: `grep -n "VertexColorEnabled" include/.../BasicEffect.hpp` returns exactly one line (the
  field declaration) — no getter, no setter. Contrast `TextureEnabled`
  (`getTextureEnabledProperty()`/`setTextureEnabledProperty(bool)`, lines 166-167) and `DiffuseColor`
  (`getDiffuseColorProperty()`/`setDiffuseColorProperty(const Vector3&)`, lines 155-156) on the exact
  same class, and `World`/`View`/`Projection` (public fields *plus* wrapper methods, lines 40-42 +
  accessors). `CLAUDE.md` (this project's binding coding standard) states verbatim: "Do not replace C#
  properties with public fields unless the type already establishes that style" — and even by that
  more permissive bar, `BasicEffect` establishes "field + wrapper", not "field only", making
  `VertexColorEnabled` inconsistent with its own class, not just with the project norm in the abstract.
- Why it matters: this is not a runtime-correctness bug (the field is read correctly by
  `FillGpuDrawParams()`, verified above) but a real, durable API-surface gap: any future change that
  needs `VertexColorEnabled`'s setter to have side effects (mirroring FNA's own dirty-flag marking, or
  any future CNA-side caching optimization) cannot be added without either breaking every existing
  direct-field-write call site or introducing an inconsistent shadow property alongside the field.
  It also means static analysis / API-surface tooling that greps for `getXProperty()`/`setXProperty()`
  to enumerate this class's XNA-facing surface would silently miss `VertexColorEnabled` entirely.
- FNA/XNA comparison: FNA's `VertexColorEnabled` is a real C# property with a non-trivial setter body
  (dirty-flag marking) — CNA's bare-field version cannot express that even if a future task needed to.
- Related files: `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp` (the actual defect
  location); this test file and its `_disabled_test.cpp` sibling are simply the call sites that
  surfaced it.
- Suggested future action (not implemented by this audit — out of scope for an audit-only task): add
  `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty(bool)` wrapper methods around the
  existing field (mirroring the `World`/`View`/`Projection` pattern already established on this same
  class), then decide whether to keep the field public (for source compatibility with existing direct
  writes like this test) or migrate call sites to the new setter.

## Cross-File Observations

- The identical field (same XNA concept) is implemented correctly on `DualTextureEffect`
  (`getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty(bool)`, with real dirty-flag
  marking: `dirtyFlags_ |= DirtyShaderIndex;`, `DualTextureEffect.cpp:170-178`) — proving this is an
  inconsistency specific to `BasicEffect`, not a project-wide architectural choice.
- The sibling `vulkan_basiceffect_vertexcolor_disabled_test.cpp` does not touch this field at all (it
  tests the default, untouched), so F1 does not manifest there; its own audit report cross-references
  this finding rather than duplicating it.

## Missing or Weak Tests

None specific to this file's own stated purpose — its three-way discrimination check is solid.

## Positive Findings

- The chosen constants (`kVertexColor(200,100,50,200)`, `kDiffuse(0.8,0.4,0.6)`) were picked so the
  correct product, diffuse-only, and vertex-only results are all numerically distinct — verified this
  independently by hand (see Behavioral correctness above) and confirmed it holds exactly.
- The underlying shader logic (`colored3d.vert.glsl`'s `vertexColorEnabled` gate) is correct and
  matches FNA's `VSBasicVcNoFog`'s `vout.Diffuse *= vin.Color` semantics.

## Final Assessment

The test itself is well-designed and passes for the right reason. The audit's main product from this
file is a genuine, independently-verified production-code finding (F1) about `BasicEffect`'s
`VertexColorEnabled` breaking both the project's documented C++ convention and its own class's internal
precedent — worth a small, low-risk follow-up (add the missing wrapper methods) since it is exactly the
kind of surface gap that becomes expensive to fix once more call sites accumulate.
