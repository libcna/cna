# Audit: examples/bgfx_environmentmapeffect_amount_zero_test.cpp

## Metadata

- Source file: `examples/bgfx_environmentmapeffect_amount_zero_test.cpp` (165 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `EnvironmentMapEffect.EnvironmentMapAmount=0` cube-map
  ignore pixel test, Bgfx backend, Task 393.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_environmentmapeffect_amount_zero …)` /
  `cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_AmountZero …)`
  (`cmake/Tests/BgfxTests.cmake:181-183`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect.EnvironmentMapAmount` (per FNA's own doc
  comment: *"If set to zero, the RGB channels of the environment map will [be] completely
  ignored"*).
- FNA reference: `HLSL/EnvironmentMapEffect.fx` `PSEnvMap`'s `lerp(color.rgb, envmap.rgb,
  pin.Specular.rgb)` at `Specular.rgb=0` degenerates to `color.rgb` unchanged.
- Related production code: same `fs_env_map3d.sc`/`EnvironmentMapEffect.cpp`/
  `BgfxGraphicsBackend.cpp` paths as the `amount_one`/`combined` siblings.

## Purpose

Single-check pixel test asserting that with a green cube map bound but `EnvironmentMapAmount=0`,
the rendered result is texture-only (the cube map's color must not leak in at all, in either the
lerp target or, since `EnvironmentMapSpecular=(0,0,0)` here, the specular-alpha term either). The
header comment explicitly notes this test **cannot discriminate** the additive-vs-lerp formula bug
that Task 394 (`amount_one_test.cpp`) fixed — at `EnvironmentMapAmount=0` both formulas coincide
(`baseColor + envColor*0 == lerp(baseColor, envColor, 0) == baseColor`) — and correctly defers that
distinction to the `amount_one` test rather than overclaiming coverage.

## Executive Verdict

**Healthy** — the single expected constant was independently re-derived and matches exactly; the
file's own scoping comments (both the additive/lerp non-discrimination note and the
readback-capability correction aimed at `bgfx_env_map_test.cpp`) were independently verified
accurate.

## Checklist Results

### Behavioral correctness
`texColor=(200,100,50)/255=(0.7843,0.3922,0.1961)`; `EmissiveColor=(0.5,0.5,0.5)`,
`DirectionalLight0`'s default diffuse is `Vector3::Zero` (`DirectionalLight.cpp:7`) even though
`Enabled=true`, and `AmbientLightColor` defaults to zero — so `litRGB = (0.5,0.5,0.5)*(1,1,1) =
(0.5,0.5,0.5)` and `baseColor = litRGB*texColor = (0.39215,0.19608,0.09804)`. With
`EnvironmentMapAmount=0` set explicitly (line 121), the Fresnel-gated `blendFactor =
pow(...)*EnvironmentMapAmount = (anything)*0 = 0` regardless of the (Identity-view) geometry's
Fresnel term — so this test's expected value does not actually depend on the same
grazing-angle-vs-head-on geometric analysis that matters for the `amount_one`/`combined` siblings,
since multiplying by `0` short-circuits it either way. `mix(baseColor, envColor, 0) = baseColor →
(100.0,50.0,25.0)` — matches the asserted `Color(100,50,25,255)` (lines 142-144) exactly.
`EnvironmentMapSpecular=(0,0,0)` (line 122) means the unconditional specular `+=` term also
contributes nothing, so the full pipeline (not just the lerp term) genuinely produces a
texture-only result, consistent with the test's own claim.

### Cross-file claim verification
This file's header comment (lines 7-9) states: *"Task 278's own note that Bgfx 'has no GPU
readback API' predates the real GetBackBufferData()-based readback established by later Bgfx tests
in this project (Tasks 379/383-389) -- this test uses real pixel verification, not a smoke test."*
This audit independently confirmed both halves of this claim: (1) `bgfx_env_map_test.cpp` (Task
278) does still contain the "no GPU readback API" comment verbatim, now stale (see that file's own
audit report, finding F1); (2) this file (Task 393) does call `dev.GetBackBufferData(&reg, &got, 0,
1)` (lines 137, within the retry loop at 128-140) and assert against a real pixel value, not a
no-crash check. This is a rare case in this shard of one file's comment making a falsifiable claim
about a *different* file, and it holds up under independent verification.

### Testing
The single assertion is precise and correctly scoped; the file's own comment about what it does
*not* discriminate (additive-vs-lerp formula) is accurate and appropriately narrow, deferring that
question to `amount_one_test.cpp` rather than silently under-claiming or over-claiming coverage.

## Detailed Findings

None. The single expected constant is an exact re-derivation from the current production formula,
and this file's unusually self-referential cross-file claims (about both its own scope limits and
another file's stale rationale) were independently verified rather than assumed.

## Cross-File Observations

- See `bgfx_env_map_test.cpp`'s audit report (finding F1) — this file's own comment is the
  first-party source that alerted this audit to that finding, and this audit's independent
  verification corroborates rather than merely repeats it.
- Together with `bgfx_environmentmapeffect_amount_one_test.cpp`, these two files form a matched
  pair (`Amount=0` / `Amount=1`) that jointly, but not individually, confirm the lerp (not
  additive) formula — this file alone would still pass under the pre-fix additive formula, which is
  exactly what its own comment says.

## Missing or Weak Tests

None specific to this file's own stated scope. See `bgfx_env_map_test.cpp`'s audit report (F2) for
the one related gap this shard still has (specular contribution at `EnvironmentMapAmount=0` —
this file itself uses `EnvironmentMapSpecular=(0,0,0)`, so it does not fill that gap either, though
it does correctly confirm the *lerp* term is silenced at `Amount=0`, which is a distinct question
from whether a *nonzero* specular term is correctly left un-gated by `Amount`).

## Positive Findings

- Genuinely transparent, accurate self-scoping: explicitly disclaims discriminating power over the
  additive/lerp bug rather than implying broader coverage than it has.
- Its claim about a sibling file's stale rationale was independently verified true, which is a
  useful, unusually specific form of cross-file documentation accuracy for this shard.

## Final Assessment

A precise, honestly-scoped test whose single expected pixel value is exactly correct and whose
unusual self-referential claims about both its own limitations and another file's staleness both
independently check out.
