# Audit: tools/xna-oracle/CnaOracleRender.cpp

## Metadata
- Source file: `tools/xna-oracle/CnaOracleRender.cpp` (899 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: C++ tool (standalone renderer, one of two halves of the oracle diff harness)
- XNA/FNA relevance: this IS the project's own FNA-comparison oracle tooling — foundational trust
  infrastructure for this entire audit's "confirmed against FNA" claims
- Main related tests: consumed by `plans/plan_dx9.md` Phase D9-A/D9-9x workflow; not a CTest itself
  (produces a PNG for `scripts/xna-diff.py` to judge, per the shard's own README)

## Purpose
Reads the same `.scene` text format `Oracle.cs` reads and renders it through CNA's real public
`Game`/`GraphicsDeviceManager`/`GraphicsDevice`/Stock-Effect API on whichever backend the binary
was built against, saving a PNG for pixel-diffing against the real-XNA render.

## Executive Verdict
Excellent, trustworthy infrastructure. The scene-parsing and effect-construction logic is a
faithful, key-for-key, default-for-default mirror of `Oracle.cs` (verified by direct side-by-side
comparison of every `else if (key == ...)` branch against `Oracle.cs`'s matching `case` — every
key name, default value, and enum mapping matches exactly). Uses CNA's real public API throughout
(`Game`, `GraphicsDeviceManager`, `BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/
`EnvironmentMapEffect`/`SkinnedEffect`, `SpriteBatch`, `Texture2D`, `TextureCube`,
`RasterizerState`) — not a shortcut through internal/backend interfaces, matching the shard's own
explicit "not the raw `ISpriteBatchBackend` interface" requirement.

## Checklist Results
- The `AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`/`BasicEffect`
  objects are all declared as `std::unique_ptr` at `Draw()`'s own top level (lines 686-690), with an
  explicit comment (lines 681-685) explaining why: `GraphicsDevice::DrawUserPrimitives()` reads
  `GpuDrawParams` from `currentEffect_`, a raw pointer `Effect::Apply()` sets, so an effect object
  scoped only to the `if`/`else` branch that constructs it would be destroyed before the shared
  `DrawUserPrimitives()` call further down reads it — this is a real, previously-found-and-fixed
  dangling-pointer bug (matching the shard's own README account of it), correctly fixed here.
- `EnvironmentMapEffect`/`SkinnedEffect`'s `LightingEnabled` explicit-interface-implementation
  carve-out (lines 735-740, 782-783) is correctly NOT called from the CNA side either, deliberately
  matching what a real game using either effect can and cannot do — even though C++ has no
  explicit-interface-implementation hiding to force this (CNA's own `setLightingEnabledProperty`
  is directly callable), the tool disciplines itself to only exercise what real XNA permits.
- `frame_++ < 1` (line 574) deliberately skips the first `Draw()` call "to give the swap chain one
  frame to settle" before capturing — a benign, already-established D3D9-specific pattern (cited as
  matching "every other D3D9 CTest in this project"), and does not affect the actual rendered
  content since the scene is static and drawn identically on frame 0 and frame 1 (no time-based
  animation exists in this tool).
- `OracleBackendName()` (lines 92-117) is a clean compile-time backend-name dispatch, correctly
  updated (per its own comment) to no longer hardcode `"D3D9"` now that this same file is built
  against multiple backends.

## Detailed Findings
None.

## Cross-File Observations
- Byte-for-byte parallel structure with `Oracle.cs` in this same shard — every scene key, default
  value, and effect-construction branch was cross-checked and found to match. This symmetry is the
  entire basis of the oracle's trustworthiness; see `Oracle.cs.audit.md` for the corresponding
  C#-side confirmation.
- `scripts/xna-diff.py` (the actual pixel-comparison script the README extensively describes,
  including its own claimed mutation-testing self-verification) is **not part of this shard's file
  list** (it lives under `scripts/`, not `tools/xna-oracle/`) and was not directly reviewed in this
  pass — flagged as a completeness gap: the diff logic itself, which is equally foundational to
  this tool's trustworthiness, should be audited wherever `scripts/*.py` tooling is in scope (not
  identified as covered by any manifest shard checked so far in this pass).

## Missing or Weak Tests
Not registered as a CTest itself (by design, per the README — it produces a PNG with no pass/fail
assertion of its own).

## Positive Findings
The dangling-pointer fix (unique_ptr effects at top scope) and the deliberate self-restriction to
not call `setLightingEnabledProperty` on `EnvironmentMapEffect`/`SkinnedEffect` (matching real XNA's
own explicit-interface-implementation restriction, even though C++ doesn't force it) are both
genuine examples of careful, XNA-fidelity-first engineering rather than convenience-first C++.

## Final Assessment
No findings. This file is trustworthy, faithful, real-API-based CNA-side oracle rendering
infrastructure.
