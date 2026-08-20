# Audit: examples/d3d9_effectbackend_test.cpp

## Metadata

- Source file: `examples/d3d9_effectbackend_test.cpp` (261 lines)
- Audit status: AUDITED (STATIC/SOURCE-READING ONLY — see Environment Note below)
- Subsystem: `examples-tests-d3d9` shard — `D3D9EffectBackend`, CNA's custom
  runtime-`D3DCompile()` `ShaderEffect` backend (`plans/plan_dx9.md` Phase D9-11 / D9-111).
- File type: `Game`-subclass executable (`D3D9EffectBackendTest : public Game`), 5 checks (A-E),
  CTest-registered as `D3D9_EffectBackend` (`cmake/Tests/D3D9Tests.cmake:181-186`, `TIMEOUT 60`,
  gated behind `TARGET cna_backend_graphics_d3d9_effect`, pinned to the real-compiler Wine prefix
  `~/.wine-cna-d3d9-spike`).
- XNA/FNA relevance: indirect — `D3D9EffectBackend`/`IEffectBackend` back CNA's own NOXNA
  `ShaderEffect` mechanism (custom-shader `SpriteBatch.Begin(effect)` support), not a
  `Microsoft::Xna` Stock Effect itself.
- Related production code: `include/CNA/Internal/Backends/D3D9/D3D9EffectBackend.hpp` (79 lines),
  `src/CNA/Internal/Backends/D3D9/D3D9EffectBackend.cpp` (161 lines) — both read in full.

**Environment note (per D-P4/audit instructions):** D3D9 is Windows-only, and this test additionally
requires both a real D3D9 device and the real Microsoft `d3dcompiler_47.dll` (via the pinned Wine
spike prefix). No build or execution was attempted in this Linux sandbox; this report is entirely
static-source-reading, cross-checked line-by-line against the current
`D3D9EffectBackend.cpp`/`.hpp` production implementation.

## Purpose

Five checks proving `D3D9EffectBackend` — CNA's runtime-compile custom vertex+pixel shader
mechanism, separate from the offline stock-shader pipeline — is real end-to-end: (A)
`CompileProgram()` succeeds for a genuine custom HLSL vertex+pixel shader pair
(`vs_2_0`/`ps_2_0` under `Reach`); (B) `SetUniformVec4("MyColor",...)` on the pixel shader's own
real, compiler-assigned register genuinely drives the rendered pixel color, not a hardcoded/ignored
no-op; (C) changing that same uniform to a different value changes the rendered output
accordingly, proving the upload is genuinely per-call; (D) split into two sub-checks (D1/D2)
specifically to close a mutation-testing gap: D1 first proves a re-uploaded identity
`WorldViewProj` still paints red (a positive anchor), then D2 proves a far-away `WorldViewProj`
leaves the target pixel unpainted — together ruling out both "the upload never happens" (which
would ALSO leave the register at zero/uninitialized, degenerating the triangle and ALSO leaving the
pixel unpainted, for a completely different, broken reason) and "the upload happens but has no
effect" as false-positive explanations for D2 passing; (E) a deliberately invalid HLSL vertex
shader fails to compile, `IsValid()` stays false, and `GetCompileError()` reports a real, non-empty
diagnostic.

## Executive Verdict

**Healthy** — a small, tightly-scoped, and unusually rigorous test file. Its Check D
positive-anchor-before-negative-check design (D1 before D2) is a genuinely sophisticated
mutation-testing discipline, explicitly justified in the file's own comment, and independently
confirmed sound by this audit.

## Checklist Results

### API / XNA / FNA parity
N/A directly — `D3D9EffectBackend`/`IEffectBackend` are CNA-internal (`NOXNA`) backend plumbing
implementing this project's own custom-shader-effect abstraction, not `Microsoft::Xna` API surface.
The class correctly implements `IEffectBackend`'s virtual contract
(`CompileProgram`/`Bind`/`Unbind`/`IsValid`/`GetCompileError`/`SetUniform{Float,Int,Vec2,Vec3,
Vec4,Mat4}`), confirmed against `D3D9EffectBackend.hpp` lines 51-62.

### Behavioral correctness
- Check A (lines 155-160): `effect.CompileProgram(kVertexShaderSrc, kPixelShaderSrc)` returns
  `true` and `IsValid()` is `true`. Confirmed against `D3D9EffectBackend::CompileProgram()`
  (`D3D9EffectBackend.cpp` lines 26-76): both `D3DCompile()` calls (vertex targeting
  `vs_2_0`/pixel targeting `ps_2_0` since `hiDef=false` is passed at construction, line 156)
  must succeed, then both `CreateVertexShader`/`CreatePixelShader` must succeed, then both
  blobs are parsed via `ParseConstantTableEXT` (this batch's `d3d9_constanttable_test.cpp`
  companion validates that parser independently) before `valid_=true` is set — a real, multi-step
  success path, not a stub returning `true` unconditionally.
- Check B (lines 168-178): `SetUniformVec4("MyColor", 1,0,0,1)` before `Bind()`+draw, expects an
  exact opaque-red readback. Confirmed `D3D9EffectBackend::UploadEXT()` (lines 93-121) looks up
  `"MyColor"` in `psConstants_` (the pixel shader's own real, compiler-assigned register table,
  parsed from the actual compiled bytecode, not a hardcoded slot) and calls
  `SetPixelShaderConstantF()` at that shader's own real register index — a genuine per-shader,
  per-name lookup, not a fixed-slot convention (the file's own header comment, lines 8-14,
  correctly explains WHY this differs from `D3D11EffectBackend`'s fixed-cbuffer-offset
  convention: D3D9 constant registers are compiler-assigned and can vary between shaders).
- Check C (lines 180-190): re-uploads `"MyColor"` as `(0,1,0,1)` (green) and re-draws, expecting a
  different, exact opaque-green readback. This is the check that specifically rules out "the
  uniform was latched once and never actually re-read per draw" — confirmed
  `SetUniformVec4`→`UploadEXT` unconditionally calls `SetPixelShaderConstantF` every time it's
  invoked (no caching/dirty-flag short-circuit in the production code that could cause a stale
  value to persist), so this check's premise (a real regression class it could catch) is valid.
- Check D1/D2 (lines 192-225): the file's own comment (lines 193-200) is explicit and correct
  about WHY D1 (re-upload identity `WorldViewProj`, confirm it STILL paints red) must precede D2
  (upload a far-away `WorldViewProj`, confirm the pixel goes unpainted): a `WorldViewProj` upload
  that silently never happens at all would leave the vertex shader's constant register at its
  power-on/stale value (frequently all-zero after a fresh compile, per typical D3D9 register-file
  behavior), which would ALSO degenerate the oversized triangle to a degenerate point/zero-area
  primitive and ALSO leave the background unpainted — for a completely unrelated, broken reason
  that D2 alone cannot distinguish from "the upload is real and genuinely moved the triangle
  off-screen." D1 closes that ambiguity by proving the SAME register, explicitly re-uploaded as
  identity, still produces the correct positive (painted) result immediately before D2's negative
  case is checked. This is a materially stronger design than a bare "far away ⇒ unpainted" check
  alone, and this audit confirms the reasoning is sound (not just plausible-sounding) — a
  register-upload-never-happens bug really would produce a false-negative-masking false-positive
  on D2 alone, and D1 really does rule that out.
- Check E (lines 231-236): `kBrokenShaderSrc` ("this is not valid HLSL at all !!!") as the VERTEX
  shader source, paired with the valid `kPixelShaderSrc`. Confirmed
  `D3D9EffectBackend::CompileProgram()` compiles the vertex shader FIRST (lines 40-48) and returns
  `false` immediately on failure, before ever attempting to compile the pixel shader or create
  either D3D9 shader object — so `compileError_` is populated from the vertex-shader `D3DCompile`
  failure specifically, `valid_` stays `false` (never set `true` anywhere on this path), and
  `vs_`/`ps_` stay reset (`.Reset()` at lines 30-31, never reassigned). `GetCompileError()`
  (a direct accessor, `D3D9EffectBackend.hpp` line 55) correctly surfaces this non-empty string.

### Logic
`ToShaderRegistersEXT()` (test file, lines 97-101) transposes the matrix then packs it
column-major — confirmed this exactly matches `UploadMatrixConstantVS`'s own convention inside
`D3D9EffectDraw.cpp` (this batch's `d3d9_drawex_test.cpp` companion file's production code,
lines 122-131: `Matrix::Transpose(m)` then `.ToColumnMajor(regs)`), and the test's own comment
(lines 92-96) correctly cites this as "matches this project's own established Matrix->HLSL-register
convention" — a real, verified cross-file consistency, not an assumed one.

### C++ correctness
`drawFullscreenTriangle` lambda (lines 139-145) intentionally ignores its own `world` parameter
(`(void)world;`) with a clear inline comment explaining `WorldViewProj` is uploaded via
`SetUniformMat4` directly rather than through this helper — a deliberate, documented design choice
(the lambda's only real job is to (re)bind the vertex declaration/stream source and issue
`DrawPrimitive`), not a forgotten/dead parameter. `readCenter()` (lines 148-153) reads exactly 1
pixel via a 4×4-sized-but-1×1-rectangle readback buffer (`px(4*4,...)` but `region(28,28,4,4)` —
the rectangle IS actually 4×4, and `px[0]` is read as "the center," which for a solid-fill
full-viewport triangle is representative of any pixel in that 4×4 block; this is consistent with
the file's own full-NDC-oversized-triangle design where the entire 4×4 region is uniformly the same
color, so reading only `px[0]` rather than checking all 16 is a reasonable simplification here, not
a bug, given the geometry guarantees uniformity).

### Memory/resource lifetime
`ComPtr<IDirect3DVertexDeclaration9> decl` (line 132) and the vertex buffer (line 135) are held in
local variables scoped to `Draw()`, appropriately released at end of frame. `D3D9EffectBackend
effect(...)` (line 156) and `brokenEffect` (line 232) are plain stack objects — `CompileProgram()`
internally resets (`vs_.Reset()`/`ps_.Reset()`) before each attempt, so no leak risk across the
two `D3D9EffectBackend` instances constructed in this file.

### Thread safety, Performance
N/A — single-threaded, one-shot diagnostic; not a hot path.

### Architecture
Correctly exercises `D3D9EffectBackend` directly (constructed explicitly, not through
`ShaderEffect`'s own public constructor) — an intentional choice appropriate for a backend-focused
unit test isolating this class's own contract from the higher-level `ShaderEffect`/`SpriteBatch`
integration, which this project's sibling file `d3d9_spritebatch_customeffect_test.cpp` (not in
this batch) is presumably responsible for covering instead.

### Maintainability
The header comment (lines 1-22) and the D1/D2 mutation-testing rationale (lines 193-200) are both
genuinely informative, non-boilerplate explanations of WHY each check is shaped the way it is —
consistent with this batch's overall finding that D3D9 test files in this shard tend to document
real reasoning rather than restating what the assertion already says.

### Robustness
Check E is a real, non-degenerate negative test (deliberately-invalid HLLSL, not merely an empty
string or a trivially-obviously-broken one-character string) that verifies 3 independent
postconditions together (`!compiled`, `!IsValid()`, non-empty `GetCompileError()`) rather than just
one.

### Testing
This file is the primary direct test of `D3D9EffectBackend`'s full public contract
(`CompileProgram`/`Bind`/`Unbind`/`IsValid`/`GetCompileError`/`SetUniformVec4`/`SetUniformMat4`).
Not directly exercised by this file: `SetUniformFloat`/`SetUniformInt`/`SetUniformVec2`/
`SetUniformVec3` (see Missing or Weak Tests) — though `SetUniformVec4`/`SetUniformMat4`'s shared
`UploadEXT()` code path (confirmed in `D3D9EffectBackend.cpp` lines 93-160: every `SetUniform*`
overload funnels through the same `UploadEXT()`) means the untested overloads differ only in how
many floats they pack into the call, not in the underlying register-lookup/upload logic itself —
a real but low-risk gap.

### Cross-file consistency
`D3D9EffectBackend.hpp`/`.cpp` were read in full (79 + 161 lines). Every behavior this test
exercises or asserts (constructor `hiDef` parameter selecting `vs_2_0/ps_2_0` vs. `vs_3_0/ps_3_0`,
`CompileProgram`'s reset-then-recompile sequencing, `UploadEXT`'s per-stage name lookup, `Bind()`'s
`if (!valid_) return;` guard, `Unbind()`'s intentionally-empty body) was independently confirmed
against the current production source, not merely assumed from the test's own comments.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

## Missing or Weak Tests

- **LOW (confidence HIGH): `SetUniformFloat`/`SetUniformInt`/`SetUniformVec2`/`SetUniformVec3` are
  never exercised.** Only `SetUniformVec4` (Checks B/C) and `SetUniformMat4` (Check D) are used.
  Since all 6 `SetUniform*` overloads funnel through the same `UploadEXT()` implementation
  (confirmed above), the marginal risk is low — a bug specific to, say, `SetUniformVec2`'s own
  2-float packing (`D3D9EffectBackend.cpp` lines 139-143) would not be caught by this file, but the
  shared-implementation structure means such a bug would most likely be a simple size/count
  mismatch localized to that one overload, not a systemic issue this file's existing checks would
  miss for the shared logic.
- **LOW (confidence MEDIUM):** `SetUniformInt`'s documented design choice (converts to `float` and
  routes through the Float4 register path rather than a separate `D3DXRS_INT4` register set,
  `D3D9EffectBackend.cpp` lines 128-137's own comment) is asserted only in a comment, never
  actually exercised by any check in this file — a real, if minor, gap given this is an explicit,
  documented divergence from a naive "int uniforms use int registers" expectation.

## Positive Findings

- The D1-before-D2 mutation-testing design for Check D is the standout strength of this file: it
  is a real, non-obvious technique (most test files in this batch and its sibling shards use a
  single "moved far away ⇒ unpainted" check without an anchor), explicitly justified in the file's
  own comment, and this audit independently confirmed the specific false-positive-masking failure
  mode (a silently-never-happening upload) that D1 alone rules out.
- Check E's 3-postcondition negative test (`!compiled && !IsValid() && !GetCompileError().empty()`)
  is a genuinely thorough failure-path check, not a single loosely-related assertion.
- The test's own `ToShaderRegistersEXT()` helper was independently confirmed to exactly match the
  production `UploadMatrixConstantVS`'s own transpose+column-major convention — real cross-file
  verification, not an assumed match.

## Final Assessment

A small, well-designed, and — as far as this audit's static reading and cross-checking against
current production source can determine — fully correct test of `D3D9EffectBackend`'s custom
runtime-shader-compilation contract. Its one standout strength (the D1/D2 mutation-testing anchor
pattern) is worth highlighting as a positive example for other test files in this project; its only
gaps (untested `SetUniformFloat`/`Int`/`Vec2`/`Vec3` overloads) are low-risk given the shared
`UploadEXT()` implementation underneath all six.
