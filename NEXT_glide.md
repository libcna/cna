# Glide backend continuity

## Session status

- **Branch:** `feature/glide`.
- **Authoritative plan:** [`plan_glide.md`](plan_glide.md).
- **Scope:** authentic 32-bit Windows/x86 Glide 3.x backend only. Do not add a software or modern-API fallback.
- **Session focus:** closed every item in plan_glide.md's "Next implementation work" section that
  does not require the external i686 `sharp-runtime`/dgVoodoo dependency: GLIDE-AUD-004, -009,
  -010, -011, -012, -013, -014, -015. The only items still open there (GLIDE-AUD-006 follow-up,
  GLIDE-AUD-007 execution, the sub-texel LOD phase validation) are explicitly blocked by that
  dependency and were not touched. The "Future authentic Glide capability roadmap" section was not
  worked this session beyond one documentation sync (GLIDE-FUT-003's staged note, since GLIDE-AUD-004
  landed inside it).

## Established state

- The backend dynamically loads an external `glide3x.dll`, submits native fixed-function work, and is intentionally limited to the documented Glide subset.
- The local fake DLL / loader test is intended to work independently of CNA, SDL, sharp-runtime and dgVoodoo. Visual smoke validation still needs a compatible external Glide runtime.
- **Confirmed durable, not just "may remain":** the sibling `../sharp-runtime` `System::Int128`/`Decimal` types require the GCC/Clang `__int128` extension, and i686-w64-mingw32-g++ 14 genuinely does not support `__int128` for the 32-bit target (`error: expected primary-expression before '__int128'`, verified directly this session with a standalone repro). sharp-runtime's own CLAUDE.md documents this as an explicit, permanent, accepted 2026-07-11 decision (not implementing hand-rolled 128-bit arithmetic as a workaround). This means the full i686 CNA executable — and therefore fake-DLL renderer-sequence tests and any dgVoodoo/real-hardware visual validation — stays blocked pending either a policy change on that decision or an equivalent prebuilt x86 CNA smoke binary. Do not attempt to work around it from the CNA side; it is out of this subsystem's scope.

## Completed in this session (2026-08-03)

All eight are individually committed (`git log --oneline` on `feature/glide`), verified with
i686 MinGW `-fsyntax-only` recompilation of the whole `GlideGraphicsBackend.cpp` plus a standalone
native g++ build/run of every portable Glide gtest file (44/44 passing at session end).

- **GLIDE-AUD-004** — `TransformGlideLightingNormal()` was dotting the normal against columns of
  the inverse World 3×3 (computing `n * World^-1`) instead of rows (`n * World^-1^T`), invisible to
  the existing diagonal-scale probe since diagonal matrices are symmetric. Fixed the indexing; added
  rotation, hand-derived-shear, and perpendicularity-invariant probes that fail pre-fix and pass
  post-fix.
- **GLIDE-AUD-009** — the textured triangle/point/line paths looped tiles in the outer position,
  which could silently reorder two overlapping primitives whenever they sampled different physical
  tiles of the same multi-tile texture. Swapped to primitive-major traversal with a shared
  `boundTile` pointer that only rebinds/flushes on an actual tile change — zero behavioural or
  performance difference for the common single-tile case.
- **GLIDE-AUD-010** — confirmed via the actual Glide 3.0 Reference Manual (`grVertexLayout` /
  `GR_PARAM_STn`) that `GR_WINDOW_COORDS` s/t are native `[0..256]`-per-repeat units, not texel
  offsets or normalized UV. Added `GlideNativeTextureCoordinateScale()` and applied it in both the
  SpriteBatch quad path and the shared 3D `makeGlideVertex` (triangles/points/lines).
- **GLIDE-AUD-011** — `grAlphaBlendFunction`'s four argument slots have different, position-
  dependent legal value sets (confirmed via the Reference Manual); `GR_BLEND_SRC_COLOR`/
  `GR_BLEND_DST_COLOR` even share a numeric code. Added `ToGlideBlendFactor(Blend, GlideBlendSlot)`
  in a new portable header, plus a same-buffer-conflict check for
  `DestinationAlpha`/`InverseDestinationAlpha`/`SourceAlphaSaturation` while depth buffering is on
  (also documented in the Reference Manual).
- **GLIDE-AUD-012** — indexed draws expanded into a fresh `GlideVertexBufferBackend` and called
  plain `SetData()`, which re-guessed the vertex layout from stride instead of reusing the source
  buffer's already-resolved layout. Added `SetDataWithLayout()` to carry it forward exactly.
- **GLIDE-AUD-013** — startup selected display *dimensions* by area but always opened a hardcoded
  60 Hz refresh, never actually checked against the chosen candidate. Extracted portable
  `SelectGlideDisplayMode()` that keeps each candidate's resolution and refresh paired.
- **GLIDE-AUD-014** — triangle/polygon clipping was missing the positive-W eye-plane margin that
  point/line clipping already had, so a vertex with `clipX=clipY=clipZ=0` could survive all six
  nominal frustum planes at `W == 0` and throw in `makeGlideVertex`. Added the same margin plane;
  moved the shared Sutherland-Hodgman clipper into `GlidePrimitiveClip.hpp` alongside the segment
  clipper for portable testability.
- **GLIDE-AUD-015** — audited all four named categories (blend/depth/cull/alpha-test). Blend and
  depth were fixed as part of GLIDE-AUD-011. Cull was already atomic by construction. Alpha-test's
  native push in `DrawPrimitiveRange()` was moved to after every throw-capable validation in that
  function, since it was previously being committed to native Glide before checks that can still
  abort the draw.

New portable headers/tests added this session (all under
`include/CNA/Internal/Backends/Glide/` and `tests/CNA/Internal/Backends/Glide/`):
`GlideBlendFactor.hpp`, `GlideDisplayModeSelection.hpp`, `GlideTextureCoordinate.hpp`, plus new
probes in `GlidePrimitiveClipTests.cpp` and `GlideLightingTests.cpp`.

## Rules and assumptions

- Use `plan_glide.md` together with this file for all Glide work. `NEXT.md` is outside this subsystem's continuity scope.
- Prefer testable x86 fake-DLL contracts and portable unit tests. Do not claim dgVoodoo or real-Voodoo visual validation without actually running it.
- Preserve unrelated working-tree changes and make only focused Glide/backend/documentation edits.
- When a fix's core logic is pure CPU math (clipping, lighting, blend-factor mapping, coordinate
  conversion, display-mode selection), extract it into its own small header under
  `include/CNA/Internal/Backends/Glide/` with a matching portable gtest file, mirroring the
  existing `GlideLighting.hpp`/`GlidePrimitiveClip.hpp`/`GlideTextureMip.hpp`/`GlideVertexLayout.hpp`
  pattern — this is the only way to get real, executable regression coverage while the external
  i686 dependency stays blocked.
- When fixing an audit-flagged bug, prefer proving the regression empirically (temporarily revert
  the fix, show the new test fails, restore the fix, show it passes) over asserting correctness by
  inspection alone, where practical.

## Validation in this session

- `g++ -std=c++23 -pthread -Iinclude -Ivendor/googletest/googletest/include -Ivendor/googletest/googletest tests/CNA/Internal/Backends/Glide/*.cpp vendor/googletest/googletest/src/gtest-all.cc vendor/googletest/googletest/src/gtest_main.cc -o <bin> && <bin>` — 44/44 passing (GlideLightingTest ×9, GlideTextureMipTest ×5, GlidePrimitiveClipTest ×8, GlideBlendFactorTest ×9, GlideDisplayModeSelectionTest ×8, GlideTextureCoordinateTest ×5).
- `i686-w64-mingw32-g++ -std=c++23 -fsyntax-only -DCNA_BACKEND_GLIDE -Iinclude -I/usr/local/include -I../sharp-runtime/include src/CNA/Internal/Backends/Glide/GlideGraphicsBackend.cpp` — clean, after every commit.
- `vendor/googletest` submodule was not yet initialized at session start; ran `git submodule update --init vendor/googletest` once (small, pinned, matches the project's own established test infra — not a "large dependency" re-clone).
- `git diff --check` clean at every commit.

## Next action

The next practical, non-blocked work is in the "Future authentic Glide capability roadmap"
section of `plan_glide.md` — e.g. GLIDE-FUT-004 (second TMU), GLIDE-FUT-007 (per-format texture
encoding), or GLIDE-FUT-008/009/010 (depth bias, dither, gamma). Each of those is a genuine new
capability proposal (not a bug fix), needs its own design/scope confirmation before implementation,
and still needs the same blocked fake-DLL/dgVoodoo validation to be marked release-complete. Until
the sibling `sharp-runtime` i686 `__int128` dependency is resolved (see "Established state" above),
prioritize portable, CPU-side, testable work over anything that claims runtime/visual validation.
