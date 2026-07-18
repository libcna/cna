# Audit: examples/easygl_texturecube_faces_test.cpp

## Metadata

- Source file: `examples/easygl_texturecube_faces_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 172, `TextureCube` per-face `SetData`/`GetData`
  round-trip
- File type: hand-rolled `Game`-subclass executable, CTest-registered as
  `cna_test_easygl_texturecube_faces` (`cmake/Tests/EasyGLTests.cmake:840-841`).
- XNA/FNA relevance: `TextureCube.SetData`, `TextureCube.GetData`, `CubeMapFace` — real XNA 4.0 API.
- Related production code: `TextureCube::SetData`/`GetData` (2-arg overload,
  `src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp:125-135, 172-182`);
  `EasyGLTextureCubeBackend::SetData`/`GetData`
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:210-241`).

## Purpose

Creates a 2×2, non-mipmapped `TextureCube`, writes each of the six faces a distinct solid color via
the simple `SetData(face, data[], elementCount)` overload, then reads every face back via the
matching `GetData(face, data[], elementCount)` overload and checks every pixel, verifying colors do
not bleed across faces.

## Executive Verdict

**Healthy.** A simple, direct, correctly-targeted test whose backend implementation (a per-face GL
cube-map-face target for `SetData`, a per-face-and-level FBO attachment for `GetData`) was read and
confirmed to genuinely isolate each face — there is no shared mutable state between faces in the
production code path this test exercises that could cause silent cross-face bleed.

## Checklist Results

### Behavioral correctness
`kFaces` (lines 55-62) assigns a distinct, mutually-exclusive color per face (Red/Green/Blue/Yellow/
Cyan/Magenta) — chosen so any single-channel mixup between faces (e.g. writing to the wrong GL cube
target) would produce an easily-distinguishable wrong color rather than an ambiguous near-miss.
`colourEq` (lines 46-51) compares R/G/B only, consistent with this project's established convention
(noted in the `PixelTestGame` audit) of not asserting on alpha unless a test specifically cares about
it.

Two-phase structure — write **all six** faces first (lines 88-92), *then* read back and verify all
six (lines 95-108) — is the right shape to catch cross-face bleed: if `SetData(face,...)` for one
face were mistakenly binding a different GL cube-face target, this ordering guarantees a
later-written face's color would appear when an earlier face is read back, rather than each
face's write/verify happening in isolation where the bug could go unnoticed.

### Cross-file consistency
`EasyGLTextureCubeBackend::SetData` (lines 210-219) binds the cube-map texture object once, then
targets the specific face via `kCubeFaceTargets[face]` in `set_sub_image_2d` — the six GL cube-map-
face enum targets (`TextureCubeMapPositiveX`...`NegativeZ`) are distinct, per-face storage locations
within a single GL texture object, so there is no shared per-face mutable state at the GL level that
could leak between `SetData` calls for different faces (confirmed by reading the backend directly,
not assumed).

`GetData` (lines 221-241) creates a fresh `Framebuffer`, attaches the *specific requested face* via
`kCubeFaceTargets[face]` as a 2D color attachment, and reads back with `glReadPixels` — again, no
state shared across faces (a new FBO per call), so a read of face N cannot accidentally return face
M's data through leftover framebuffer binding state.

`kSize=2` (2×2 per face, 4 pixels) plus `mipMap=false` maps to `EasyGLTextureCubeBackend`'s
constructor computing `levelCount=1` (line 155, since `mipMap` is false) — matches the test's own
non-mipmapped scope; no mip-level ambiguity to account for in this particular file (covered
separately by the sibling `easygl_texturecube_mip_test.cpp`, audited in this same batch).

### Testing
`check()` (lines 69-76) prints both the expected and actual color for every individual pixel check
(24 total: 6 faces × 4 pixels) and accumulates a single `result_` flag — a reasonable "fail loud, keep
going" diagnostic pattern that surfaces every mismatching pixel in one run rather than stopping at the
first failure, useful for triage if a real regression did land.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — this is a small, direct, correctly-scoped test with no
discrepancy found between its stated intent and its actual assertions, and its production-code
dependency (`EasyGLTextureCubeBackend::SetData`/`GetData`) was read directly and confirmed to behave
as the test assumes.

## Cross-File Observations

- `gdm_->setPreferredBackBufferWidthProperty(1)` / `...HeightProperty(1)` (lines 119-120) minimizes
  the on-screen window/backbuffer since this test never actually renders anything (`Draw()` is a
  no-op, line 113) — a sensible resource-minimization choice for a pure data-upload/readback test,
  consistent with its sibling `easygl_texturecube_mip_test.cpp`/`easygl_texturecube_partial_rect_test.cpp`.
- Shares its `kFaces`/color-palette/`colourEq` pattern nearly verbatim with
  `easygl_texturecube_mip_test.cpp` and `easygl_texturecube_partial_rect_test.cpp` — a reasonable,
  low-risk duplication across sibling single-purpose test files rather than a shared-header
  abstraction, consistent with this project's own stated convention (per `PixelTestGame.hpp`'s header
  comment) of not retrofitting every existing example into shared infrastructure.

## Missing or Weak Tests

The 6-arg rect/level-based `SetData`/`GetData` overload is intentionally **not** exercised here — that
gap is explicitly and correctly closed by the sibling `easygl_texturecube_partial_rect_test.cpp`
(Task 275, audited separately in this batch), whose own header comment states this exact division of
labor. No unintended gap found.

## Positive Findings

- Two-phase write-all-then-verify-all ordering is a deliberate, effective design choice for catching
  cross-face bleed, not an accidental byproduct of the code's structure.
- Minimal backbuffer size correctly reflects that this test performs no actual rendering.
- Directly traced through to the real GL-level implementation and confirmed no shared mutable state
  exists between per-face `SetData`/`GetData` calls that could produce a false pass.

## Final Assessment

A small, correct, well-targeted round-trip test; its production-code dependency was independently
verified to genuinely isolate per-face state, giving high confidence the test's pass result reflects
real per-face correctness rather than an accidental non-discriminating check.
