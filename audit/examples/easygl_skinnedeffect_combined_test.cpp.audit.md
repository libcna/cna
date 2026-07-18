# Audit: examples/easygl_skinnedeffect_combined_test.cpp

## Metadata

- Source file: `examples/easygl_skinnedeffect_combined_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 409, cross-backend `SkinnedEffect` capstone
- File type: C++ example/integration test
- Related production code: `SkinnedEffect.hpp`/`.cpp` (`SetBoneTransforms`,
  `EnableDefaultLighting`), `EasyGLGraphicsBackend.cpp::EnsureSkinnedProgram()`
- XNA/FNA relevance: `SkinnedEffect` is a real XNA 4.0 stock effect; this test composes 3
  previously-separately-verified bone scenarios (Tasks 406-408, referenced by name) into one draw
  call, to prove they compose correctly together rather than only in isolation.
- Main related tests: `easygl_skinned_effect_bones_test.cpp` (individual-draw-call versions of
  the same 3 scenarios), `easygl_skinnedeffect_golden_test.cpp` (Task 469, reuses this exact
  scene's "quad A" via golden-image comparison instead of this file's own predicate check).

## Purpose

Proves that 3 quads with *different* per-vertex bone weight/index data, uploaded via a single
bone-palette (`SetBoneTransforms` with 4 matrices) and drawn in one `DrawPrimitives` call, each
independently pick up their own correct per-vertex skinning data — i.e. the vertex shader reads
genuinely per-vertex `aBoneWeights`/`aBoneIndices`, not some cached/global last-set value that
would incorrectly apply to all 18 vertices in the single draw.

## Executive Verdict

**Healthy.** The 3-quad, 4-bone composition is well-designed specifically to catch a per-vertex
skinning-data regression that a single-quad test cannot: if the shader ever read a stale/shared
uniform instead of the true per-vertex attribute, all 3 quads would land at the same X position
(bone 0's transform) rather than 3 distinct positions. The test's own math for quad C's blended
shift (`0.5*1.0 + 0.5*2.0 = 1.5`) is verified against the real shader formula and does not
coincide with quad A's or quad B's own shift, so a "shared value" bug would visibly fail.

## Checklist Results

### API / XNA / FNA parity
`SetBoneTransforms`, `setWeightsPerVertexProperty(2)`, `EnableDefaultLighting()` are all genuine
XNA 4.0 `SkinnedEffect`/`IEffectLights` API members — correct usage, not a NOXNA extension test.

### Behavioral correctness
Traced the 3 quads' expected post-transform positions against `EnsureSkinnedProgram()`'s
`skinMat = sum_i(uBones[indices[i]] * weights[i])` formula:
- **Quad A**: `w0=1, i0=0` -> `Bones[0]=Identity` -> stays at authored x:[-1.0,-0.5].
- **Quad B**: `w0=1, i0=1` -> `Bones[1]=Translate(+0.75,0,0)` -> shifts to x:[-0.25,0.25].
- **Quad C**: `w0=w1=0.5, i0=2, i1=3` -> `Bones[2]=Translate(+1.0,0,0)`,
  `Bones[3]=Translate(+2.0,0,0)` -> blended shift `0.5*1.0+0.5*2.0=1.5` -> shifts to x:[0.5,1.0].
All 3 target regions are non-overlapping and land exactly on the shard's 3 established sample
columns (`W/8`, `W/2`, `7*W/8`, i.e. NDC x ~ -0.75/0.00/+0.75) — verified these fall inside each
quad's respective post-transform x-range (e.g. quad A's [-1.0,-0.5] does contain NDC -0.75; quad
B's [-0.25,0.25] contains 0.00; quad C's [0.5,1.0] contains +0.75). Correct, deliberate design.

### Logic
`appendQuad()` (lines 65-76) builds one quad per call with shared weight/index data across all 6
of its own vertices, and `verts` accumulates all 3 quads' vertices into one buffer before a single
`DrawPrimitives(TriangleList, 0, verts.size()/3)` call (line 138) — this is the actual mechanism
under test (one draw, heterogeneous per-vertex skinning data) and is correctly assembled.

### Memory/resource lifetime
`VertexBuffer vb(device, verts.size())` constructed locally with all 18 vertices uploaded via
`SetDataRaw` before the single draw call — standard, correct RAII usage, no dangling-pointer risk.

### C++ correctness
`static_assert(sizeof(SkinnedGpuVertex) == 52)` (line 61) matches the shared stride-52 layout used
throughout this shard's `SkinnedEffect` tests. No manual struct-packing pragma is used here
(unlike the ShipGame shader tests) because the natural alignment of `float`x N + `uint8_t`x4
already produces the correct 52-byte size on this project's target platforms — consistent with the
sibling files that also omit `#pragma pack` for this exact struct shape (e.g.
`easygl_skinnedeffect_fog_test.cpp`, `easygl_skinnedeffect_identity_bones_test.cpp`).

### Performance
N/A at this scale (18 vertices, 1 draw call).

### Architecture
No explicit `GraphicsDeviceManager` is constructed by this test (no constructor override at all,
unlike `easygl_skinned_effect_bones_test.cpp`/`easygl_skinnedeffect_fog_test.cpp` which do) —
confirmed this is safe: `Game::getGraphicsDeviceProperty()` falls back to a directly-owned
`GraphicsDevice_` member when no `IGraphicsDeviceService` was registered (`Game.cpp` lines
172-185), and the test correctly reads the *actual* viewport width/height dynamically
(`vp.getWidthProperty()`/`getHeightProperty()`, lines 99-101) rather than hardcoding a size, so
this works regardless of what the fallback device's default backbuffer dimensions are. Not a
defect — just an inconsistency in style versus sibling files in the same batch, worth noting for
anyone searching this shard for "how is the device created" patterns.

### Maintainability
Clean, focused single-purpose test — no dead code, no unused parameters (unlike its sibling
`easygl_skinned_effect_bones_test.cpp`'s `drawAndCheck()`).

### Robustness
N/A — deterministic, no external input.

### Testing
This file is itself the test; `easygl_skinnedeffect_golden_test.cpp` independently reuses its
"quad A" scene via a different verification mechanism (golden-image comparison) rather than
duplicating this file's own predicate logic, which is a reasonable non-duplicative reuse pattern.

### Cross-file consistency
`device.setRasterizerStateProperty(RasterizerState::CullNone);` (line 109) carries the same
"Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix)" comment as every other
`SkinnedEffect` test in this batch — consistent, correctly-applied project-wide convention.

## Detailed Findings

No MEDIUM+ findings.

- **LOW / INFO** — Assertion strength: `aOk`/`bOk`/`cOk` (lines 150-152) use a loose predicate
  (`R > G && R > 50`, "red-dominant") rather than a computed expected byte value, because
  `EnableDefaultLighting()` applies real 3-light Phong lighting math that isn't trivial to
  hand-derive (the file's own golden-test sibling explicitly says this, see that file's header
  comment). This is a reasonable, disclosed trade-off given the lighting complexity, but it does
  mean this specific file could not, on its own, catch a lighting-intensity regression that still
  keeps R dominant over G/B (e.g. red channel dropping from 200 to 60 would still pass) — the
  golden-image sibling test (`easygl_skinnedeffect_golden_test.cpp`) is the one that would catch
  that class of regression for quad A specifically. Not a defect in this file; a scope note.

## Cross-File Observations

This file is the "capstone" (per its own header) that Task 469's golden test explicitly reuses
(quad A only) for a stricter pixel-level comparison — the two files are correctly complementary
rather than redundant: this one proves the 3-quad *composition* works, the golden test proves
quad A's *exact* pixel values are stable over time.

## Missing or Weak Tests

See the "red-dominant" predicate note above — a follow-up could tighten quads B and C to also have
their own golden-image or computed-value cross-checks (currently only quad A has one, via the
sibling golden test), but this is a reasonable, disclosed scope choice rather than an oversight.

## Positive Findings

- Well-designed 3-quad, 4-bone single-draw-call composition that specifically targets a
  per-vertex-vs-shared-state class of regression that isolated single-quad tests cannot catch.
- Correctly and dynamically reads viewport dimensions rather than hardcoding, making it robust to
  its own lack of an explicit `GraphicsDeviceManager`.

## Final Assessment

A well-targeted composition test with a real, distinct verification purpose (per-vertex skinning
data correctness within one draw call) not covered by its simpler sibling tests. No defects found;
the loose "red-dominant" pass predicate is a disclosed, reasonable trade-off given lighting-math
complexity, and is complemented by the sibling golden-image test for quad A.
