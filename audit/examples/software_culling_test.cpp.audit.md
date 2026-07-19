# Audit: examples/software_culling_test.cpp

## Metadata
- Source file: `examples/software_culling_test.cpp` (165 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-tests-software` shard
- File type: standalone backend integration-test executable (`Game` subclass)
- XNA/FNA relevance: exercises `RasterizerState`/`GraphicsDevice::DrawPrimitives` (public XNA API)
  against the Software backend's backface-culling implementation

## Purpose
Empirically verifies backface culling: real XNA/FNA's default `CullCounterClockwise` state keeps a
clockwise-as-displayed triangle visible and discards a counter-clockwise one, `CullClockwise`
inverts that, and `CullNone` disables culling entirely — each checked against the actual rendered
pixel, not derived from first principles alone.

## Executive Verdict
Correct, and the header comment explicitly states the project's own methodology preference: "This is
not derived from first principles here — it is checked empirically against the actual rendered
pixel, per this project's own established rigor around winding/orientation-sensitive code." This is
a sound practice specifically for orientation/winding logic, which this audit has repeatedly found to
be an easy category to get subtly wrong (e.g. the confirmed EffectParameter Matrix transpose
inversion, the SkinnedEffect ambientColor/emissiveColor consumption mismatches).

## Checklist Results
- Checks A/B and C/D form two exact mirror-pairs (default state vs. `CullClockwise`, same two
  triangles), directly demonstrating the cull state inverts which winding survives — a real,
  reciprocal proof rather than two independent assertions that happen to agree.
- Check E (`CullNone`) specifically re-uses the SAME triangle Check B proved was culled under the
  default state, now confirming it renders — a genuine before/after contrast for the exact same
  input, isolating "culling is now off" from "this triangle happens to render for some unrelated
  reason."
- The centroid pixel position (21,21) is derived precisely from the actual triangle corners
  (0,0)/(64,0)/(0,64) rather than asserted as a round/convenient number.

## Detailed Findings
None.

## Cross-File Observations
Explicitly cross-referenced by `software_clipping_test.cpp`'s own `RasterizerState::CullNone`
choice (audited in the same batch), which cites this file's SOFTWARE-81 concern by name as the
reason it deliberately isolates itself from any winding/culling interaction.

## Missing or Weak Tests
None identified for this file's stated scope.

## Positive Findings
The explicit methodological statement — checking orientation-sensitive behavior empirically against
real rendered pixels rather than only reasoning about it — reflects genuine, hard-won engineering
judgment about where this class of bug tends to hide, consistent with actual confirmed
winding/orientation defects found elsewhere in this audit.

## Final Assessment
No findings.
