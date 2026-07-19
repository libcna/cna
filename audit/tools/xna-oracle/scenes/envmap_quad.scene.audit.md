# Audit: tools/xna-oracle/scenes/envmap_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/envmap_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `EnvironmentMapEffect` baseline scene, "non-fresnel bucket" case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises `EnvironmentMapEffect`'s base reflective-cubemap shading path with `fresnelfactor=0`
explicitly set.

## Executive Verdict
Correct as currently written, but this scene has real, documented history worth being aware of: it
previously omitted `fresnelfactor` entirely, meaning it was *actually* exercising the
Fresnel-ENABLED shader bucket the whole time (real XNA's `EnvironmentMapEffect` constructor defaults
`FresnelFactor=1`), despite the header comment claiming to test the non-Fresnel bucket. This went
undetected because the scene's coplanar-quad/`EyePosition` geometry makes the Fresnel-enabled and
-disabled paths produce numerically identical output — a real, subtle self-consistency bug in the
fixture, not in production CNA code. Verified this scene's current content includes an explicit
`fresnelfactor=0` line, confirming the documented fix is present.

## Checklist Results
- `fresnelfactor=0` is present and explicit (verified in the current file content) — the historical
  gap (relying on an unstated default) is closed.
- All other keys match `README.md`'s documented table.
- Confirmed pixel-perfect per `README.md`'s status log (both before and after the fix, since the
  two code paths are numerically identical for this geometry — the fix corrects what the scene
  documents itself as testing, not its pixel output).

## Detailed Findings
None currently open. (Historical: see Executive Verdict — already fixed.)

## Cross-File Observations
Complements `envmap_fresnel_quad.scene` (the Fresnel-ENABLED counterpart with non-degenerate
geometry actually capable of showing a visible difference) and `envmap_specular_quad.scene`.
Together these three scenes are the basis of `README.md`'s claim that `EnvironmentMapEffect`'s
Fresnel and specular paths are both covered.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
The project's own transparency in documenting and fixing its own fixture's prior
documentation/testing mismatch (rather than silently leaving it) is a positive sign of rigor.

## Final Assessment
No open findings. Historical fixture-documentation bug already fixed and verified present in
current content.
