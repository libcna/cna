# Audit: examples/easygl_basiceffect_texture_vertexcolor_enabled_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_texture_vertexcolor_enabled_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `BasicEffect` texture+vertex-color (stride-24) pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_basiceffect_texture_vertexcolor_enabled …)` /
  `cna_register_backend_test(NAME EasyGL_BasicEffect_TextureVertexColorEnabled …)`,
  `cmake/Tests/EasyGLTests.cmake:1091-1093`).
- XNA/FNA relevance: direct — `BasicEffect.TextureEnabled=true` + `VertexColorEnabled=true`
  combination (`VSBasicTxVcNoFog`/`PSBasicTxNoFog` shader family), the stride-24
  `VertexPositionColorTexture` vertex format.
- FNA reference: `BasicEffect.cs` (shader-index selection), `HLSL/Common.fxh`
  (`ComputeCommonVSOutput`, `vout.Diffuse *= vin.Color` in the `*Vc*` shader variants).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
  (`FillGpuDrawParams()` line 71), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureColoredTextured3DProgram()` lines 2702-2766).

## Purpose

Proves the 3-way product `TextureColor * VertexColor * DiffuseColor` for the stride-24
`VertexPositionColorTexture` path — the most combinatorially complex of this shard's `BasicEffect`
color-combination tests. The file's own header comment (lines 24-38) documents a **real bug found and
fixed while writing this test**: EasyGL's `EnsureColoredTextured3DProgram()` (the stride-24 shader)
previously dropped `DiffuseColor` entirely (`FragColor=texture(...)*vColor`, no `uDiffuseColor`
uniform at all) and had no `VertexColorEnabled` gate, unlike its stride-16 (`EnsureColored3DProgram()`,
Task 364) and stride-20 (`EnsureTextured3DProgram()`, Task 366) siblings.

## Executive Verdict

**Healthy** — the claimed bug and fix are both confirmed present in the current
`EnsureColoredTextured3DProgram()` source (`uDiffuseColor`/`uVertexColorEnabled` uniforms and the
gating expression are all there), and all 7 pixel assertions (1 positive, 6 negative/discriminating)
independently check out arithmetically against the shader's actual formula. Carries the same stale
"deferred EmissiveColor gap" documentation issue as its sibling `texture_enabled` test (see F1, LOW).

## Checklist Results

### API / XNA / FNA parity
`setTextureEnabledProperty(true)`/`VertexColorEnabled = true` (line 139, a direct public-field
assignment — see Cross-File Observations)/`setDiffuseColorProperty(kDiffuse)` (lines 137-140) all
map correctly to FNA's `BasicEffect` surface for this combination.

### Behavioral correctness
Confirmed the claimed fix in `EnsureColoredTextured3DProgram()`'s fragment shader (lines 2744-2748):
```
uniform vec4 uDiffuseColor;
uniform float uVertexColorEnabled;
...
vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);
FragColor=texture(uTexture,vUV)*vc*uDiffuseColor;
```
— exactly matching the header comment's description of the fix (mirroring `EnsureColored3DProgram()`'s
Task-364 `uVertexColorEnabled` gate pattern). Numerically re-derived all 7 constants from
`kTexColor=(200,100,50)`, `kVertexColor=(150,200,100)`, `kDiffuse=(0.8,0.4,0.6)`:
- `kExpected(94,31,12)`: `(200/255)(150/255)(0.8)×255≈94.1→94`;
  `(100/255)(200/255)(0.4)×255≈31.4→31`; `(50/255)(100/255)(0.6)×255≈11.8→12`. Matches exactly.
- `kTextureDiffuseOnly(160,40,30)` (vertex color forced to white): matches the sibling
  `texture_enabled` test's own `kExpected` exactly (same texture/diffuse pair) — a good internal
  cross-check this audit exploited.
- `kVertexDiffuseOnly(120,80,60)` (texture forced to white): `(150/255)(0.8)×255=120`;
  `(200/255)(0.4)×255=80`; `(100/255)(0.6)×255=60`. Matches exactly.
- `kTextureVertexOnly(118,78,20)` (diffuse forced to 1): `(200/255)(150/255)×255≈117.6→118`;
  `(100/255)(200/255)×255≈78.4→78`; `(50/255)(100/255)×255≈19.6→20`. Matches exactly.
- `kTextureOnly`/`kVertexOnly`/`kDiffuseOnly` are the three single-input-alone references, all
  correctly distinct.
- All 6 failure-mode references are pairwise separated from `kExpected(94,31,12)` and from each other
  by margins well outside the `±8` tolerance (line 115-118) — no risk of an incorrect implementation
  accidentally passing via tolerance overlap with any of the 6 negative checks.

### Logic
Dispatch confirmed: `VertexPositionColorTexture` is stride 24, and `SelectProgram()`'s stride-24 case
(`EasyGLGraphicsBackend.cpp` line 3963) routes unambiguously to `EnsureColoredTextured3DProgram()`.

### C++ correctness
7-way discrimination (1 positive + 6 negative assertions, lines 167-184) is thorough for a 3-input
product — this is the strongest of this shard's color-combination tests in terms of raw assertion
count and is proportionate given 3 inputs have `2^3-1=7` non-trivial partial-product combinations to
rule out (the file tests all of them: texture-only, vertex-only, diffuse-only, and all three 2-of-3
partial products).

### Robustness
Same restraint as sibling tests: doesn't over-claim coverage of `Alpha`/fog interactions, which are
each other tests' dedicated scope.

### Testing
Satisfies the anti-boilerplate bar decisively — 7 distinct numeric hypotheses are ruled out, not just
"looks textured and tinted."

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Same stale "deferred EmissiveColor gap" comment as the sibling `texture_enabled` test

- Severity: LOW
- Confidence: HIGH
- Category: maintainability (stale documentation)
- Location/symbol: header comment lines 21-23 (*"NOTE (deferred, not this task's scope): same
  +EmissiveColor gap as Tasks 364-366, tracked as part of Task 369."*)
- Evidence: identical situation to `easygl_basiceffect_texture_enabled_test.cpp`'s F1 — the gap was
  closed by `e4c60e26 fix(Task 369): honor EmissiveColor in BasicEffect's no-lighting diffuse
  formula`, which post-dates this file's own Task 367
  (`ca7c8ae4 fix(Task 367): honor DiffuseColor and VertexColorEnabled in BasicEffect's
  texture+vertexcolor shader path`). Current `BasicEffect.cpp` line 71 does add `emissiveColor_` in
  the disabled-lighting branch this test exercises.
- Why it matters: same as the sibling file — purely a stale-comment issue, invisible to this test
  since it uses the default `EmissiveColor=(0,0,0)`.
- Suggested future action (not implemented by this audit): update both files' comments together in
  one documentation pass now that Task 369 is closed.

## Cross-File Observations

- `fx.VertexColorEnabled = true;` (line 139) is a **direct public-field assignment**, not a
  `setVertexColorEnabledProperty(true)` call. Checked `include/.../BasicEffect.hpp` (line 48):
  `bool VertexColorEnabled = false;` is declared as a bare public field with **no**
  `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` wrapper anywhere in the class —
  unlike `World`/`View`/`Projection` (also public fields, but *additionally* exposed via
  `getWorldProperty()`/`setWorldProperty()` overrides of `IEffectMatrices`) and unlike every other
  `IEffectLights`/`IEffectFog` member (`AmbientLightColor`, `LightingEnabled`, etc.), which are
  getter/setter pairs with no raw field at all. This means the test file has no choice but to use the
  raw-field form — it is not a test-authoring shortcut, it is the only API `BasicEffect.hpp` actually
  exposes for this property. Worth flagging in whatever shard eventually audits `BasicEffect.hpp`
  itself: this is a project-convention inconsistency (`CLAUDE.md`'s own "C# Properties → C++
  Convention" section calls for `getXProperty()`/`setXProperty()` and says "do not replace C#
  properties with public fields"), since FNA's own `BasicEffect.cs` declares `VertexColorEnabled` as
  a genuine C# property (`get`/`set` with a `dirtyFlags |= EffectDirtyFlags.ShaderIndex` side effect
  on change), not a plain field.
- Shares the `kTexColor`/`kDiffuse` numeric pair with `easygl_basiceffect_texture_enabled_test.cpp`,
  making `kTextureDiffuseOnly` here and that file's `kExpected` mutually verifiable — confirmed
  identical (160,40,30) in both files.

## Missing or Weak Tests

None — the 7-assertion design is already thorough for this 3-input combination.

## Positive Findings

- The "REAL BUG FOUND AND FIXED" narrative in the header comment is fully corroborated: the specific
  missing `uDiffuseColor`/`uVertexColorEnabled` uniforms this comment describes are present and wired
  correctly in the current `EnsureColoredTextured3DProgram()`.
- Choosing 3 distinctly-valued, non-white/non-trivial inputs (rather than any input at 1.0 or white)
  is exactly what makes all 7 partial products numerically distinguishable — deliberate, well-reasoned
  test design.
- Cross-referencing this file against the sibling `easygl_basiceffect_texture_enabled_test.cpp`'s own
  independently-derived constant provided a genuine internal consistency check during this audit.

## Final Assessment

The strongest color-combination test in this shard by assertion count and discriminating power; its
"bug found and fixed" claim is fully verified against the current shader source. Only issue is the
same stale `+EmissiveColor` documentation note shared with its sibling test file (LOW, cosmetic).
