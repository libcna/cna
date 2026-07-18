# Audit: examples/bgfx_blendstate_blendfactor_test.cpp

## Metadata

- Source file: `examples/bgfx_blendstate_blendfactor_test.cpp` (129 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `GraphicsDevice.BlendFactor` propagation from
  `BlendState.BlendFactor` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_blendstate_blendfactor …)` /
  `cna_register_backend_test(NAME Bgfx_BlendState_BlendFactor …)`,
  `cmake/Tests/BgfxTests.cmake:146-148`).
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.Blend.BlendFactor`,
  `BlendState.BlendFactor`, `GraphicsDevice.BlendFactor`.
- FNA reference: `src/Graphics/GraphicsDevice.cs:116-126` (`BlendState` setter is a plain field
  store, `nextBlend = value`), `GraphicsDevice.cs:1573-1580` (`ApplyState()`'s
  `FNA3D_SetBlendState(GLDevice, ref nextBlend.state)` call — the native blend-state struct
  FNA3D applies atomically includes the blend-constant colour, matching this project's own
  `setBlendStateProperty` design).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp:1667-1682`
  (`setBlendStateProperty`, explicitly re-forwards `value.getBlendFactorProperty()` at line 1681),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp:1941-1955` (`SetBlendFactor`, packs into
  `blendFactorPacked_`, later consumed by `bgfx::setState(..., blendFactorPacked_)`).

## Purpose

Builds a custom `BlendState` (`ColorSourceBlend=Blend::BlendFactor`, `ColorDestinationBlend=
Blend::Zero`, with `BlendFactor` baked into the state object as `(200,100,0,255)`), calls **only**
`GraphicsDevice.setBlendStateProperty` — deliberately never calling `setBlendFactorProperty`
separately — and asserts the rendered output already reflects the state's own baked-in blend-constant
colour. This is a genuine, specifically-targeted regression test for one exact propagation path (state
→ device), not a general `BlendFactor` correctness test.

## Executive Verdict

**Healthy** — the propagation path this test exercises was independently traced end-to-end
(`BlendState::setBlendFactorProperty` → `GraphicsDevice::setBlendStateProperty`'s explicit re-forward
call at line 1681 → `BgfxGraphicsBackend::SetBlendFactor` → `blendFactorPacked_` → `bgfx::setState`)
and found genuinely wired, matching this project's own documented rationale for mirroring FNA's
atomic `FNA3D_SetBlendState` semantics.

## Checklist Results

### API / XNA / FNA parity
`Blend::BlendFactor` (ordinal 10 per `Blend.hpp`) and `Blend::Zero` (ordinal 1) map correctly through
`XnaBlendToBgfxFactor` (`BgfxGraphicsBackend.cpp:1551`, `case 10: return BGFX_STATE_BLEND_FACTOR`;
line 1542, `case 1: return BGFX_STATE_BLEND_ZERO`). `state.setBlendFactorProperty(Color(200,100,0,255))`
(line 67) matches `BlendState.hpp`'s real setter naming convention.

### Behavioral correctness
Re-derived: `ColorSourceBlend=BlendFactor` scales the fully-white source `(255,255,255,255)` by the
device's active blend-constant `(200,100,0,255)/255`; `ColorDestinationBlend=Zero` discards the black
background entirely. Expected output: `R=255*(200/255)=200`, `G=255*(100/255)=100`, `B=0` — matches
the tolerance bands (`R∈[185,215]`, `G∈[85,115]`, `B≤15`, lines 96-98) with a comfortable ±15 margin
each. Confirmed `AlphaSourceBlend=One`/`AlphaDestinationBlend=Zero` (lines 65-66) are set so the alpha
channel's own blend doesn't need `BlendFactor`'s alpha component, keeping the test focused purely on
the colour-channel propagation.

### Logic
The critical design point — *no* separate `dev.setBlendFactorProperty(...)` call anywhere in this
file (confirmed by reading the entire file: the only `BlendFactor`-related call is
`state.setBlendFactorProperty(...)` on the *state object*, line 67, never on `dev` directly) — is
exactly what makes this discriminating for the specific bug class it targets: a
`setBlendStateProperty` implementation that updates the 6 blend factors/functions but forgets to also
apply the state's own baked-in `BlendFactor` colour. Confirmed this exact propagation line exists in
current production code (`GraphicsDevice.cpp:1681`, with an explanatory comment directly referencing
FNA's atomic-apply semantics) — this is not a hypothetical risk the test guards against speculatively;
it is a real, correctly-implemented behavior the test verifies is still present.

### C++ correctness
No lifetime concerns; `state` (a local `BlendState`) is fully consumed by
`dev.setBlendStateProperty(state)` (which copies it into `blendState_`, confirmed at
`GraphicsDevice.cpp:1669`) before going out of scope.

### Robustness
Same `DepthStencilState`-based depth-disable substitution pattern as this batch's other files
(lines 58-60), for the same confirmed reason (`SetDepthTestEnabled` throws on Bgfx).

### Testing
Single, narrowly-targeted assertion is appropriate for this file's specific, narrow claim (state→device
`BlendFactor` propagation) — broader `BlendFactor`-as-a-blend-input correctness is implicitly also
exercised (the whole equation must compute correctly for the assertion to pass), but this file does
not claim to test factor/function combinations beyond this one scenario, and correctly doesn't
over-claim in its own header comment.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings.

## Cross-File Observations

- This is the one file in this batch that does **not** use `BasicEffect.DiffuseColor`/`VertexColor`
  to shape its source colour — it uses a plain opaque white source and lets the blend factor alone
  produce the expected output, which is the right simplification for isolating exactly the property
  under test (state→device `BlendFactor` propagation) from any `BasicEffect`-side colour math.
- Shares the `RasterizerState::CullNone` workaround (line 89, "Task 896 finding") and the same quad
  vertex ordering as every other file in this batch — independently re-confirmed CCW via the same
  cross-product check used for the `Additive`/`AlphaBlend` siblings.
- Cross-referenced against `GraphicsDevice.cpp`'s own comment (lines 1678-1680) explaining *why*
  `setBlendStateProperty` re-forwards `BlendFactor`: it correctly cites FNA's own atomic
  `FNA3D_SetBlendState` behavior as the reference semantic being mirrored, not an arbitrary CNA
  design choice — independently corroborated by reading `GraphicsDevice.cs:1573-1580`.

## Missing or Weak Tests

None identified for this file's stated scope. A worthwhile *future* addition (not a defect in this
file) would be a complementary test proving the reverse direction — that an explicit
`dev.setBlendFactorProperty(...)` call survives a *subsequent* `setBlendStateProperty` call whose own
state object has a *different* baked-in `BlendFactor` (i.e., confirming the state's value wins, per
this file's own tested direction, without a separate test for the ordering reversed) — but this is
outside this file's stated, narrower scope.

## Positive Findings

- Explicitly calls out its own design intent in the header comment (line 16: *"Deliberately no
  separate dev.setBlendFactorProperty(...) call"*) and again inline at the call site (lines 69-70) —
  this kind of self-documenting negative-space test design is easy to misread as an oversight without
  the comment, and the comment correctly prevents that misreading.
- The `[INFO]` diagnostic on failure (lines 104-105) correctly names the exact defect class a failure
  would indicate.

## Final Assessment

A small, well-targeted regression test with a clear, narrow, and correctly-verified claim. No defects
found in the test file or in the `BlendFactor` propagation path it exercises.
