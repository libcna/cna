# Audit: src/CNA/Internal/Backends/Canvas/CanvasSpriteBatchBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/Canvas/CanvasSpriteBatchBackend.cpp`
- Audit status: AUDITED
- Subsystem: `backend-canvas` shard
- File type: C++ implementation (379 lines)
- Related header/implementation: `include/CNA/Internal/Backends/Canvas/CanvasSpriteBatchBackend.hpp` (same shard)
- XNA/FNA relevance: implements `SpriteBatch::Draw()`'s full overload set via `ctx.drawImage()`.
- Graphics backend relevance: the SpriteBatch drawing core of the Canvas backend.
- FNA reference: the rotation/origin placement math (lines 82-92) was cross-checked conceptually against the same
  "origin maps to (destX,destY) invariant under rotation" FNA `GenerateVertexInfo` contract already verified in
  the SdlRenderer/Software backend audits.
- Main related tests: `examples-tests-canvas` (2 files, not yet audited)

## Purpose

Implements every `SpriteBatch::Draw()` overload via a single shared `DrawSprite()`/`CNA_Canvas2D_DrawSprite`
path (per the file's own doc comment, explicitly following its own prior design conclusion that no separate
per-overload backend code is needed), handling rotation/origin/scale/flip via `ctx` transform composition, tint
and premultiply-alpha correction via an exact per-pixel `getImageData`/`putImageData` pass (only when actually
needed), and Wrap/Mirror addressing for out-of-bounds source rectangles via `ctx.createPattern`.

## Executive Verdict

**Healthy.** Careful, well-verified implementation with two explicitly documented instances of a real bug found
and fixed via formal spec verification (not guessed): a Porter-Duff `'copy'`-without-clip bug that cleared the
entire canvas outside the sprite's own footprint, and a tint-compositing formula that produced a quadratic
(`A²`-style) darkening error at semi-transparent edges. Both fixes are independently plausible and consistent
with the CSS Compositing spec's documented per-pixel formulas.

## Checklist Results

### API / XNA / FNA parity
The rotate-then-scale-then-flip-about-local-center transform order (lines 82-92) correctly matches XNA/FNA's
`SpriteEffects` semantics: flip changes which source corner maps to which (unchanged) destination corner, not the
overall screen footprint — verified this is achieved by flipping about the sprite's own local center rather than
the pivot or the whole coordinate system, avoiding a footprint shift.

### Behavioral correctness
The out-of-bounds Wrap/Mirror pattern-fill path (lines 94-127) is correctly gated to only activate when
`addressU`/`addressV` isn't `(1,1)` (Clamp) — the common in-bounds case always takes the cheaper clamp-and-offset
path (lines 129-178) regardless of the nominal address mode, which is correct since Clamp/Wrap/Mirror can only
ever visibly differ once the source rectangle actually exceeds the texture bounds (verified by direct reading of
the clamp-path math, which handles any address mode identically when in-bounds).

### Logic
`ValidateAddressModeCombination` (lines 224-242, declared in the header for standalone testability) correctly
throws for exactly the three narrow gaps its own doc comment documents (mixed U/V modes, tinted+out-of-bounds,
AlphaBlend-needs-unpremultiply+out-of-bounds) and is a genuine no-op for the common Clamp/in-bounds case — real,
loud failures for genuinely unimplemented combinations rather than silently wrong output, consistent with this
codebase's established discipline.

### Memory/resource lifetime
`CanvasIdOf` (lines 216-221) correctly uses a `dynamic_cast`-based safe resolution between the two sibling
concrete texture-like classes (`CanvasTextureBackend`/`CanvasRenderTargetBackend`), explicitly citing the same
"two sibling classes, not a subclass relationship" hazard SdlRenderer's own Task 705 fix (already verified in that
backend's audit) documented — a good example of a lesson learned in one backend being deliberately applied in
another, not independently rediscovered by luck.

### C++ correctness / Performance / Thread safety / Portability / Architecture
No issues found beyond what's already noted; the per-pixel tint/unpremultiply pass (lines 140-171) is correctly
skipped entirely when neither is needed (the stated common case), avoiding unnecessary `getImageData`/
`putImageData` round-trips.

### Maintainability
379 lines, proportionate; comment quality (each design decision cites its own `plans/plan_canvas.md` task ID and, in two
cases, a specific found-and-fixed bug with the verification method) matches this codebase's highest bar.

### Robustness / Testing
Not independently assessed beyond what's covered above (queued for `examples-tests-canvas`).

## Detailed Findings

None.

## Cross-File Observations

The `CanvasIdOf` dynamic-cast pattern directly reuses a lesson from SdlRenderer's own Task 705 fix — worth noting
in `AUDIT_CROSS_CUTTING_FINDINGS.md` as a positive example of cross-backend lesson propagation (contrasted with
the fog-formula and skinned-normal-transform bugs, which are examples of the *opposite* — a defect, not a fix,
propagating across backends).

## Missing or Weak Tests

Not independently assessed (queued for `examples-tests-canvas`).

## Positive Findings

Two explicitly-documented real bugs (Porter-Duff clip, tint-formula darkening) found and fixed via formal spec
verification rather than guesswork — strong evidence of genuine correctness engineering, not just
plausible-looking code.

## Final Assessment

No issues found; a well-verified, carefully-reasoned implementation.
