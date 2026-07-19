# Audit: scripts/verify-d3d9-stock-effects-vendored.sh

## Metadata
- Source file: `scripts/verify-d3d9-stock-effects-vendored.sh` (49 lines)
- Audit status: AUDITED (full read)
- Subsystem: `scripts` shard
- File type: Bash script (byte-identity enforcement check)
- XNA/FNA relevance: directly enforces that CNA's vendored Microsoft XNA 4.0 Stock Effects HLSL sources stay byte-identical to the FNA reference tree
- Main related tests: not a CTest itself; a manually-invoked (or CI-invocable) verification tool

## Purpose
Mechanically enforces "not one line edited" for the 10 vendored Stock Effects HLSL/FXH files
(`BasicEffect.fx`, `AlphaTestEffect.fx`, `DualTextureEffect.fx`, `EnvironmentMapEffect.fx`,
`SkinnedEffect.fx`, `SpriteEffect.fx`, `Macros.fxh`, `Common.fxh`, `Lighting.fxh`,
`Structures.fxh`), diffing each against the local FNA reference tree.

## Executive Verdict
Correct and exactly matches its own stated purpose — this is a genuinely valuable enforcement
mechanism (not just a comment/convention) for a design decision (vendored files must stay
byte-identical to FNA) that would otherwise be trivially and silently violated by a well-intentioned
future edit.

## Checklist Results
- Defaults to this project's own documented FNA reference tree location (per CLAUDE.md's "Source
  Reference"), with an override via argument or `CNA_FNA_TREE` env var for a different machine.
- Missing source directory fails clearly with the exact expected path and remediation guidance.
- Each file is checked individually (missing vs. mismatched are distinct failure messages), with
  a real `diff -u` printed for any mismatch rather than just "files differ."
- Exit code correctly aggregates across all 10 files (any single failure fails the whole run) while
  still reporting every file's individual status, not stopping at the first failure.

## Detailed Findings
None.

## Cross-File Observations
None directly, though this enforces the same "vendored third-party content must not silently
drift" discipline seen elsewhere in this project (e.g. the SDL/SDL_image/SDL_mixer vendoring
conventions in `cmake/ThirdPartySDL.cmake`, audited in the `build-cmake` shard).

## Missing or Weak Tests
N/A (this IS the verification mechanism, not itself under test) — though it is worth noting this
script is not itself registered as a CTest (unlike, e.g., `D3D9_XNA_Diff`), so a drift would only
be caught when a developer or CI step explicitly runs it, not automatically on every build.

## Positive Findings
A real, mechanical enforcement of a design decision that would otherwise depend entirely on
developer discipline/code review to catch a silent drift.

## Final Assessment
No findings.
