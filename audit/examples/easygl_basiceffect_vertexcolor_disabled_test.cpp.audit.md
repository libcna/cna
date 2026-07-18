# Audit: examples/easygl_basiceffect_vertexcolor_disabled_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_vertexcolor_disabled_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `BasicEffect.VertexColorEnabled=false` (default) pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_basiceffect_vertexcolor_disabled …)` /
  `cna_register_backend_test(NAME EasyGL_BasicEffect_VertexColorDisabled …)`,
  `cmake/Tests/EasyGLTests.cmake:1073-1075`).
- XNA/FNA relevance: direct — `BasicEffect.VertexColorEnabled`'s real FNA default (`false`),
  `VSBasicNoFog`/`PSBasicNoFog` shader family.
- FNA reference: `BasicEffect.cs` (`VertexColorEnabled` default), `HLSL/Common.fxh`
  (`ComputeCommonVSOutput`: `vout.Diffuse = DiffuseColor`, no `vin.Color` multiply in the non-`*Vc*`
  shader family).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp` (default
  `VertexColorEnabled` field init), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureColored3DProgram()` lines 2583-2641, `uVertexColorEnabled` gate lines 2620-2624).

## Purpose

Proves `VertexColorEnabled`'s real default (`false`, fixed by the referenced Task 361) genuinely
gates the vertex-color multiply at the shader/GPU level, not merely as an inert stored C++ property.
Draws a full-screen quad with a bright red per-vertex color and a distinct teal-ish `DiffuseColor`,
deliberately not setting `VertexColorEnabled` at all, and asserts the read-back pixel equals
`DiffuseColor` — not red, not a red/teal blend.

## Executive Verdict

**Healthy** — the expected pixel value and the "vertex color must be fully ignored" claim both check
out exactly against the current `EnsureColored3DProgram()` shader and `BasicEffect`'s documented
default.

## Checklist Results

### API / XNA / FNA parity
Deliberately does **not** call `setTextureEnabledProperty`/set `VertexColorEnabled` at all (line
100-103, explicit comment) — the correct way to test "the real default," since explicitly setting
`false` would not distinguish "default is false" from "default is true but this test happens to
override it correctly."

### Behavioral correctness
Confirmed `EnsureColored3DProgram()`'s fragment shader (lines 2622-2624):
```
vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);
FragColor=vc*uDiffuseColor;
```
With `VertexColorEnabled` at its default `false`, `uVertexColorEnabled` is `0.0`, so `vc` is forced to
`(1,1,1,1)` regardless of the actual `VertexPositionColor` attribute data — `FragColor` reduces to
`uDiffuseColor` alone. `DiffuseColor=(0.2,0.6,0.9)`×255 = `(51,153,229.5)`, matching
`kExpected(51,153,230,255)` (line 48, rounding `229.5→230`) exactly. `kVertexRed=(255,0,0,255)`
(line 49) is never read into the output under the correct implementation, and `looksRed()` (lines
82-85, `R≥200 && G≤60 && B≤60`) is a reasonable, generous heuristic for "did the red vertex color leak
through" that would reliably catch even a partial-blend bug (e.g. an accidental 50/50 mix would give
`R≈153,G≈76,B≈114`, still failing `looksRed`'s `G≤60` condition — actually this specific blend would
also fail the `matchesDiffuse` check by a wide margin, so the real discriminating power here comes
from `matchesDiffuse`, with `looksRed` as a clearer diagnostic for the fully-red-leak case
specifically).

### Logic
Dispatch confirmed: `VertexPositionColor` (position+color, stride 16) hits `SelectProgram()`'s
`default` case (`EasyGLGraphicsBackend.cpp` line 3979): `EnsureColored3DProgram(); return
prog_colored_;` — correctly the shader containing the `uVertexColorEnabled` gate this test targets.

### C++ correctness
`matchesDiffuse()`'s `±10` tolerance (lines 75-80) and `looksRed()`'s coarse thresholds are
appropriately separated: the gap between "correct" `(51,153,230)` and "red leaked"
`(255,0,0)`/"any red-ish blend" is large enough that no plausible partial-bug output could satisfy
both `matchesDiffuse` and fail to also fail some other sanity check.

### Robustness
Correctly exercises the *default* rather than an explicitly-set value — the harder and more valuable
case to get right, since a default-value regression (e.g. accidentally flipping the class's own
field initializer) would otherwise go undetected by tests that always set the flag explicitly.

### Testing
This is the direct complement of `easygl_basiceffect_vertexcolor_enabled_test.cpp` (same shader,
opposite gate state) — together the pair gives genuine two-sided coverage of
`uVertexColorEnabled`'s branch, which is exactly the right test-pairing strategy for a boolean gate.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW findings — this file is small, precisely scoped, and its single assertion
pair checks out exactly against the live shader source and FNA's default-value semantics.

## Cross-File Observations

- Forms a matched pair with `easygl_basiceffect_vertexcolor_enabled_test.cpp` against the same
  `EnsureColored3DProgram()` shader — recommended these two always be reviewed/updated together if
  the shader's vertex-color gating logic ever changes.
- `BasicEffect`'s constructor-time `DirectionalLight0.setEnabledProperty(true)` (in
  `BasicEffect.cpp`) is irrelevant here since `LightingEnabled` itself defaults to `false` and is
  never set by this test — confirmed no unintended lit-path interaction.

## Missing or Weak Tests

None specific to this file's narrow, well-defined scope.

## Positive Findings

- Deliberately leaving `VertexColorEnabled` unset (rather than explicitly `false`) is the correct
  test design to actually validate a *default value*, not just a code path when the flag is
  explicitly toggled.
- Choice of a maximally-contrasting vertex color (pure red) against a non-primary `DiffuseColor`
  (teal-ish) makes any partial leak trivially visible, not just a boundary-precision question.

## Final Assessment

A small, correct, well-targeted default-value regression test; its single pixel assertion is fully
verified against the current shader source and the referenced Task 361 default-value fix.
