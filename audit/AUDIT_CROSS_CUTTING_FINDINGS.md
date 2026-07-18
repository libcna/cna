# AUDIT_CROSS_CUTTING_FINDINGS.md

**Status: SKELETON — populated incrementally during Pass 2 as patterns spanning multiple files emerge, finalized
in Pass 5.**

Each entry references the per-file audit reports that provide evidence rather than restating their detail.
Organize by category as entries accumulate.

## Known pre-existing issue to actively cross-check (from `known_bugs.md`, consulted as secondary context per D-3)

- "Multiple SpriteBatch Begin/End in one frame discards all but the last" — check whether this is still reproducible
  against current `SpriteBatch` source, which backend(s) it affects, and whether it's backend-specific or a shared
  `Microsoft::Xna::Framework::Graphics::SpriteBatch` logic bug. Link the corresponding per-file finding here once
  the `xna-graphics` / `tests-xna-graphics` shards are audited.

## Architecture

- **Silent-default-degradation risk in `IGraphicsBackend`** (see `include/CNA/Internal/Backends/Common/
  IGraphicsBackend.hpp.audit.md` F1): most optional 3D-state/effect-parameter methods default to a silent no-op
  or colored-fallback rather than a negotiable capability, with `SupportsCapability()` defaulting to `true` for
  everything. SdlRenderer's audit confirms the *good* counter-pattern (every unsupported method explicitly
  overridden to throw); worth checking during Pass 4 whether other backends follow SdlRenderer's discipline or
  IGraphicsBackend's riskier default.

## Duplicated backend logic

_(pending — revisit once more backends are audited)_

## Recurring memory/resource risk patterns

_(pending)_

## Recurring performance risk patterns

_(pending)_

## Systematic FNA parity gaps

- **EasyGL skinned-effect shaders skip the WorldInverseTranspose normal transform** (SkinnedEffect's
  `EnsureSkinnedProgram`/`EnsureSkinnedVertexLitProgram` and SkinnedPbrEffect's `EnsurePbrSkinnedProgram`) —
  discovered incidentally while auditing 3 EasyGL example tests (`examples-tests-easygl` shard), all of which use
  `World=Identity` and so cannot detect it. This is a genuine, uncorroborated-by-any-existing-test FNA parity gap
  in production code (any game applying a non-uniform-scale or rotated World transform to a skinned model would
  get incorrectly-lit normals). **High priority to verify directly when `backend-easygl`'s own shard is audited**
  (not yet reached). See `AUDIT_FINDINGS_INDEX.md` HIGH section for the three test reports that surfaced this.
  (see also — pending — Pass 3 in AUDIT_PROGRESS.md)_

## Recurring testing gaps

- **Documentation rot: header comments describing "known bugs"/"current limitations" are not revisited once the
  underlying code is fixed.** Found repeatedly in the `examples-tests-easygl` batch (218 files) — at least 6
  distinct files carry stale bug/limitation claims contradicted by since-closed tasks (Vulkan blend state "almost
  entirely fake," `SetReferenceStencil` claimed universally missing, anisotropic filtering bugs claimed open,
  `EnvironmentMapEffect`'s pre-fix shader formula documented instead of the current one, `GetData()` claimed
  unimplemented). None of these are currently-live production bugs — the underlying code was actually fixed in
  each case — but the stale comments actively mislead a future reader (including future audit passes) into
  believing a fixed issue is still open, or vice versa risk under-trusting a test that's actually fine. Recommend
  (not implemented by this audit) a periodic sweep specifically for "Task NNN"/"known bug"/"currently broken"-style
  comments cross-checked against `git log`/current source, independent of any one file's own audit.
- **Tests asserting metadata/capacity instead of actual data content or actual code-path execution**: a recurring
  shape across the EasyGL example-test shard — `easygl_vertexbuffer_setdata_test.cpp` (capacity getters only, never
  checks uploaded bytes), `easygl_dynamic_buffer_stress_test.cpp` (index-buffer half never actually draws
  indexed), `easygl_msaa_test.cpp` (scene can't distinguish MSAA-resolved from never-engaged). Worth watching for
  the same shape in other backends' example-test shards during Pass 2.

## Build-system inconsistencies

_(pending)_
