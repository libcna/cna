# Audit: examples/bgfx_dualtextureeffect_null_texture0_test.cpp

## Metadata

- Source file: `examples/bgfx_dualtextureeffect_null_texture0_test.cpp` (165 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DualTextureEffect.Texture` (slot 0) null-texture
  fallback verification, Bgfx backend.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_dualtextureeffect_null_texture0 …)` /
  `cna_register_backend_test(NAME Bgfx_DualTextureEffect_NullTexture0 …)`
  (`cmake/Tests/BgfxTests.cmake:408-410`).
- XNA/FNA relevance: indirect/`NOXNA`-adjacent — null-texture-slot fallback behavior is a CNA
  backend-robustness concern (FNA's own D3D9-era `Texture=null` semantics on real hardware is a
  driver-dependent no-op or undefined-texture read; CNA's own contract, verified here, is to
  render an opaque-white fallback rather than leak a stale binding).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (dual-texture
  dispatch, `texColor3DSampler_`/slot-0 fallback, lines ~2451-2464 and ~2418-2431 for the
  vertex-color variant).

## Purpose

Verify-only test (the header comment states "zero bugs expected"): confirms that Bgfx's
dual-texture dispatch branch falls back to `defaultWhiteTexture3D_` for `Texture` (slot 0) when
null, per Task 379's general 7-call-site fix. Structured as two sequential draws: first with a
"distinctive" texture (200,20,20) bound to slot 0 to establish stale-state bait, then a second
draw with `Texture=nullptr` — asserting both that the result matches the expected white-fallback
color *and* that it does not match the first draw's distinctive color (proving no leftover binding
leak).

## Executive Verdict

**Healthy** — the production fallback code this test targets was located and confirmed present
and correct; the expected pixel value was independently re-derived and matches exactly.

## Checklist Results

### Behavioral correctness
Located the actual dispatch branch in `BgfxGraphicsBackend.cpp` (`else if (params.dualTexture &&
bgfx::isValid(dualTexture3DProgram_))`, lines 2448-2479): slot 0
(`texColor3DSampler_`) has an explicit `if (params.texture0) {...} else { bgfx::setTexture(0,
texColor3DSampler_, defaultWhiteTexture3D_, ...); }` (lines 2451-2464), confirming the fallback the
test asserts is genuinely wired, not merely claimed by the comment.
Re-derived the expected pixel: `Texture=null → white(1,1,1)` fallback, `Texture2=kTex2(80,40,120)/255
=(0.3137,0.1569,0.4706)`, `DiffuseColor` defaults to white (`DualTextureEffect.hpp:269`,
`Vector3{1,1,1}`, not overridden by this test). `base.rgb = white*2 = (2,2,2)`; `color = (2,2,2) *
(0.3137,0.1569,0.4706) * (1,1,1) = (0.6275,0.3137,0.9412) → (160.0,80.0,240.0)` — matches the
asserted `Color(160,80,240,255)` (line 138-140) exactly.

### Logic
The two-draw "distinctive previous texture" pattern (lines 118-141) specifically targets a stale
GPU-binding class of bug (a backend forgetting to rebind or clear a sampler slot between draws),
which is architecturally distinct from "does the fallback constant exist at all" — this is the
correct technique to catch a backend that silently reuses whatever was bound to unit 0 on the
*previous* draw call rather than actually applying the documented white-fallback. Confirmed via
the source read above that the current code does call `bgfx::setTexture(0, ..., defaultWhiteTexture3D_,
...)` unconditionally in the null branch (not skip the call), so this leak class is genuinely
guarded against, not just assumed.

### Robustness
Same `RasterizerState::CullNone` requirement (line 104, Task 364/884) applied consistently with
this shard's other pixel tests; justified by the same CCW-quad-winding-vs-CullCounterClockwiseFace
analysis given in `bgfx_dualtextureeffect_doubling_test.cpp`'s audit.

### Testing
Two assertions per draw pair: a positive check (matches expected fallback color) and a negative
check (`!colourMatch(got, kDistinctivePrev)`, line 141-143) — this negative check is not redundant
with the positive one in general (a backend bug could produce neither the fallback nor the stale
color, e.g. black from a crash-recovered state), so it adds real, independent verification value
rather than merely re-stating the positive assertion.

## Detailed Findings

None. The claimed fallback behavior was independently confirmed present in the current
`BgfxGraphicsBackend.cpp` source (not merely asserted by the test or its comment), and the
expected pixel value is an exact match to the current formula.

## Cross-File Observations

- This file's sibling, `bgfx_dualtextureeffect_null_texture2_test.cpp`, tests the identical
  fallback for slot 1 (`Texture2`); per that file's own header comment, slot 1's fallback did
  **not** exist prior to Task 387 (a real bug, fixed by that task) — this file's slot-0 case was
  already fixed as part of Task 379's original 7-call-site sweep, which is why this file's header
  explicitly states "zero bugs expected." This audit confirms both files' framing is accurate: slot
  0's fallback (`texColor3DSampler_`, lines 2451-2464) and slot 1's fallback
  (`texColor3DSampler2_`, lines 2465-2478) are both present in the current source, consistent with
  Task 379 fixing slot 0 first and Task 387 later closing the slot-1 gap.
- The expected pixel value `(160,80,240)` is intentionally identical to the one asserted in
  `bgfx_dualtextureeffect_null_texture2_test.cpp` — this is correct by design, not a copy/paste
  error: `(A*2)*B == A*(B*2)` for the scalar-doubling formula regardless of which of the two
  texture slots holds the fallback white and which holds the non-saturated `(80,40,120)` texture,
  so both files legitimately converge on the same expected constant.

## Missing or Weak Tests

None specific to this file. A test combining "both `Texture` and `Texture2` null simultaneously"
does not appear in this shard, but is a comparatively low-value gap given both single-slot cases
are independently verified and the fallback logic for each slot is textually independent in the
production code (no shared conditional between the two `if (bgfx::isValid(texColor3DSampler_))`/
`if (bgfx::isValid(texColor3DSampler2_))` blocks).

## Positive Findings

- The test explicitly documents (and this audit confirms) that it expects zero bugs — a rare and
  useful form of honesty distinguishing "verifying already-fixed behavior" from "discovering a new
  bug," which most of this shard's sibling null-texture/doubling files also do consistently.
- The "distinctive previous draw" technique is a genuinely stronger test design than asserting the
  fallback color alone, and both this file and its slot-1 sibling apply it correctly.

## Final Assessment

A precise, correctly-targeted regression/verification test; the specific backend fallback code
path it exercises was independently located and confirmed correct, and its expected pixel value is
an exact match to the current formula.
