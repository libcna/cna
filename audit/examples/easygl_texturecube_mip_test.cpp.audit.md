# Audit: examples/easygl_texturecube_mip_test.cpp

## Metadata

- Source file: `examples/easygl_texturecube_mip_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 276, `TextureCube` mip-level `SetData`/`GetData`
  round-trip, all six faces
- File type: hand-rolled `Game`-subclass executable, CTest-registered as
  `cna_test_easygl_texturecube_mip` (`cmake/Tests/EasyGLTests.cmake:846-847`).
- XNA/FNA relevance: `TextureCube.SetData(face,level,rect,...)`,
  `TextureCube.GetData(face,level,rect,...)`, `CubeMapFace` — real XNA 4.0 API.
- Related production code: `TextureCube::SetData`/`GetData` (6-arg, level-based overload,
  `src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp:144-212`);
  `EasyGLTextureCubeBackend` constructor's per-level GPU storage pre-allocation
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:147-174`).

## Purpose

Creates a 4×4 mipmapped `TextureCube` (3 levels: 4×4, 2×2, 1×1), writes each face's mip 0 to that
face's own distinctive color and mips 1/2 to two colors shared across all faces (White, Orange) as
cross-contamination markers, then reads back and verifies every face×level combination — designed
specifically to catch a per-face-vs-per-level indexing bug that a whole-face-only test (like the
sibling `easygl_texturecube_faces_test.cpp`) could not detect.

## Executive Verdict

**Healthy.** The test's stated goal (catch cross-face *and* cross-level bleed) is genuinely served by
its write-then-verify-all structure, and the underlying backend's per-face, per-level GPU storage
pre-allocation (confirmed by direct source reading) supports exactly the level-indexed access pattern
this test exercises.

## Checklist Results

### Behavioral correctness
Mip-chain math: `TextureCube(dev, 4, mipMap=true, SurfaceFormat::Color)` → 3 levels (`4×4→2×2→1×1`,
`CalculateMipLevels`-equivalent logic in both `TextureCube.cpp` and the EasyGL backend, cross-checked
and found consistent — `CalculateCubeMipLevels` in `EasyGLGraphicsBackend.cpp` lines 139-145 produces
the identical level count as `TextureCube.cpp`'s own `CalculateMipLevels`, both looping `while (s>1) {
s=max(1,s/2); ++levels; }` starting from `levels=1`).

Per-face writes (lines 89-96): `base16` (this face's own color, 16 px for mip 0), `white4` (shared,
4 px for mip 1), `orange1` (shared, 1 px for mip 2) — the choice to use **shared** markers for mips 1/2
(rather than per-face-distinct colors at every level) is a deliberate simplification that still
catches the two bug classes the header comment names: a per-face indexing bug would show one face's
mip-0 color appearing in another face's mip-0 readback (still distinguishable, since mip 0 colors
*are* per-face-distinct); a per-level indexing bug would show White/Orange swapped or appearing at the
wrong level (still distinguishable, since White/Orange only ever belong at levels 1/2 respectively,
never at level 0 which is always the face's own color).

Two-phase structure — every face's entire mip chain written first (lines 90-96), verification of
every face/level only afterward (lines 99-130) — mirrors the same effective anti-bleed discipline
already found correct in `easygl_texturecube_faces_test.cpp`'s audit, extended here across the mip
dimension too.

### Cross-file consistency
Backend pre-allocation (`EasyGLTextureCubeBackend`'s constructor, lines 147-174): explicitly loops
over all 6 `kCubeFaceTargets` and, for **each** face, calls `set_image_2d` for **every** level from 0
to `levelCount-1` with `nullptr` pixel data (allocating storage without initial content) — this is
exactly the prerequisite the header comment attributes to "Task 276 finding" (the constructor's own
in-code comment, line 152-154, states this explicitly: "without this loop, `SetData(level>0,...)`
would silently fail"). This audit independently confirms the fix this test was written to verify is
present and structurally sound: each of the 6×3=18 face/level combinations gets its own
`glTexImage2D` call before any `SetData` (which uses `glTexSubImage2D`, requiring a pre-existing image
at that level) is ever issued.

`SetData`/`GetData` at the `TextureCube.cpp` level (lines 144-212) validate `level>=0`, compute
`levelSize = mipDim(size_, level)` per-level (correctly halving down from the base `size_`, matching
this test's own 4→2→1 progression), and forward to the backend with the resolved level — independently
traced and found consistent with what this test assumes.

### Robustness
`GetData` calls use a sentinel fill (`kGray`, distinct from every color actually written) for the
destination buffer before each readback (lines 104, 114, 124) — ensures a `GetData` that silently no-
ops (writes nothing) would be caught as a mismatch against the expected color rather than accidentally
matching a coincidentally-pre-existing value.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — the test's structure genuinely matches its stated anti-bleed goal,
and its core dependency (per-face-and-level GPU storage pre-allocation) was independently confirmed
present and correct in the backend.

## Cross-File Observations

- Directly validates the fix described inline in `EasyGLTextureCubeBackend`'s own constructor comment
  ("Task 276 finding") — a genuine, traceable production-code-to-test correspondence, not a test whose
  target fix could not be located.
- Complements (without duplicating) `easygl_texturecube_faces_test.cpp` (whole-face-only, no mip
  dimension) and `easygl_texturecube_partial_rect_test.cpp` (partial-rect writes, audited separately in
  this batch) — the three files together cover whole-face, mip-level, and partial-rect axes without
  redundant overlap.
- `mipDim(base, level) = std::max(1, base >> level)` (`TextureCube.cpp` line 34-37) correctly floors at
  `1` rather than reaching `0` at the final level, matching this test's `1×1` mip-2 expectation.

## Missing or Weak Tests

None specific to this file's own scope. A theoretical addition (not required) would be a case where a
level's rect argument is combined with a non-zero level on a *different* face in the same run to more
aggressively fuzz face×level combinations, but the current systematic all-face×all-level sweep already
provides strong coverage for the specific bug class this file targets.

## Positive Findings

- Test structure (shared cross-face markers at non-zero levels, per-face-distinct color at level 0)
  is an efficient, well-reasoned design that still discriminates both targeted bug classes without
  needing 18 entirely-distinct colors.
- Directly corresponds to, and verifies, a specific in-code "Task 276 finding" comment in the
  production backend — a clean, traceable test-to-fix relationship.
- Sentinel-fill-before-readback pattern applied consistently at every verification site.

## Final Assessment

A well-targeted, correctly-structured test whose anti-bleed design was verified to genuinely match its
production-code dependency's actual per-face-and-per-level storage model; no defects found.
