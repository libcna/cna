# Audit: examples/easygl_basiceffect_vertexcolor_enabled_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_vertexcolor_enabled_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `BasicEffect.VertexColorEnabled=true` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_basiceffect_vertexcolor_enabled …)` /
  `cna_register_backend_test(NAME EasyGL_BasicEffect_VertexColorEnabled …)`,
  `cmake/Tests/EasyGLTests.cmake:1079-1081`).
- XNA/FNA relevance: direct — `BasicEffect.VertexColorEnabled=true`, `VSBasicVcNoFog`/`PSBasicNoFog`
  shader family (`vout.Diffuse *= vin.Color`).
- FNA reference: `HLSL/Common.fxh` (`ComputeCommonVSOutput` + the `*Vc*` shader family's per-vertex
  multiply), `EffectHelpers.cs` (`SetMaterialColor`).
- Related production code: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureColored3DProgram()` lines 2583-2641, `uVertexColorEnabled` gate lines 2620-2624).

## Purpose

The direct complement of `easygl_basiceffect_vertexcolor_disabled_test.cpp`: proves
`VertexColorEnabled=true` genuinely multiplies the per-vertex color attribute into the output
(`DiffuseColor * VertexColor`), exercising the `true` branch of `EnsureColored3DProgram()`'s
`uVertexColorEnabled` gate — the branch that predates the Task 364 fix (which only added/fixed the
`false` branch), so this file's own comment (lines 17-24) correctly frames itself as new-coverage
verification rather than a bug-fix test, while still insisting the claim be *confirmed* by an actual
pixel readback rather than assumed.

## Executive Verdict

**Healthy** — the expected pixel value and both discriminating negative checks were independently
re-derived and match the current shader source exactly.

## Checklist Results

### API / XNA / FNA parity
`fx.VertexColorEnabled = true;` (line 111) — same direct public-field assignment pattern flagged in
the sibling `texture_vertexcolor_enabled` test's report (BasicEffect.hpp exposes no
`setVertexColorEnabledProperty()` wrapper at all; see that report's Cross-File Observations for the
full analysis — not repeated in detail here to avoid duplication, but the same underlying
`BasicEffect.hpp` API-surface inconsistency applies to this file's usage too).

### Behavioral correctness
Confirmed `EnsureColored3DProgram()`'s fragment shader (lines 2622-2624):
`vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0); FragColor=vc*uDiffuseColor;` — with
the gate now `true`, `vc` reads the real per-vertex attribute. `kVertexColor=(200,100,50,200)`,
`kDiffuse=(0.8,0.4,0.6)`: `(200/255)*0.8*255=160`; `(100/255)*0.4*255=40`; `(50/255)*0.6*255=30` —
exactly `kExpected(160,40,30,255)` (line 61). `kDiffuseOnly(204,102,153)` (vertex color forced white,
the pre-fix/broken-gate failure mode) and `kVertexOnly(200,100,50)` (diffuse ignored) are both
correctly, widely separated from `kExpected` (closest gap 40, versus `±8` tolerance) — no accidental-
pass risk.

### Logic
Dispatch identical to the sibling `vertexcolor_disabled` test (`VertexPositionColor`, stride 16 →
`SelectProgram()` default case → `EnsureColored3DProgram()`), confirmed the same production code path
is exercised from the opposite gate state.

### C++ correctness
Semi-transparent vertex color (`alpha=200/255≈0.784`, not opaque) is a deliberate choice
(line 26 comment) to avoid a coincidentally-round alpha value — reasonable, though this test's own
assertions (lines 138-143) only check R/G/B via `matches()`, not alpha; the alpha channel's own
correctness (`vc.a * uDiffuseColor.a`) is implicitly exercised by the render pipeline but not
independently asserted in this file. Minor, proportionate scope choice (alpha/blending has its own
dedicated test files elsewhere in this shard).

### Robustness
Good discriminating design: both single-input failure modes (vertex-only, diffuse-only) are
explicitly ruled out, not just "pixel is neither black nor the unmultiplied inputs."

### Testing
Correctly pairs with `easygl_basiceffect_vertexcolor_disabled_test.cpp` for full two-sided coverage
of the same boolean gate.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `fx.VertexColorEnabled = true` uses BasicEffect's only available API (raw public field, no getter/setter)

- Severity: LOW
- Confidence: HIGH
- Category: cross-file API consistency (see full analysis in the sibling
  `easygl_basiceffect_texture_vertexcolor_enabled_test.cpp` audit report's Cross-File Observations)
- Location/symbol: line 111 (`fx.VertexColorEnabled = true;`); `BasicEffect.hpp` line 48
- Evidence: `BasicEffect.hpp` declares `bool VertexColorEnabled = false;` as a bare public field with
  no `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` anywhere in the class, unlike
  every other `IEffectLights`/`IEffectFog` member. FNA's own `BasicEffect.cs` declares
  `VertexColorEnabled` as a real C# property with a `dirtyFlags` side effect on change.
- Why it matters: this test file has no alternative API to use — it is not a test-authoring choice,
  it reflects `BasicEffect.hpp`'s own incomplete property-wrapper coverage. Recorded here for
  completeness since this file's content directly evidences it, but the actionable finding belongs to
  whichever shard audits `BasicEffect.hpp` itself.
- Suggested future action (not implemented by this audit): none from this test file's perspective —
  see the `BasicEffect.hpp`/`xna-graphics` shard audit for the actual remediation recommendation.

## Cross-File Observations

- Forms a matched pair with `easygl_basiceffect_vertexcolor_disabled_test.cpp` — both target the same
  `uVertexColorEnabled` gate in `EnsureColored3DProgram()` from opposite states; together they give
  complete branch coverage of that specific conditional.
- Shares the `(200,100,50)` numeric triple with `easygl_basiceffect_texture_enabled_test.cpp`'s
  `kTexColor` and `easygl_basiceffect_texture_vertexcolor_enabled_test.cpp`'s `kTexColor` — all three
  files' expected constants are mutually cross-checkable via the shared `(0.8,0.4,0.6)` diffuse
  multiplier, and this audit confirmed they agree.

## Missing or Weak Tests

Alpha-channel correctness (`vc.a * uDiffuseColor.a`) is exercised by the render pipeline but not
independently asserted — low-priority given dedicated alpha/blend test files exist elsewhere in this
shard.

## Positive Findings

- Explicit framing as "new-coverage verification, not assumed" (header comment) and then actually
  computing/asserting a non-trivial multiplied value (rather than settling for "vertex color visibly
  changes something") is good test discipline.
- Deliberately non-opaque vertex-color alpha avoids a coincidental round-number blind spot.

## Final Assessment

A correct, precisely-targeted complement to the `vertexcolor_disabled` test; its expected pixel value
and both negative discriminating checks are fully verified against the current shader source.
