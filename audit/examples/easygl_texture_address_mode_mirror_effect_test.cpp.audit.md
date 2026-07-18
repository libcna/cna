# Audit: examples/easygl_texture_address_mode_mirror_effect_test.cpp

## Metadata

- Source file: `examples/easygl_texture_address_mode_mirror_effect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `DualTextureEffect`/`DrawUserPrimitives`
  `TextureAddressMode::Mirror` pixel test
- File type: C++ example/integration-test executable (`TextureAddressModeMirrorEffectTest : Game`, `main()`)
- Related production code: same call chain as `easygl_texture_address_mode_clamp_effect_test.cpp` —
  `GraphicsDevice::applySamplerStatesToBackend()` (`GraphicsDevice.cpp:1592-1604`),
  `EasyGLGraphicsBackend::ApplySamplerState` (`EasyGLGraphicsBackend.cpp:2055-2139`, `Mirror→MirroredRepeat`
  branch), `DualTextureEffect`'s GLSL (`EnsureDualTextured3DProgram`, `EasyGLGraphicsBackend.cpp:3009-3070`)
- XNA/FNA relevance: `GraphicsDevice.SamplerStates[0]`, `TextureAddressMode.Mirror`, `DualTextureEffect`.
  Confirmed via `FNA/src/Graphics/States/SamplerState.cs` that no `PointMirror` static preset exists in real
  XNA/FNA (only `*Clamp`/`*Wrap` combinations), matching this file's use of a custom-constructed
  `SamplerState` instead of a static preset.
- Main related tests: this file (Task 296); direct counterpart of
  `easygl_texture_address_mode_clamp_effect_test.cpp` (Task 294, same code path, Clamp instead of Mirror);
  sibling of `easygl_texture_address_mode_mirror_test.cpp` (Task 737, Mirror via the `SpriteBatch` code path
  instead of `DrawUserPrimitives`+`Effect`).

## Purpose

Verifies `TextureAddressMode::Mirror` is honored on the `GraphicsDevice.SamplerStates[0]` →
`DrawUserPrimitives`+`DualTextureEffect` code path — a genuinely different production path from both the
`SpriteBatch`-based Mirror test and this file's own Clamp-focused sibling. Draws the same full-screen quad and
2×1 (Red|Green) `texture0` / white 1×1 `texture1` setup as the Clamp sibling, but with a custom
`Filter=Point, AddressU=AddressV=Mirror` `SamplerState`, sampling at `U=1.6` where Mirror's answer
(`RED`) genuinely differs from both Wrap's and Clamp's (`GREEN`) — the same deliberately-chosen
discriminating point already verified in the `SpriteBatch`-based Mirror sibling. Placement matches the shard
convention.

## Executive Verdict

**Healthy** — the `U=1.6` sample point and expected Red/Green split were independently re-derived and
confirmed correct for this code path, and this file correctly reuses the already-verified insight (from its
`SpriteBatch`-based sibling) that `U=1.25` would not discriminate Mirror from Clamp, choosing `U=1.6` instead.
The full `GraphicsDevice.SamplerStates[0]` sampler-state call chain and the `DualTextureEffect` shader's
`×2`-modulate semantics were independently re-verified (shared evidence with the Clamp sibling) and hold up.

## Checklist Results

### API / XNA / FNA parity
`SamplerState pointMirror; pointMirror.setFilterProperty(TextureFilter::Point);
pointMirror.setAddressUProperty(TextureAddressMode::Mirror); pointMirror.setAddressVProperty(...)` (lines
80-83) is the correct way to construct a non-preset `SamplerState` given FNA has no static `*Mirror` preset
(confirmed against `FNA/src/Graphics/States/SamplerState.cs`, same finding already made for this file's
`SpriteBatch`-based sibling). `device.getSamplerStatesProperty()[0] = pointMirror` (line 84) correctly assigns
by value (`SamplerState` has no special ownership semantics requiring move/pointer assignment here).

### Behavioral correctness — cross-checked against the already-verified Clamp-sibling call chain
This file shares its entire production call chain with
`easygl_texture_address_mode_clamp_effect_test.cpp` (verified in that file's own report):
`GraphicsDevice.SamplerStates[0]` → `applySamplerStatesToBackend()` → `EasyGLGraphicsBackend::ApplySamplerState`
→ GL sampler-object bound to texture unit 0 (matching `texture0`/`patternTex_`'s bound unit). Confirmed the
`Mirror=2 → TextureWrapMode::MirroredRepeat` branch (`EasyGLGraphicsBackend.cpp:2131`) is reached for this
file's `AddressU`/`AddressV` values, the specific mapping this test is a regression guard for.

Re-derived the expected color at `u=1.6` using the same mirror-reflection formula already verified for the
`SpriteBatch`-based Mirror sibling (`u∈[1,2) → mirrored = 2-u`): `2-1.6=0.4`, falling in the texel-0 half
`[0,0.5)` under point sampling → texel 0 → **Red** for `patternTex_` (Red|Green pattern, texel 0 = Red) —
matching this test's `isRed` check (line 114, `result_=0` on match, line 121). Confirmed Wrap and Clamp both
give texel 1 (Green) at this point (`fract(1.6)=0.6` for Wrap; clamp-to-`1.0` resolves to texel 1 for Clamp,
per the texel-center analysis already verified in the sibling reports), matching the header's claim that this
sample point discriminates Mirror from both alternatives, unlike `U=1.25` where Mirror and Clamp coincide.

As with the Clamp-focused sibling, confirmed the `DualTextureEffect` `×2`-modulate GLSL
(`base.rgb*=2.0; FragColor=base*texture(uTexture2,vUV)*uDiffuseColor;`) does not change the qualitative
Red-vs-Green determination here either: `texture1`'s white 1×1 value and the saturating write to the backbuffer
preserve the same primary-color read regardless of the transient `×2` overshoot, since the non-dominant color
channels of the sampled texel are exactly `0`.

### Logic
Same single-scene structure as the Clamp sibling: one `Clear`, one sampler assignment, one `DualTextureEffect`
setup/`Apply()`, one `DrawUserPrimitives`, one readback (lines 76-112) — no cross-case isolation concerns since
there's only one case in this file (unlike the `SpriteBatch`-based Mirror sibling, which asserts Mirror
alongside Wrap/Clamp controls in a single run — this file does not include that same three-way
cross-check, see Missing or Weak Tests).

### Memory/resource lifetime
`patternTex_`/`whiteTex_` value members, `done_` guard — identical, correct pattern to the Clamp sibling. No
lifetime concern.

### C++ correctness
`isRed`/`isGreen` threshold checks (lines 114-115) use the same `>=200`/`<=50` bounds already verified as a
reasonable, non-overlapping tolerance in the Clamp sibling.

### Performance
N/A — single-frame draw, no hot path.

### Robustness
No invalid-input path exercised; correct scope for a positive-path sampler-state test.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL findings.

### F1 — unlike its `SpriteBatch`-based Mirror sibling, this test does not cross-check that Wrap/Clamp would actually give the *other* answer at this sample point

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / self-verification strength
- Location/symbol: `Draw()` (lines 66-135); compare to `easygl_texture_address_mode_mirror_test.cpp`'s
  `SampleAtUOnePointSix` called three times with `mirrorPass && wrapIsBlue && clampIsBlue` (that file's line
  124).
- Evidence: this file only ever assigns and tests the `pointMirror` sampler; it does not additionally draw the
  same scene with `SamplerState::PointWrap`/`PointClamp` to confirm those specifically give Green at `U=1.6` in
  *this* code path (`DrawUserPrimitives`+`DualTextureEffect`), whereas its `SpriteBatch`-based sibling does
  exactly that three-way check for the `SpriteBatch` code path.
- Why it matters: a hypothetical regression that made *every* sampler mode read Red at `U=1.6` on this code
  path specifically (e.g., a bug that made `ApplySamplerState` a no-op for this call chain, always leaving
  whatever GL default wrap mode was last set) would still pass this test's single `isRed` assertion, since the
  test only checks "is the result Red," not "is the result Red *specifically because* Mirror was honored,
  distinct from what Wrap/Clamp would give here." The `SpriteBatch`-based sibling closes exactly this gap for
  its own code path; this file does not close the equivalent gap for the `DrawUserPrimitives` code path.
- FNA/XNA comparison: N/A (test-design strength, not an FNA behavior question).
- Suggested future action: add a Wrap/Clamp control draw (mirroring the `SpriteBatch` sibling's three-way
  check) to this file and its Clamp-focused counterpart, to close the same gap on this code path; not
  implemented by this audit.

## Cross-File Observations

- Shares the V-axis-never-exercised gap with the rest of this test family (see
  `easygl_texture_address_mode_test.cpp.audit.md` F1) — this quad's V coordinates also never leave `[0,1]`.
- F1 above is the mirror image of a strength already noted as a Positive Finding in the `SpriteBatch`-based
  Mirror sibling's report — worth reading together, since it shows the same underlying test idea (Mirror vs.
  Wrap vs. Clamp discrimination at `U=1.6`) was implemented more defensively in one code path's test than the
  other.
- Directly paired with `easygl_texture_address_mode_clamp_effect_test.cpp` (Task 294) — identical scene setup
  and code path, differing only in sampler mode and expected color; both were independently verified against
  the same production call chain.

## Missing or Weak Tests

- V-axis addressing is never exercised (shared gap).
- Does not cross-check Wrap/Clamp controls on this specific code path (F1) — present in the `SpriteBatch`
  sibling but not here or in the Clamp-focused counterpart.
- Same stride-20-only / cross-slot-isolation gaps already noted in the Clamp-focused sibling's report apply
  identically here (not re-derived).

## Positive Findings

- Correctly reuses the already-proven-necessary `U=1.6` discriminating sample point rather than re-deriving
  (or worse, guessing) a new one — a sign of deliberate, consistent test design across this task family.
- Correctly follows the FNA-verified absence of a `PointMirror` static preset by constructing a custom
  `SamplerState`, matching the identical, correct approach used by its `SpriteBatch`-based sibling.

## Final Assessment

A correctly-targeted test for `TextureAddressMode::Mirror` on the `DrawUserPrimitives`+`DualTextureEffect` code
path, reusing an already-proven discriminating sample point and production call chain shared with its
Clamp-focused sibling. Its one real weakness (F1) is a lower bar of self-verification than its
`SpriteBatch`-based Mirror counterpart achieves for the equivalent code path — worth closing to bring this
code path's Mirror coverage to the same strength.
