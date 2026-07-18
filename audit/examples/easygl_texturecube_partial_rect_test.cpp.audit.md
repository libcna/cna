# Audit: examples/easygl_texturecube_partial_rect_test.cpp

## Metadata

- Source file: `examples/easygl_texturecube_partial_rect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 275, `TextureCube` face upload/readback: partial
  rect and `startIndex`, all six faces
- File type: hand-rolled `Game`-subclass executable, CTest-registered as
  `cna_test_easygl_texturecube_partial_rect` (`cmake/Tests/EasyGLTests.cmake:852-853`); also reused
  verbatim for Vulkan (`cmake/Tests/VulkanTests.cmake:391`) and Bgfx
  (`cmake/Tests/BgfxTests.cmake:534`) — backend-agnostic public-API code despite the `easygl_`
  filename prefix.
- XNA/FNA relevance: `TextureCube.SetData(face,level,rect,data,startIndex,count)`,
  `TextureCube.GetData(face,level,rect,data,startIndex,count)` — real XNA 4.0 API.
- Related production code: `TextureCube::SetData`/`GetData` 6-arg overload
  (`src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp:144-212`);
  `EasyGLTextureCubeBackend::SetData`/`GetData`
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:210-241`).

## Purpose

Three sub-tests closing a coverage gap the sibling `easygl_texturecube_faces_test.cpp` explicitly does
not cover: (A) a genuine sub-rectangle write/read round-trip across all six faces (an off-center 2×2
white rect inside a 4×4 face), (B) `SetData` with a non-zero `startIndex` into a rect-based write, and
(C) `GetData` with a non-zero `startIndex` reading out of a rect-based region — each verifying real
pixel values, not just that the calls don't throw.

## Executive Verdict

**Healthy.** All three sub-tests' index arithmetic was independently re-derived from the row-major
pixel-index convention (`i = y*width + x`) and found to match both the test's own expected-value
tables and the production `TextureCube::SetData`/`GetData` implementation's actual index handling.

## Checklist Results

### Behavioral correctness

**Sub-test A** (lines 98-135): background-fills all 6 faces with their own distinct color via the
simple whole-face `SetData` overload, then writes a `Rectangle(1,0,2,2)` White rect into every face
via the 6-arg overload, then verifies via a whole-face `GetData` that only pixels `{1,2,5,6}` (i.e.
`(1,0),(2,0),(1,1),(2,1)` in a 4-wide row-major layout) are White and the remaining 12 are that face's
own background — this audit independently re-derived the same four linear indices from the rect's
`(x=1,y=0,w=2,h=2)` geometry (`idx = y*4+x` for `y∈{0,1}, x∈{1,2}` → `{1,2,5,6}`) and confirms the
test's `whiteIdx` array is correct. Verification happens only *after* all six faces are written
(mirroring the anti-bleed discipline already found correct in the sibling `easygl_texturecube_faces_
test.cpp`/`easygl_texturecube_mip_test.cpp` audits), so a face-indexing bug in the rect-write path
would be caught even if it happened to write the right pixels within the wrong face.

**Sub-test B** (lines 140-164): background = solid Red (whole face). `src6 = {Green, Green, Blue,
Blue, Green, Green}`, `rect2x1 = Rectangle(1,0,2,1)`, `SetData(PositiveX, 0, &rect2x1, src6,
startIndex=2, elementCount=2)` — should consume only `src6[2..3]` (`Blue, Blue`) into the 2-pixel
rect. This audit independently confirms the expected result (`Blue` at linear indices `1,2` — i.e.
`(1,0)` and `(2,0)`, `Red` everywhere else) matches `inRect = (i==1 || i==2)` (line 158) exactly.

**Sub-test C** (lines 169-190): reuses sub-test B's exact setup, then calls `GetData(PositiveX, 0,
&rect2x1, out.data(), startIndex=1, elementCount=2)` into a 4-element sentinel-filled (`kGreen`)
output array — should write only `out[1]` and `out[2]` (the same two rect pixels, `Blue, Blue`),
leaving `out[0]`/`out[3]` at their sentinel value. The expected array
`{kGreen, kBlue, kBlue, kGreen}` (line 183) matches this audit's independent trace of the `startIndex`
semantics through `TextureCube::GetData`'s implementation (`data[startIndex+i] = ...` for
`i∈[0,elementCount)`, `TextureCube.cpp` line 211 via `rgbaToColors`).

### Logic
All three sub-tests correctly use a **fresh** `TextureCube` instance per sub-test (lines 100, 142,
171) rather than reusing one across sub-tests — avoids any risk of a prior sub-test's state leaking
into a later one's assertions, and keeps each sub-test's own preconditions self-evident from its own
code rather than depending on execution order of unrelated blocks.

### Cross-file consistency
The rect-based `SetData`/`GetData` overload these sub-tests target forwards `x,y,w,h` straight to
`EasyGLTextureCubeBackend::SetData`/`GetData` (`glTexSubImage2D`/FBO-attach-then-`glReadPixels`
respectively) — independently confirmed in `EasyGLGraphicsBackend.cpp` lines 210-241 to use the exact
rect bounds passed through from `TextureCube.cpp`'s own bounds validation (`x,y,w,h` against
`levelSize`, `TextureCube.cpp` lines 158-164/198-204), with no off-by-one or coordinate-flip found
between the two layers for the specific rect geometries this test exercises.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — this audit independently re-derived every expected pixel-index
mapping used by all three sub-tests from first principles (row-major linear indexing over the rect's
own `(x,y,w,h)`) and found each one to match both the test's own assertions and the production
`SetData`/`GetData` code path's actual behavior.

## Cross-File Observations

- Confirmed reused verbatim for Vulkan (`cmake/Tests/VulkanTests.cmake:391`) and Bgfx
  (`cmake/Tests/BgfxTests.cmake:534`) — legitimate, since this file exercises only the public
  `Microsoft::Xna::Framework` `TextureCube` API with no EasyGL-specific assumptions (unlike
  `easygl_texture_mip_filter_effect_test.cpp`, which needed a backend-specific fork due to a real
  filter-mapping divergence).
- Explicitly and correctly scoped (per its own header comment, lines 3-9) as *not* re-testing what
  `easygl_texturecube_faces_test.cpp` and `TextureCubeTests.cpp`'s argument-guard tests already cover
  — whole-face round-trip and invalid-argument handling, respectively — avoiding duplicate coverage
  while still closing the specific rect/`startIndex` gap those files leave open.
- Shares the `kFaces`/color-palette/`colourEq` pattern with its two sibling `easygl_texturecube_*`
  files in this batch — consistent, not drifted (identical palette values, identical helper
  signatures).

## Missing or Weak Tests

None found for this file's stated scope. Sub-tests B/C only exercise the `PositiveX` face for the
`startIndex` behavior (not all six) — a reasonable simplification given sub-test A already established
whole-face-and-rect correctness is uniform across all six faces, and the `startIndex` arithmetic itself
is face-independent (it operates purely on the source/destination array offset, not on which GL cube-
map target is bound) — not a meaningful coverage gap.

## Positive Findings

- Three independent sub-tests each target a genuinely distinct code path (whole-face-rect, `SetData`
  startIndex, `GetData` startIndex) rather than one test vaguely covering "rects" in general.
- Correctly uses a fresh `TextureCube` per sub-test, avoiding cross-sub-test state coupling.
- Every expected-value table in the file was independently re-derivable from first principles and
  matched on the first check — no arithmetic surprises found during this audit's verification pass.

## Final Assessment

A thorough, correctly-scoped three-part test closing a real, explicitly-identified coverage gap left
by its whole-face-only sibling test; every index computation was independently verified against both
the test's own tables and the actual `TextureCube`/`EasyGLTextureCubeBackend` implementation.
