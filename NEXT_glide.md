# Glide backend continuity

## Post-audit integration continuity (2026-08-08)

- **Original branch:** `feature/glide` at
  `2f9b47e1281590e6735b5f76ef1e13dd781d8981`; preserve it unchanged.
- **Adaptation branch:** `adapt/glide`, based on `integration/post-audit-phase1` at
  `0a51f8647eb4ddf2fdcd2102756ea79bb49625b7`.
- **Archive:** annotated `archive/preintegration/glide-20260804`, preserving the original head,
  fork `a7a49e3dc135cd3394b04dbc761123584b4e1d45`, and all 32 unique commits.
- **Authoritative plan:** [`plans/plan_glide.md`](plans/plan_glide.md), especially its post-audit adaptation
  record and capability boundaries.
- **Scope:** authentic 32-bit Windows/x86 Glide 3.x only. Never add a software or modern-API
  fallback. The host had neither physical Voodoo hardware nor an external `glide3x.dll`; the
  production renderer is build-only here, while the fake-DLL ABI contract is a test-double wrapper
  result, not a rendering result.
- **Adaptation findings:** `REMED-GFX-226`, `REMED-GFX-227`, and `REMED-GFX-228` are MEDIUM and
  resolved. The current stream-array contract, separate depth/stencil truth, texture pitch units,
  deferred lifetime, TMU residency, and per-slot sampler behavior are recorded in the plan.
- **Validated:** 78/78 portable tests, 13/13 shared contracts, 39-export x86 ABI contract,
  i686 whole-backend syntax, 78/78 ASan/UBSan, and five serial OPENGLES pixel/state controls.
- **Remaining external boundary:** a full CNA i686 executable remains unavailable until the
  sibling `sharp-runtime` can satisfy its i686 ZLIB and `__int128` requirements. A real emulator
  or hardware rendering matrix remains unexecuted and must never be inferred from the ABI test.

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

New portable headers/tests added in the AUD batch (all under
`include/CNA/Internal/Backends/Glide/` and `tests/CNA/Internal/Backends/Glide/`):
`GlideBlendFactor.hpp`, `GlideDisplayModeSelection.hpp`, `GlideTextureCoordinate.hpp`, plus new
probes in `GlidePrimitiveClipTests.cpp` and `GlideLightingTests.cpp`.

## Completed in the Future-roadmap batch (2026-08-03/04)

Each individually committed; the portable Glide unit suite grew from 44 to 65 tests across this
batch, all passing after every commit.

- **GLIDE-FUT-015** — `SpriteBatch` quads now batch through `Impl::pendingSpriteTriangles` /
  `grDrawVertexArrayContiguous` instead of one immediate `grDrawTriangle` pair per sprite per tile,
  rebinding TMU0 only when the tile/sampler actually changes. Every `GlideGraphicsBackend` method
  that mutates state read at flush time, or issues its own native draw/clear/present/readback, now
  calls `Impl::FlushSpriteBatch()` first, so the deferred queue can never be reordered relative to
  another native submission.
- **GLIDE-FUT-011** — `grGetString`/`grGetProcAddress` resolved as two more required core exports
  (39 total). New portable `GlideExtensionCapabilities.hpp` parses `GR_EXTENSION`'s space-separated
  list; `Impl::QueryRuntimeCapabilities()` also queries hardware/renderer/vendor/version at startup.
  Detection/reporting only (`CNA_GLIDE_DIAGNOSTICS=1`) -- no specific extension is adopted yet.
  Verified further than usual: built the fake DLL + `GlideAbiLoaderTests.cpp` with i686 MinGW and
  ran it under Wine outside a build directory, exit code 0.
- **GLIDE-FUT-013** — `AllocateTexture()` now retries texture-memory exhaustion by evicting the
  least-recently-used *other* resident texture (new portable `GlideTextureEviction.hpp` /
  `SelectGlideEvictionVictim`) before giving up. `GlideTextureBackend` implements
  `IGlideResidentTexture` and self-registers/unregisters. Found and fixed a real hazard composing
  this with FUT-015's deferred SpriteBatch queue: `EnsureAddressMode()`, `UpdatePixels()`,
  `UpdatePixelsLevel()`, and the eviction branch of `AllocateTexture()` all now flush the pending
  sprite batch before touching native tile state, so a queued-but-unsubmitted quad can never render
  against texture content it wasn't actually queued against.
- **GLIDE-FUT-007** — new portable `GlideTextureFormat.hpp` classifies each texture's ARGB4444
  alpha coverage (Opaque/Binary/Fractional) and, opt-in via `CNA_GLIDE_ADAPTIVE_TEXTURE_FORMAT=1`,
  re-packs it as RGB565 or ARGB1555 instead of always ARGB4444 -- no reallocation needed, since all
  three are 16-bit formats (confirmed against the Glide 3.0 Reference Manual).
- **GLIDE-FUT-004** — second TMU support for `DualTextureEffect`, explicitly scoped (project-owner
  decision) to single-tile texture0/texture1 sharing identical dimensions, since this backend's
  vertex-decl parser only accepts one `TextureCoordinate0` semantic and TMU0/TMU1 therefore
  necessarily share native s/t. `Impl::ConfigureDualTextureCombiner()` reproduces FNA's
  `DualTextureEffect.fx` chain natively (TMU1 upstream of TMU0, chained multiply into the final
  iterated stage); the CPU-side `color.rgb *= 2` step (no native Glide "x2" combiner stage exists)
  is folded into the already-CPU-computed iterated RGB, proven exactly equivalent by associativity.
  Every unsupported combination (no second TMU, mismatched dimensions, non-triangle primitives,
  a tiled texture0) throws a specific error instead of misrendering. No new portable header was
  needed -- this is native-call sequencing plus one scalar multiply, not new branchy CPU math.

## Rules and assumptions

- Use `plans/plan_glide.md` together with this file for all Glide work. `NEXT.md` is outside this subsystem's continuity scope.
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

## Validation

- Portable Glide suite command (excludes `FakeGlide3xDll.cpp`/`GlideAbiLoaderTests.cpp`, which need
  i686 MinGW + Wine, and `GlideVertexLayoutTests.cpp`, which needs the full `SHARP_RUNTIME`/CMake
  link and is not part of this ad hoc suite):
  `g++ -std=c++23 -pthread -Iinclude -I../sharp-runtime/include -Ivendor/googletest/googletest/include -Ivendor/googletest/googletest $(ls tests/CNA/Internal/Backends/Glide/*.cpp | grep -v -E "FakeGlide3xDll|GlideAbiLoaderTests|GlideVertexLayoutTests") vendor/googletest/googletest/src/gtest-all.cc vendor/googletest/googletest/src/gtest_main.cc -o <bin> && <bin>`
  — 65/65 passing as of GLIDE-FUT-004 (9 suites: GlideLighting, GlideTextureMip, GlidePrimitiveClip,
  GlideBlendFactor, GlideDisplayModeSelection, GlideTextureCoordinate, GlideExtensionCapabilities,
  GlideTextureEviction, GlideTextureFormat).
- `i686-w64-mingw32-g++ -std=c++23 -fsyntax-only -DCNA_BACKEND_GLIDE -Iinclude -I/usr/local/include -I../sharp-runtime/include src/CNA/Internal/Backends/Glide/GlideGraphicsBackend.cpp` — clean, after every commit.
- `vendor/googletest` submodule was not yet initialized at the start of the AUD batch; ran
  `git submodule update --init vendor/googletest` once (small, pinned, matches the project's own
  established test infra — not a "large dependency" re-clone).
- `git diff --check` clean at every commit.

## Future backend work

All five items the project owner picked as "most valuable non-blocked" from the Future roadmap
(GLIDE-FUT-004, -007, -011, -013, -015) are now `[x]`. What remains open in that section is either
partially staged already or genuinely needs one of the two blockers below before it can progress
further:

- **GLIDE-FUT-005** (sampler mip controls) — LOD bias is done; `MaxMipLevel` stays explicitly
  rejected pending a tile-invariant mapping design (see its "Audit (2026-08-01)" note).
- **GLIDE-FUT-006** (partial `SetData`) — functionally staged; only a performance pass (regenerate
  just the affected derived levels instead of the full shared pyramid) remains.
- **GLIDE-FUT-008/009/010** (depth bias calibration, dither/AA, gamma) — each explicitly needs
  either real Voodoo/dgVoodoo hardware calibration or a new CNA-level opt-in design decision before
  any Glide-side code should be written; do not guess at the numeric mapping.
- **GLIDE-FUT-012** (NPOT) — now unblocked by GLIDE-FUT-011's extension negotiation, but needs a
  real advertised NPOT extension name to gate on, plus mip/wrap/clamp/mirror verification before
  preferring it over the existing tiled path.
- **GLIDE-AUD-006 follow-up / GLIDE-AUD-007** — still blocked on the sibling `sharp-runtime` i686
  `__int128` dependency (see "Established state" above): the real CNA renderer cannot be
  instantiated in the i686 target, so the fake-DLL recorder can't yet assert real draw/texture/
  clear sequences, and no dgVoodoo/real-Voodoo visual capture is possible for anything in this file.

Until that sharp-runtime blocker is resolved, keep prioritizing portable, CPU-side, testable work
over anything that would need to claim runtime/visual validation.
