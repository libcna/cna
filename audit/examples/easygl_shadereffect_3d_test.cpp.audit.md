# Audit: examples/easygl_shadereffect_3d_test.cpp

## Metadata

- Source file: `examples/easygl_shadereffect_3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 1079: `ShaderEffect` driving a real 3D
  `GraphicsDevice::DrawIndexedPrimitives()` call (not just `SpriteBatch`)
- File type: raw `Game`-derived executable, manual PASS/FAIL via `printf`/exit code, two checks (A/B)
- XNA/FNA relevance: exercises `IEffectMatrices` (`World`/`View`/`Projection`), the same interface every stock
  effect (`BasicEffect`, etc.) implements, plus `GraphicsDevice::DrawIndexedPrimitives` — both genuinely
  XNA-facing surfaces, even though `ShaderEffect` itself is `NOXNA`.
- Related production files: `ShaderEffect.hpp`/`.cpp` (`IEffectMatrices` override, `FillGpuDrawParams()`),
  `GraphicsDevice.cpp` (`ExtractMatrices()`, `DrawIndexedPrimitives()`), `EasyGLGraphicsBackend.cpp`
  (`DrawIndexedPrimitivesEx()`, `BindCustomEffectMatrices()`).

## Purpose

Proves the wiring introduced by Task 1079: before this task, `ShaderEffect::FillGpuDrawParams()` had no override,
so a custom shader bound via `Apply()` was silently ignored by the 3D draw path (which always dispatched to a
built-in stride-selected shader). This test drives a lit `VertexPositionNormalTexture` quad (stride 32, an
already-supported layout) through `ShaderEffect`'s new `IEffectMatrices` properties and verifies the custom
program — not a built-in one — actually executes.

## Executive Verdict

**Healthy.** Both checks were independently re-derived against the actual GLSL and confirmed exact; Check B's
180°-Y-rotation choice (rather than 90°) is a genuinely well-reasoned false-positive guard, and the reasoning was
verified to be sound, not merely asserted.

## Checklist Results

### API / XNA / FNA parity
`ShaderEffect` overriding `getWorldProperty()`/`setWorldProperty()`/etc. (via `IEffectMatrices`) is the identical
mechanism every stock effect uses; `GraphicsDevice::ExtractMatrices()` (`GraphicsDevice.cpp` lines 550-561) does a
`dynamic_cast<const IEffectMatrices*>(effect)` and pulls `World`/`View`/`Projection` generically — confirmed by
direct reading that this dispatch has no special-casing for stock effects vs. `ShaderEffect`, i.e. this file's
premise ("extracted the same way every stock effect already works") is accurate, not just asserted in the comment.

### Behavioral correctness
Traced the full call chain: `DrawOnce()` sets `World`/`View`/`Projection` on `fx`, calls `fx->Apply()` (→
`OnApply()` → `effectBackend_->Bind()`), then `SetTexture`/`SetUniformVec3` (writes to the now-bound program), then
`device.DrawIndexedPrimitives(...)`. This reaches `GraphicsDevice::DrawIndexedPrimitives` →
`ExtractMatrices(currentEffect_, …)` → `currentEffect_->FillGpuDrawParams(p)` (sets `p.customEffectBackend`) →
`EasyGLGraphicsBackend::DrawIndexedPrimitivesEx()`, which checks `params.customEffectBackend` first (line 4577)
and, when set, calls `BindCustomEffectMatrices()` (binds the program a second time and re-uploads
`World`/`View`/`Projection` by name, matching this file's own vertex shader's `uniform mat4 World/View/Projection`
declarations exactly) instead of `SelectProgram()`'s built-in stride dispatch. This is a complete, verified,
non-hypothetical trace, not an assumption from the header comment.

### Logic
Independently re-derived both checks:
- Check A (`World=Identity`): world normal stays `(0,0,1)`; `N·L = dot((0,0,1),(0,0,1)) = 1` → full
  `diffuseColor=(200,100,50)` — matches `aOk` tolerance band exactly.
- Check B (`World=CreateRotationY(π)`): `mat3(World)` for a 180° Y-rotation maps `(0,0,1) → (0,0,-1)`
  (`RotationY(θ)` flips X and Z sign at `θ=π`, leaving Y sign flipped is for X-axis rotation — for Y-axis: X and Z
  both negate at 180°, Y unchanged), so world normal `(0,0,-1)`; `N·L = dot((0,0,1),(0,0,-1)) = -1`, clamped to `0`
  by the fragment shader's `max(...,0.0)` → output `(0,0,0)` (`bOk` checks `<=10` on all channels) — confirmed
  correct, and the choice of 180° over 90° is specifically justified (a 90° rotation would turn the quad edge-on,
  making "renders nothing" and "renders black" indistinguishable — a real false-positive risk correctly identified
  and avoided) — verified this reasoning is sound: a 90° Y-rotation of a planar XY quad does reduce its screen
  footprint to a line, which would make near-zero pixel coverage a genuine risk at the sampled centre pixel.
- `RasterizerState::CullNone` (line 181) is necessary for Check B to render at all once winding flips — confirmed
  present.

### Memory/resource lifetime
`vb_`/`ib_` are `std::unique_ptr`s with normal RAII lifetime. `fxBase_` is a `std::shared_ptr<Effect>` loaded via
`ContentManager`; the `.cnj`/`.vert.glsl`/`.frag.glsl` files are written to a per-instance temp directory
(`std::filesystem::temp_directory_path() / ("cna_shadereffect_3d_test_" + <this pointer>)`, lines 135-138) and
never removed — see F1.

### C++ correctness
`dynamic_cast<ShaderEffect*>(fxBase_.get())` is performed once in `Draw()` (validated, guarded against null) and
again, unconditionally and un-null-checked, inside `DrawOnce()` (line 183) — safe in practice only because
`DrawOnce()` is exclusively called after `Draw()`'s own guard has already passed, but the second cast has no
independent defense if that invariant is ever violated by future refactoring — see F2.

### Performance
N/A — single-shot per-frame test.

### Robustness
`fx->Apply()` is always called before `SetTexture()`/`SetUniformVec3()` (comment lines 189-191 explicitly documents
this as a load-bearing ordering requirement, "matching Bloom/Clouds' own proven call order") — confirmed against
`EasyGLEffectBackend::SetUniformVec3()`/`BindTexture()` (`EasyGLGraphicsBackend.cpp` lines 311-353), which write
directly via `program_.uniform_location()`/`set_uniform()` with no independent `use()` call of their own — so the
currently-bound program at call time matters, and this ordering is indeed required, not cargo-culted.

### Testing
Both checks were independently re-derived by this audit (see Logic) rather than trusted from the header comment,
and both hold. The two-check design (Identity vs. 180°) gives genuine, distinct expected outputs
`(200,100,50)` vs `(0,0,0)`, which is strong discriminating power for a lighting-direction/World-matrix bug.

### Cross-file consistency
Consistent with `ShaderEffect::FillGpuDrawParams()` (`ShaderEffect.cpp` lines 108-111, sets only
`customEffectBackend`) and `GraphicsDevice.cpp`'s generic `ExtractMatrices()`/`currentEffect_` mechanism used by
every draw call in the codebase.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Temp directory (3 files) is written per test run and never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: resource-hygiene
- Location/symbol: `Initialize()`, lines 135-147 (`std::filesystem::create_directories`, 3× `WriteFile`)
- Evidence: no `std::filesystem::remove_all` anywhere in the file, no destructor override.
- Why it matters: harmless per-run (a few hundred bytes), but accumulates unbounded small files across repeated
  CI/local test runs — the same pattern already flagged in sibling reports in this shard
  (`easygl_billboard_shader_test.cpp.audit.md`, `easygl_animsprite_shader_test.cpp.audit.md`).
- Suggested future action (not implemented by this audit): clean up the temp directory once the effect has been
  loaded (success or failure).

### F2 — `DrawOnce()`'s own `dynamic_cast` has no independent null guard

- Severity: LOW
- Confidence: HIGH
- Category: C++ correctness / robustness
- Location/symbol: `DrawOnce(const Matrix&)`, line 183
- Evidence: `auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());` immediately dereferenced (`fx->setWorldProperty(...)`)
  with no null check, relying entirely on `Draw()`'s earlier, separate guard.
- Why it matters: purely a latent-fragility note — today it's safe because `DrawOnce()` is only reachable after
  `Draw()`'s check passes, but the guard and the use are in two different functions with no shared invariant
  enforced by the type system (e.g. passing `ShaderEffect&` into `DrawOnce()` instead of re-deriving it would
  remove this risk entirely).
- Suggested future action (not implemented by this audit): pass the already-validated `ShaderEffect&` into
  `DrawOnce()` instead of re-deriving it via a second `dynamic_cast`.

## Cross-File Observations

- Shares the exact `IEffectMatrices`/`FillGpuDrawParams()`/`BindCustomEffectMatrices()` wiring with all 6 other 3D
  `ShaderEffect` files in this batch — a single defect in that shared production code path would affect all of
  them identically; this audit traced the path directly rather than assuming consistency across files.
- Establishes the "Apply() before SetTexture()/SetUniformXxx()" ordering convention that every later file in this
  batch (custom-vertex-layout, texture3d, texturecube, shadowmapping ×2, shattereffect) also follows correctly.

## Missing or Weak Tests

- No test in this file (or apparently elsewhere) covers a `ShaderEffect` used in the 3D path whose `World`/`View`/
  `Projection` are *never* set (left at their default-constructed `Matrix::getIdentityProperty()` values) —
  though this is arguably out of scope for a file whose entire point is to prove the matrices *do* reach the
  shader.

## Positive Findings

- The 180°-vs-90° rotation choice for Check B is a genuinely well-reasoned test-design decision (avoiding a
  "renders nothing" vs. "renders black" ambiguity) — verified sound by this audit, not just taken at face value
  from the comment.
- The file's own header comment is unusually precise about *why* certain choices were made (scope reduction to
  already-supported vertex strides, explicit call-order requirement) and each of those claims was independently
  confirmed against the real production code during this audit.

## Final Assessment

A rigorous, correctly-derived proof of Task 1079's `ShaderEffect`/3D-draw wiring; both checks were independently
verified by this audit against the actual GLSL and matrix math, and the only findings are minor hygiene/robustness
notes (F1/F2), not correctness defects.
