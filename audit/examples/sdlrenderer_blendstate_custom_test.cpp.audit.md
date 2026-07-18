# Audit: examples/sdlrenderer_blendstate_custom_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_blendstate_custom_test.cpp` (213 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 700: custom (non-preset) `BlendState` combinations.
- File type: standalone `Game`-subclass executable, CTest-registered (`SDL_Renderer_BlendState_Custom`,
  `cmake/Tests/SdlRendererTests.cmake`).
- XNA/FNA relevance: direct — `BlendState`'s 4 individually-settable `Blend`/`BlendFunction` properties
  (`ColorSourceBlend`/`ColorDestinationBlend`/`AlphaSourceBlend`/`AlphaDestinationBlend`/`ColorBlendFunction`/
  `AlphaBlendFunction`), not just the 4 static presets.
- FNA reference: `Graphics/BlendFunction.cs` (`Subtract`: "Subtracts destination from source"), `Graphics/Blend.cs`
  (`BlendFactor`/`InverseBlendFactor`/`SourceAlphaSaturation`).
- Related production code: `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`ToSdlBlendFactor`/`ToSdlBlendOperation`/`ApplyBlendState`, lines 648-697).
- Third-party reference checked directly: `third_party/SDL/include/SDL3/SDL_blendmode.h` (`SDL_BlendOperation`
  enum doc comments, `SDL_ComposeCustomBlendMode`'s own per-driver support-matrix doc comment).

## Purpose

4 independent checks, each exercising a different facet of custom (non-preset) `BlendState` construction: (1) a
genuinely custom `(DestinationColor, Zero)` "modulate" blend composes and renders correctly; (2)/(3)
`Blend::BlendFactor`/`Blend::SourceAlphaSaturation` (2 of the 3 XNA `Blend` values with no `SDL_BlendFactor`
equivalent) throw `std::runtime_error` rather than silently misrendering; (4) `BlendFunction::Subtract` composes
and renders the correct `src - dst` result. `MakeBlendState` (lines 69-77) is a local helper working around
`BlendState`'s 4-`Blend`-argument constructor being private (reserved for the 4 static presets) by using the
public default constructor plus the 4 individual setters.

## Executive Verdict

**Healthy.** All 4 checks were independently re-derived and confirmed correct, including a direct check of the
production `ToSdlBlendFactor`/`ToSdlBlendOperation` switch statements and a direct read of the vendored SDL3
header's own doc comments — the file's own caveat about `SDL_ComposeCustomBlendMode`'s per-driver OpenGL support
matrix (checks 1-2 in the header comment, lines 26-32) is independently confirmed accurate against the actual
vendored `SDL3/SDL_blendmode.h`, not a stale or invented claim.

## Checklist Results

### API / XNA / FNA parity

`MakeBlendState`'s parameter order `(colorSrc, colorDst, alphaSrc, alphaDst)` (line 69) differs from
`BlendState`'s own private constructor's order `(name, colorSrc, alphaSrc, colorDst, alphaDst)` (per
`include/Microsoft/Xna/Framework/Graphics/BlendState.hpp` line 165) — this is unambiguous in practice since
`MakeBlendState`'s body dispatches by name via the 4 individual property setters (lines 72-75), not by position,
so there is no risk of an argument-order bug being silently introduced; still, a reader skimming only the call
sites (e.g. `MakeBlendState(Blend::DestinationColor, Blend::Zero, Blend::DestinationColor, Blend::Zero)`, line 133)
could momentarily misread it against `BlendState`'s own differently-ordered constructor if not checking the local
helper's own signature — a minor readability nit, not a defect (INFO, not a Finding).

`Blend::BlendFactor`/`Blend::InverseBlendFactor`/`Blend::SourceAlphaSaturation` are confirmed (via
`Blend.hpp`, ordinal values 10/11/12) to be exactly the 3 values `ToSdlBlendFactor`'s `default:` case throws for
(lines 662-667 of `SdlGraphicsBackend.cpp`) — this test exercises 2 of those 3 (`BlendFactor`,
`SourceAlphaSaturation`; `InverseBlendFactor` is not separately tested here, though it shares the same `default:`
branch so the risk of it behaving differently is effectively nil).

### Behavioral correctness

**Check 1 (modulate, lines 129-141):** custom `(colorSrc=DestinationColor, colorDst=Zero)` →
`dst = src*dst_bg + dst_bg*0 = src*dst_bg` per-channel. With `dst_bg=(200,0,0)`, `src=(128,255,128)`:
`R: 128*200/255≈100.4`, `G: 255*0/255=0`, `B: 128*0/255=0` → `(100,0,0)`, matching the test's own expected
`Color(100,0,0,255)` (line 140) — independently re-derived and confirmed correct, not just plausible.

**Checks 2-3 (BlendFactor/SourceAlphaSaturation throw, lines 144-176):** confirmed directly against
`ToSdlBlendFactor`'s switch (cases 0-9 explicit, `default:` throws `std::runtime_error`) — `Blend::BlendFactor`
(ordinal 10) and `Blend::SourceAlphaSaturation` (ordinal 12) both fall to `default:`, so both throw. The test's
`ThrowsExactRuntimeError`-style pattern here is a plain `try/catch (const std::runtime_error&)`, matching.

**Check 4 (Subtract, lines 183-191):** `subtractive` sets all 4 factors to `Blend::One`
(`colorSrc=colorDst=alphaSrc=alphaDst=One`) and both blend functions to `Subtract`. Per XNA's own
`BlendFunction.Subtract` semantics ("Subtracts destination from source": `srcColor*srcBlend - destColor*destBlend`)
and confirmed directly in the vendored `third_party/SDL/include/SDL3/SDL_blendmode.h` header
(`SDL_BLENDOPERATION_SUBTRACT = 0x2, /**< src - dst ... */`, line 72) — `ToSdlBlendOperation`'s case 1 maps
`BlendFunction::Subtract`→`SDL_BLENDOPERATION_SUBTRACT` correctly (i.e., `src - dst`, not the reversed
`dst - src`). With `dst_bg=(50,0,0)`, `src=(200,0,0)`, factors all `One`: `200*1 - 50*1 = 150` — matches the
test's own expected `Color(150,0,0,255)` (line 188) exactly.

### Logic

The header comment's own caveat (lines 26-32) — that SDL3's `SDL_ComposeCustomBlendMode` doc comment states plain
"opengl: Supports the `SDL_BLENDOPERATION_ADD` operation with all factors" without explicitly documenting
Subtract/RevSubtract/Min/Max support for the `opengl` driver specifically (unlike `opengles2`, which the same doc
comment does list as supporting Subtract/RevSubtract) — was independently verified by this audit by reading
`third_party/SDL/include/SDL3/SDL_blendmode.h` directly (lines 151-155): the claim is accurate, not an
overstated or invented caveat. The test's own framing ("works on this sandbox, not officially guaranteed on every
OpenGL driver/GPU combination") is therefore an honest, correctly-hedged characterization rather than either an
unfounded worry or a false "guaranteed to work everywhere" claim.

### C++ correctness

`ThrowsExactRuntimeError`-equivalent inline `try/catch` blocks (lines 146-159, 163-176) correctly catch by
`const std::runtime_error&` (not by value, avoiding slicing) — consistent with the pattern used across this
shard's other files.

### Robustness

`PresentationMode::NativeBackBuffer` correctly set (line 202), same Task 915 rationale as the rest of this batch.

### Testing

Genuinely broad coverage for a "custom BlendState" test: composes a real non-preset blend, tests both
no-SDL-equivalent-factor throw paths that matter in practice, and tests a non-Add `BlendFunction`. `InverseBlendFactor`
(the third no-equivalent value) is not separately tested, and only one non-Add `BlendFunction` (`Subtract`) is
exercised, not `ReverseSubtract`/`Max`/`Min` — reasonable scope-limiting given `ToSdlBlendOperation`'s switch is a
simple, exhaustive 1:1 table (all 5 values direct-mapped, no throw path), so the marginal risk left uncovered by
omitting the other 3 values is low.

## Detailed Findings

None at HIGH/CRITICAL severity. No MEDIUM findings. One LOW/INFO-level readability observation (`MakeBlendState`'s
non-`BlendState`-matching argument order) noted under API/XNA/FNA parity above; not elevated to a Detailed Finding
since it carries no behavioral risk (named setters make the mapping unambiguous at the point of use).

## Cross-File Observations

- Complements the 4 preset-specific test files in this batch (`additive`/`alphablend`/`opaque`/
  `nonpremultiplied_test.cpp`) by covering the *general* custom-`BlendState` path (`ApplyBlendState`'s full
  `SDL_ComposeCustomBlendMode`-based mapping) rather than only the 4 hardcoded presets — together these 6 files
  give reasonably thorough coverage of `SdlGraphicsBackend::ApplyBlendState`'s entire mapping surface (10 direct
  factors, 3 throw-on-unsupported factors, 5 direct operations).
- `InverseBlendFactor` (the one no-equivalent `Blend` value not directly tested by any file in this batch) shares
  `ToSdlBlendFactor`'s single `default:` throw branch with the two values that are tested — a future refactor that
  somehow special-cased `InverseBlendFactor` differently (e.g., accidentally giving it a real mapping while leaving
  `BlendFactor`/`SourceAlphaSaturation` unaffected) would not be caught by any test in this shard; low risk given
  the current implementation's simplicity, but worth flagging as an actual, if minor, test-coverage gap.

## Missing or Weak Tests

- `Blend::InverseBlendFactor` is never exercised by name in this shard (only inferred via the shared `default:`
  branch it takes alongside the two values that are tested) — a one-line addition mirroring the existing
  `BlendFactor`/`SourceAlphaSaturation` checks would close this gap cheaply.
- `BlendFunction::ReverseSubtract`/`Max`/`Min` are not exercised anywhere in this file (only `Subtract` is); given
  `ToSdlBlendOperation` is a simple direct switch with no conditional logic per value, this is low marginal risk
  but is nonetheless real, uncovered surface.

## Positive Findings

- All 4 checks' expected numeric constants were independently re-derived by this audit and confirmed exactly
  correct against both the current production `ToSdlBlendFactor`/`ToSdlBlendOperation` code and the vendored SDL3
  header's own documented semantics.
- The header comment's SDL3-per-driver-support caveat was independently verified against the actual vendored
  `SDL_blendmode.h` doc comment and found accurate — a good example of a claim that could easily have gone stale
  but did not.
- Correctly distinguishes "these 3 values have no SDL equivalent and must throw" from "these other blend functions
  work here but aren't universally guaranteed," rather than conflating the two different kinds of risk.

## Final Assessment

A thorough, well-verified custom-BlendState test with accurate, independently-confirmed numeric assertions and an
honest characterization of SDL3's own driver-support caveats. Two minor, low-risk test-coverage gaps
(`InverseBlendFactor`, the 3 untested `BlendFunction` values) are the only shortfalls found.
