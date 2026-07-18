# Audit: examples/webgpu_texture2d_getdata_test.cpp

## Metadata

- Source file: `examples/webgpu_texture2d_getdata_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `WebGPUTextureBackend::GetData()` CPU-readback test
  (WEBGPU-51), CTest target `WebGPU_Texture2D_GetData`
  (`cna_webgpu_test(cna_test_webgpu_texture2d_getdata …)` /
  `cna_register_backend_test(NAME WebGPU_Texture2D_GetData …)`, `cmake/Tests/WebGpuTests.cmake:153-155`).
- XNA/FNA relevance: indirect — this file deliberately bypasses the XNA-facing `Texture2D` API
  and drives `CNA::Internal::Backends::IGraphicsBackend`/`ITextureBackend` directly, per its own
  header comment (lines 8-15): `Texture2D::GetData()` is implemented via a CPU-side pixel shadow
  (`cpuPixels_`) in the shared, backend-agnostic `Texture2D.cpp` and only ever calls through to
  `ITextureBackend::GetData()` for a `RenderTarget2D`-backed instance — so exercising
  `WebGPUTextureBackend::GetData()` itself (a plain, `SetData()`-populated texture) requires going
  around that layer.
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`WebGPUTextureBackend::GetData()` lines 690-780, `UpdatePixelsLevel()` lines 665-681,
  `UpdatePixels()` lines 629-663, `WebGPUGraphicsBackend::CreateTexture()` lines 5778-5781),
  `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` (`ITextureBackend::GetData()`
  default, lines 204-210, and its own doc comment stating exactly the CPU-shadow bypass this test
  relies on).

## Purpose

Three-check test proving `WebGPUTextureBackend::GetData()` — added in WEBGPU-51, previously the
safe no-op `ITextureBackend::GetData()` default — is a genuine staged `MAP_READ`-buffer CPU
readback, not a stub: (A) a full 4x4 RGBA8 round trip through `CreateTexture()`+implicit
`UpdatePixels()`; (B) a 2x2 sub-rectangle at a non-origin offset `(3,2)` inside an 8x8 texture,
proving the x/y offset is honoured rather than always reading from `(0,0)`; (C) a mip-level round
trip where level 1 is uploaded via `UpdatePixelsLevel()` with content deliberately distinct from
level 0, proving the `level` parameter reaches the right GPU mip slice.

## Executive Verdict

**Healthy** — all three checks were independently re-derived against the actual
`WebGPUTextureBackend::GetData()`/`UpdatePixelsLevel()` implementation and match exactly; the one
real gap is a missing negative/out-of-bounds test for this exact backend entry point (see Missing
or Weak Tests), not a defect in the checks that exist.

## Checklist Results

### API / XNA / FNA parity

N/A in the conventional sense — this file deliberately targets the CNA-internal
`IGraphicsBackend`/`ITextureBackend` layer, not an `Microsoft::Xna::Framework` type. The comment
correctly identifies `Texture2D::GetData()`'s own CPU-shadow-first behavior
(`src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`, not re-read line-by-line here since this
file explicitly routes around it) as the reason a backend-direct test is needed at all — this
framing is accurate and matches `ITextureBackend::GetData()`'s own header doc (`IGraphicsBackend.hpp`
lines 204-208): "Texture2D::GetData() only calls this when its own CPU-side pixel shadow is
unavailable... plain, SetData()-populated textures never reach this path."

### Behavioral correctness

- **Check A** (lines 96-112): `MakeGradient(4,4,10)` produces 16 distinct-ish RGBA8 pixels (per
  the file's own per-pixel formula `seed+x*16`/`seed+y*16`/`seed+x+y`), uploaded via
  `backend.CreateTexture(img)` (which triggers the constructor's implicit
  `UpdatePixels(data.pixels.data(), width_*4)` since `img.pixels` is non-empty, per
  `WebGPUTextureBackend::WebGPUTextureBackend()` line 617-618), then read back whole via
  `tex->GetData(0,0,0,w,h,...)`. Traced `GetData()`'s inner loop (lines 760-776): for `x=y=0` sized
  exactly `w×h`, every `(sx,sy)` stays in-bounds, so the per-pixel copy at
  `mapped + sy*bytesPerRow + sx*4` reproduces the tightly-packed source exactly — `readback ==
  pixels` is a correct expectation.
- **Check B** (lines 115-143): an 8x8 gradient, then `tex->GetData(0, rx=3, ry=2, rw=2, rh=2, ...)`.
  Re-traced the same loop with `x=3,y=2`: for `row∈{0,1}, col∈{0,1}`, `sx=3+col∈{3,4}`,
  `sy=2+row∈{2,3}`, all within the 8x8 bounds, so `d[...] = mapped[sy*bytesPerRow + sx*4 ...]` —
  the test's own expected-index formula `pixels.data() + ((ry+row)*w + (rx+col))*4` (line 135-136)
  matches this exactly (the source is tightly packed at `width*4` per row, and `bytesPerRow` here
  equals `AlignBytesPerRow(8*4=32) = 256` due to WebGPU's 256-byte row-pitch requirement — a
  materially *different* stride between the CPU-side `pixels` vector (32 bytes/row) and the GPU
  readback buffer (256 bytes/row), which `GetData()`'s own `mapped + sy*bytesPerRow + ...`
  indexing correctly accounts for and the test's own `pixels.data() + (...)*w...*4` indexing
  correctly does *not* need to (since it addresses the untouched, tightly-packed CPU source, not
  the aligned GPU buffer) — no aliasing bug here, just two different, both-correctly-handled
  strides).
- **Check C** (lines 146-165): `img.mipLevels=2` with `img.pixels=level0` triggers the
  constructor's `UpdatePixels()` call, which (since `mipLevels_>1`) also invokes
  `owner_->GenerateMips2D(texture_, width_, height_, mipLevels_)` (line 661-662) — meaning level 1
  is first populated with a *real, linearly-downsampled* copy of level 0's own gradient before the
  test's explicit `tex->UpdatePixelsLevel(1, level1.data(), w/2, h/2)` call overwrites it with the
  deliberately distinct seed-200 content. Both operations are submitted to the same WebGPU queue in
  program order (`UpdatePixels`'s mip-gen render pass, then the later `UpdatePixelsLevel`'s
  `wgpuQueueWriteTexture`), and WebGPU guarantees in-order execution of commands submitted to one
  queue, so the final state read back is deterministically the `UpdatePixelsLevel` write, not a
  race with the auto-generated mip — the check's implicit assumption holds.

### Logic

`MipDim`/`AlignBytesPerRow` (both file-local helpers in the production `.cpp`, lines 186-196) are
exercised correctly by all three checks; `MipDim(base,level)=max(1,base>>level)` for `level=1` on
an 8x8 source yields `4`, matching the test's own `w/2, h/2 = 4,4` expectation for level 1 in Check
C.

### Memory/resource lifetime

The test itself is a single-frame `Game`/`Draw()` override (`frame_++<1` gate, line 90) with no
manual GPU-resource cleanup — relies entirely on `GraphicsDevice`/backend teardown at `Game`
destruction, consistent with every other example-test in this shard.

### C++ correctness

`std::memcmp(expected, got, 4)` (line 138) is the correct, alignment-safe way to compare two
4-byte RGBA runs here (both are `std::uint8_t*` into `std::vector` storage, no strict-aliasing
concern). `readback == pixels` (Check A, line 110) is a `std::vector<uint8_t>` equality — legitimate
since sizes are constructed equal.

### Robustness

Not exercised by design — see Missing or Weak Tests. `WebGPUTextureBackend::GetData()`'s own
defensive fallback (lines 750-757: if the mapped pointer is null or the destination buffer is
undersized, zero-fill instead of touching out-of-bounds memory) is real production code but never
reached by any of this file's three checks, all of which supply exactly-sized, in-bounds buffers.

### Testing

Three checks, each targeting a genuinely distinct axis (whole-texture correctness, non-origin
sub-rectangle offset, mip-level selection) — a well-designed, minimal set for what WEBGPU-51 added.
See Missing or Weak Tests for the one real gap.

## Detailed Findings

No HIGH/CRITICAL findings. No MEDIUM/LOW correctness findings against this file's own three checks
— all were independently re-derived and hold.

## Cross-File Observations

- This file's own design note (lines 8-15) explicitly names
  `examples/webgpu_instanced3d_test.cpp` as the precedent for "testing a backend capability with no
  XNA-layer round trip of its own" — consistent, documented convention across this shard rather
  than an ad hoc choice.
- `WebGPUTextureBackend::GetData()`'s implementation (staged `MAP_READ` buffer, aligned
  `bytesPerRow`, `wgpuBufferMapAsync`+`WaitForCompletion` poll loop) is structurally identical to
  `WebGPUTextureCubeBackend::GetData()` and `WebGPUTexture3DBackend::GetData()` (both audited in
  this same batch) — all three share the same per-pixel zero-fill-on-out-of-bounds fallback and the
  same "silently zero instead of throw when the destination buffer is undersized" convention. This
  is a real, repeated pattern across the file, not incidental to this one class.

## Missing or Weak Tests

- **No out-of-bounds / invalid-parameter coverage for `WebGPUTextureBackend::GetData()` itself.**
  Because this file deliberately drives `ITextureBackend` directly (bypassing `Texture2D`'s own
  bounds-checked XNA-layer entry points), it is the *only* place in this codebase positioned to
  exercise `WebGPUTextureBackend::GetData()`'s own defensive branches (lines 693-696: `w<=0`/`h<=0`/
  `data==nullptr` silent return; line 695-696: `level` out of `[0,mipLevels_)` throws
  `std::out_of_range`; lines 754-757: undersized `dataLength` or a null mapped pointer silently
  zero-fills instead of throwing) — none of the three checks exercises any of these paths. A
  regression that silently broke the undersized-buffer zero-fill guard, or accidentally allowed a
  negative `level` through, would not be caught by this file. Severity: LOW-to-MEDIUM (this is a
  genuinely internal, backend-direct interface most callers never hit unbound-checked, but it is
  exactly the kind of gap the project's own `AUDIT_CROSS_CUTTING_FINDINGS.md` "recurring testing
  gaps" section calls out as a systemic pattern across backends).
- No test exercises `GetData()` with `x`/`y` placing the requested rectangle partially or wholly
  outside the level's bounds (the per-pixel zero-fill path at lines 767-771) — a case Check B could
  have cheaply extended to (e.g. a rectangle straddling the right/bottom edge) but does not.

## Positive Findings

- Check B's own re-derivation is a good, honest piece of test design: choosing a non-origin
  `(3,2)` offset inside an 8x8 (not 2x2/4x4) texture specifically defeats an implementation that
  hardcodes reading from `(0,0)` — a materially stronger check than Texture2D-getdata tests in
  other backends' shards that only ever read the whole texture.
- Check C's use of a *different* seed (200 vs. 0) for level 1 rather than merely a differently-sized
  buffer is the right technique to prove the mip level is actually selected, not just that a
  same-shaped buffer of the right size was returned.
- The file's own header comment transparently documents *why* it must go around the XNA `Texture2D`
  layer, rather than silently doing so — this made cross-checking the design decision fast instead
  of requiring independent rediscovery.

## Final Assessment

A well-targeted, three-axis test for `WebGPUTextureBackend::GetData()` (WEBGPU-51) whose expected
values were independently re-derived against the real production readback code, including the
easy-to-miss detail that the GPU-side aligned row pitch (256-byte `bytesPerRow`) differs from the
CPU-side tightly-packed source stride and is handled correctly on both sides. The only actionable
gap is the complete absence of negative/out-of-bounds coverage for this specific backend-direct
entry point, which is otherwise untested anywhere else in the codebase (since `Texture2D`'s own
XNA-layer bounds checks never let an invalid rectangle reach this method through the normal API).
