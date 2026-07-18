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
  everything. SdlRenderer's and Dx3's audits both confirm the *good* counter-pattern (every unsupported method
  explicitly overridden to throw); worth checking during Pass 4 whether other backends follow that discipline or
  IGraphicsBackend's riskier default.
- **CONFIRMED LIVE BUG (not just theoretical risk): `IGraphicsBackend::RegisterForWindow`/`windowRegistry()`'s
  register-in-constructor/unregister-in-destructor convention has no protection against a constructor that
  registers early and then throws before completing.** `EasyGLGraphicsBackend`'s own audit (F1) found a concrete,
  reachable instance: `RegisterForWindow` runs before `SDL_GL_CreateContext`, which can throw. The destructor
  (which would unregister) never runs on a failed construction, leaving a dangling pointer that
  `SdlInputBridge`/`Mouse` would dereference on the next input event. **Check `Canvas`/`SdlGpu`/`WebGPU` (the
  other three `RegisterForWindow` callers) for the same ordering risk when their shards are audited** — this may
  be a systemic pattern across all four callers, not an EasyGL-specific mistake.

## Duplicated backend logic

_(pending — revisit once more backends are audited)_

## Recurring memory/resource risk patterns

_(pending)_

## Recurring performance risk patterns

_(pending)_

## Systematic FNA parity gaps

- **CONFIRMED (direct source verification): EasyGL skinned-effect shaders skip the WorldInverseTranspose normal
  transform.** First surfaced incidentally by 3 EasyGL example-test audits (`examples-tests-easygl` shard, all
  using `World=Identity` so unable to prove it), then independently confirmed by direct reading of
  `EasyGLGraphicsBackend.cpp` during the `backend-easygl` direct audit: `EnsureSkinnedProgram`/
  `EnsureSkinnedVertexLitProgram` never register or use a `uNormalMatrix` uniform at all (normal transformed only
  by the bone-skin matrix), and `EnsurePbrSkinnedProgram` uses the raw `uWorld` matrix instead of the correct
  inverse-transpose. All three are regressions relative to a correctness bar (`uNormalMatrix` =
  `transpose(inverse(world3x3))`, Task 398) this same file already meets for every *non-skinned* lit shader. See
  `AUDIT_FINDINGS_INDEX.md` HIGH/MEDIUM sections and
  `audit/src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp.audit.md` F2/F3 for full detail.

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
