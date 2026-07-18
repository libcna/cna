# Audit: examples/bgfx_env_map_test.cpp

## Metadata

- Source file: `examples/bgfx_env_map_test.cpp` (181 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `EnvironmentMapEffect` smoke test (no-crash only, no
  pixel verification), Task 278.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_env_map …)` /
  `cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_Smoke …)`
  (`cmake/Tests/BgfxTests.cmake:175-177`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect.EmissiveColor`/`EnvironmentMapAmount`/
  `EnvironmentMapSpecular`/`EnvironmentMap`.
- FNA reference: `HLSL/EnvironmentMapEffect.fx` (`PSEnvMap`/`PSEnvMapSpecular`).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`envMap3DProgram_` dispatch, lines 2636-2693), `src/CNA/Internal/Backends/Bgfx/shaders/
  fs_env_map3d.sc`/`vs_env_map3d.sc`.
- First authored: commit `240bbab5`/`41f8fc82` ("fix(Task 278): Bgfx EnvironmentMapEffect had no
  cube-map reflection path"), 2026-07-03; last touched by `b6a00bc6` ("fix(Task 896)"),
  2026-07-07.

## Purpose

Smoke test exercising 4 `EnvironmentMapEffect` configurations (varying `EmissiveColor`,
`EnvironmentMapAmount`, `EnvironmentMapSpecular`, and the cube map itself) across 3 frames,
asserting only that no exception is thrown. Written to close a genuine Task 278 gap where Bgfx's
`DrawPrimitivesEx` had no code path for `GpuDrawParams::envMapping` at all (drawing with
`EnvironmentMapEffect` silently fell back to the plain lit-textured shader — no crash, no
reflection). The file's own comment states this is *necessarily* a smoke test because "Bgfx has no
GPU readback API in this project."

## Executive Verdict

**Needs attention** — the production `envMap3DProgram_` path this file exercises was independently
verified correct (matches FNA's `PSEnvMapSpecular` formula, confirmed via
`bgfx_environmentmapeffect_*` sibling tests' exact pixel-level derivations), but this file's own
justification for remaining smoke-test-only is now factually stale, and one of its 4 configurations
(`EnvironmentMapSpecular` contribution at `EnvironmentMapAmount=0`) is left with no pixel-level
verification anywhere in this shard.

## Checklist Results

### Behavioral correctness
The 4 configurations (lines 107-147) all call `fx.Apply()` before `DrawUserPrimitives` (no missing
apply-before-draw bug), and each sets `World`/`View`/`Projection` to Identity via `setupBase`
(lines 97-105). No logic defect found in the test's own control flow; the `try`/`catch` around the
whole `Draw()` body (lines 69-155) correctly converts any thrown exception into a `[FAIL]` with the
exception message, so the "no crash" contract is genuinely enforced, not merely assumed.

### Robustness / Maintainability — stale rationale (see F1, F2)
Both flagged findings below concern comments whose factual basis has since changed without the
file being revisited, which is exactly the class of issue this audit was asked to independently
re-verify rather than trust.

### Testing
See F1/F2 and "Missing or Weak Tests" — the smoke-test scope, while a legitimate historical
starting point, has not kept pace with capabilities added by later tasks in this same shard.

## Detailed Findings

### F1 — "Bgfx has no GPU readback API in this project" is stale; contradicted by 6 sibling files in this same shard

- Severity: MEDIUM
- Confidence: HIGH (directly contradicted by working code in this same shard, and the staleness is
  independently self-flagged by a *different* file's comment)
- Category: documentation-accuracy / stale-comment
- Location/symbol: header comment, lines 10-14: *"Bgfx has no GPU readback API in this project (see
  bgfx_render_target_usage_test.cpp), so unlike easygl_env_map_test.cpp / vulkan_env_map_test.cpp
  this cannot pixel-verify the reflection colour."*
- Evidence: every other file in this batch —
  `bgfx_dualtextureeffect_doubling_test.cpp`, `_fog_test.cpp`, `_null_texture0_test.cpp`,
  `_null_texture2_test.cpp`, `bgfx_environmentmapeffect_amount_one_test.cpp`,
  `_amount_zero_test.cpp`, `_combined_test.cpp` — call `dev.GetBackBufferData(&reg, &px, 0, 1)` to
  pixel-verify Bgfx rendering output, several of them (Tasks 383-399) testing the *exact same*
  `EnvironmentMapEffect`/`envMap3DProgram_` production code this file only smoke-tests.
  `bgfx_environmentmapeffect_amount_zero_test.cpp`'s own header comment (Task 393) independently
  confirms this: *"Task 278's own note that Bgfx 'has no GPU readback API' predates the real
  GetBackBufferData()-based readback established by later Bgfx tests in this project (Tasks
  379/383-389)."* Git log confirms the timeline: this file was authored 2026-07-03 (`240bbab5`),
  before the `GetBackBufferData`-based tests (Task 379 onward) were added.
- Why it matters: this file's stated *reason* for remaining a smoke test is no longer true, yet the
  file itself was touched again after that (Task 896, `b6a00bc6`, 2026-07-07) without being
  upgraded to pixel verification or having the comment corrected. A future reader who trusts this
  comment (rather than checking, as this audit did) would conclude Bgfx pixel verification is
  categorically impossible, when in fact 6 sibling files in the very same directory already do it.
- FNA/XNA comparison: N/A — documentation-accuracy issue, not an XNA/FNA behavior question.
- Related files: `bgfx_environmentmapeffect_amount_zero_test.cpp` (already correctly notes this
  staleness for the reader, but only in its *own* file, not by correcting this one).
- Suggested future action (not implemented by this audit): either delete the now-incorrect
  "cannot pixel-verify" claim, or replace this file's remaining configurations with real
  `GetBackBufferData` assertions the way Tasks 393/394/399 already did for the other 3
  configurations.

### F2 — smoke test leaves `EnvironmentMapSpecular` contribution at `EnvironmentMapAmount=0` (config c) with no pixel-level verification anywhere in this shard

- Severity: MEDIUM
- Confidence: MEDIUM (based on cross-referencing this shard's other `EnvironmentMapEffect` tests'
  actual parameter combinations, not a runtime observation)
- Category: test-coverage
- Location/symbol: config (c), lines 127-136: `EmissiveColor=0`, `EnvironmentMapAmount=0`,
  `EnvironmentMapSpecular=blue`.
- Evidence: per FNA's `HLSL/EnvironmentMapEffect.fx` `PSEnvMapSpecular`, the specular-alpha
  contribution (`color.rgb += EnvironmentMapSpecular * envmap.a`) is *unconditional* — added
  regardless of the `EnvironmentMapAmount`-gated lerp term — and this project's own
  `fs_env_map3d.sc` mirrors that exactly (`rgb = mix(baseColor, envSample.xyz*combinedAlpha,
  blendFactor) + u_envMapSpecular.xyz * envSample.w * combinedAlpha;`, unconditional `+=` outside
  the `mix`). This audit checked the other 3 pixel-verified files in this shard
  (`amount_one_test.cpp` uses `EnvironmentMapSpecular=(0,0,0)`; `amount_zero_test.cpp` uses
  `EnvironmentMapSpecular=(0,0,0)`; `combined_test.cpp` uses `EnvironmentMapAmount=1` together
  with `EnvironmentMapSpecular=(0.4,0.4,0.4)`) — none of them exercises non-zero
  `EnvironmentMapSpecular` *together with* `EnvironmentMapAmount=0`, which is exactly this smoke
  test's config (c). This specific combination (specular contribution firing independently of the
  amount-gated lerp) is therefore only ever smoke-tested (no crash), never pixel-verified, anywhere
  in this shard.
- Why it matters: a regression that accidentally gated the specular `+=` term behind the same
  `blendFactor`/`EnvironmentMapAmount` check as the lerp (an easy mistake — the shader's own
  structure puts both in the same expression) would silently zero out `EnvironmentMapSpecular` at
  `EnvironmentMapAmount=0` and this smoke test would still print `[PASS] ... N frames drawn without
  crash`, since a wrong-but-non-crashing pixel value is invisible to a smoke test.
- FNA/XNA comparison: FNA's `EnvironmentMapSpecular` doc comment (`EnvironmentMapEffect.cs`)
  explicitly states this alpha-based specular contribution is meant to work independently of
  `EnvironmentMapAmount` ("can be used to implement cheap specular lighting ... by encoding one or
  more specular highlight patterns into the environment map alpha channel") — i.e., this
  independence is a real, named XNA behavior, not an incidental implementation detail.
- Related files: `bgfx_environmentmapeffect_combined_test.cpp` (closest existing pixel-verified
  case, but always with `EnvironmentMapAmount=1`, not `0`).
- Suggested future action (not implemented by this audit): add a pixel-verified case (either a new
  file or an extra check in an existing one) asserting the exact specular-alpha contribution at
  `EnvironmentMapAmount=0`.

## Cross-File Observations

- Also present but not independently re-flagged as a separate finding: the comment "Note:
  SetDepthTestEnabled/setBlendStateProperty are not exercised here — the Bgfx backend does not yet
  wire 3D depth/blend state changes" (lines 71-72) reads as a stale/imprecise claim by the same
  measure as F1 — `GraphicsDevice::setBlendStateProperty`/`setDepthStencilStateProperty` do call
  `backend_->ApplyBlendState`/`ApplyDepthStencilState` (`GraphicsDevice.cpp:1671`/`1690`), which set
  `blendFlags_`/`depthFlags_` that are folded into every 3D draw's `state` bitmask
  (`BgfxGraphicsBackend.cpp:2400-2408`), and this wiring is present in the codebase well before
  Task 278 (`git log -S "blendFlags_ | depthFlags_"` shows it as far back as the "Tasks 65,80-84"
  and "Tasks 69-71,73" commits). This audit treats it as corroborating context for F1 rather than a
  separate finding, since (a) it does not affect this file's own assertions (which never touch
  blend/depth state), and (b) a genuine, narrower depth-write bug *did* exist until Task 758/759
  (`c2d11183`, 2026-07-09 — after this file's last edit), so the comment may have been accurate in
  spirit (a related depth-write no-op bug was real) even if imprecisely worded as "not wired at
  all."
- The underlying `envMap3DProgram_` shader logic this file exercises was independently confirmed
  correct against FNA by this audit via the sibling `amount_one`/`amount_zero`/`combined` tests'
  exact re-derivations — so despite F1/F2, there is no evidence of an actual rendering defect in
  the code this file smoke-tests, only in the completeness of its own verification.

## Missing or Weak Tests

See F2. Additionally, none of the 4 configurations vary `World`/`View`/`Projection` away from
Identity (unlike `bgfx_environmentmapeffect_combined_test.cpp`, which specifically stresses a
non-identity `World`/perspective `Projection`), so this file's smoke coverage of the
`WorldInverseTranspose`/`EyePosition` derivation path is comparatively weak — though that path is
independently pixel-verified by the `combined` test, softening this gap.

## Positive Findings

- The `try`/`catch` wrapping is a genuinely effective no-crash contract, not a token gesture — it
  converts a thrown exception into a `[FAIL]` with the actual exception text, so a real crash-class
  regression in `envMap3DProgram_` setup would still be caught by this file even without pixel
  verification.
- The file's original purpose (closing a total code-path gap where `envMapping` was silently
  ignored) was independently confirmed fixed: `envMap3DProgram_` and its dispatch branch are
  present and correctly wired in the current `BgfxGraphicsBackend.cpp`.

## Final Assessment

The underlying feature this file exercises is confirmed correctly implemented (independently
verified via sibling pixel tests), but the file's own stated rationale for staying smoke-test-only
is demonstrably out of date, and it is the sole remaining coverage (smoke-only) for one real,
FNA-documented behavior — `EnvironmentMapSpecular` firing independently of
`EnvironmentMapAmount=0` — that a silent regression could break without any test in this shard
noticing.
