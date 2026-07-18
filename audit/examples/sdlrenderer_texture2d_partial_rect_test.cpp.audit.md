# Audit: examples/sdlrenderer_texture2d_partial_rect_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_texture2d_partial_rect_test.cpp` (147 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 679, verifies `Texture2D::SetData`'s partial-
  rectangle overload actually produces correct GPU-visible pixels on SDL_Renderer.
- CMake registration: `cna_sdl_test(cna_test_sdl_texture2d_partial_rect
  examples/sdlrenderer_texture2d_partial_rect_test.cpp)` / `SDL_Renderer_Texture2D_PartialRect` —
  confirmed at `cmake/Tests/SdlRendererTests.cmake:115-117`.
- XNA/FNA relevance: direct — `Texture2D.SetData(int level, Rectangle? rect, T[] data, int startIndex,
  int elementCount)` (FNA `Texture2D.cs`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp` (`SetData(level, rect, …)`,
  lines 245-316); `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SdlTextureBackend::UpdatePixels`, lines 45-49).

## Purpose

Builds a 4x4 texture, fills it entirely Red via `SetData(0, nullptr, …)`, then writes a 2x2 Blue sub-rect
at `(1,1)` via the same overload with an explicit `Rectangle`. The file's own comment correctly identifies
the genuinely backend-specific question: `Texture2D::SetData`'s partial overload merges the sub-rect into
the *full* per-level CPU buffer, then calls `SdlTextureBackend::UpdatePixels()`, which always re-uploads
the **whole** merged buffer via `SDL_UpdateTexture(texture, nullptr /* whole texture */, …)` rather than
just the sub-rect — this test confirms that "always re-upload everything" strategy produces the correct
end-to-end GPU result via a real `SpriteBatch` draw + framebuffer readback, an 8-point pixel check (4
untouched-Red corners + 4 Blue sub-rect texels).

## Executive Verdict

**Healthy** — this is a genuine, non-trivial pixel test that actually exercises the real re-upload path,
not merely a CPU-cache round trip; a minor unused-include nit is the only observation.

## Checklist Results

### API / XNA / FNA parity
`Texture2D::SetData(0, nullptr, allRed.data(), 0, 16)` (line 79) and
`Texture2D::SetData(0, &subRect, blue4, 0, 4)` (line 83) both correctly use the 5-argument overload's
signature/order matching FNA's `SetData<T>(int level, Rectangle? rect, T[] data, int startIndex, int
elementCount)`. Cross-checked `Texture2D.cpp`'s bounds validation (lines 258-267): `subRect(1,1,2,2)`
against a 4x4 level (`x+w=3<=4`, `y+h=3<=4`) is correctly within bounds, and `elementCount(4) >= w*h(4)`
passes the size check.

### Behavioral correctness
Traced the actual re-upload mechanism end to end: `SetData`'s merge loop (`Texture2D.cpp` lines 283-294)
computes `src = startIndex + row*w + col` against the 2x2 `blue4` array and writes into `dst =
((y+row)*levelW + (x+col))*4` inside the *full* 4x4 CPU buffer (`buf`, obtained via `getMipBuffer(0)`) —
correctly only touching the 4 texels at grid positions (1,1),(2,1),(1,2),(2,2), leaving the Red border
texels in the same buffer untouched. Since `level==0` and `backend_` is non-null, line 298-299 calls
`backend_->UpdatePixels(buf.data(), levelW*4)` — confirmed in `SdlGraphicsBackend.cpp` (line 45-49) to
call `SDL_UpdateTexture(texture, nullptr, rgba, stride)` with `nullptr` for the destination rect, i.e.
literally re-uploading the entire merged 4x4 buffer (both the untouched Red texels and the newly-written
Blue ones) in one call — exactly the "always re-upload the whole buffer" behavior the file's own comment
describes and the 8-point check (4 Red corners, 4 Blue center texels) is precisely designed to catch a
regression in.

### Logic
The expected layout in the comment (`R R R R / R B B R / R B B R / R R R R`, lines 24-27) matches the
actual `subRect(1,1,2,2)` placement exactly — verified by hand: sub-rect spans grid columns 1-2, rows 1-2
(0-indexed), which is precisely the interior 2x2 of a 4x4 grid, leaving a 1-texel Red border on all sides,
matching the ASCII diagram exactly.

### C++ correctness
`std::vector<Color> allRed(16, kRed)` (line 78) and the fixed 4-element `blue4[4]` array (line 82) are both
correctly sized against their respective `elementCount` arguments (16 and 4). No unsafe casts or lifetime
concerns — both are stack/heap-owned for the duration of the (synchronous) `SetData` calls.

### Memory/resource lifetime
`gdm_`/`sb_`/`tex_` are `unique_ptr` members with normal RAII lifetime tied to the `Game` object; nothing
unusual here.

### Performance / Thread safety
N/A — one-shot CTest executable, single-threaded, not a hot path.

### Architecture
Correctly XNA-facing throughout — only public `Texture2D`/`SpriteBatch`/`GraphicsDevice` API is used; the
test does not reach into `SdlTextureBackend` directly, appropriate for an integration-level pixel test.

### Maintainability
147 lines, single responsibility, ASCII-art expected layout in the header comment is a nice touch for
readability. See F1 for one small nit.

### Portability
Correctly requires `PresentationMode::NativeBackBuffer` (line 136), consistent with every other file in
this batch, for the same `SDL_RenderReadPixels` physical/logical mapping reason documented in the header.

### Robustness
N/A beyond what's covered — positive-path pixel test, not an error-injection test.

### Testing
This file is itself a test; see Missing or Weak Tests below.

### Cross-file consistency
Directly complements the sibling `sdlrenderer_texture2d_startindex_test.cpp` (Task 680), which the same
comment style explicitly labels as "mirroring Task 679" — both share the identical "merge into full
buffer, re-upload whole buffer, verify via real readback" methodology, just exercising different SetData
sub-features (rectangle sub-region here vs. `startIndex`/`elementCount` array slicing there). Consistent
and non-redundant.

## Detailed Findings

### F1 — Unused `#include "Microsoft/Xna/Framework/Vector2.hpp"`

- Severity: LOW
- Confidence: HIGH
- Category: maintainability
- Location/symbol: line 36
- Evidence: `grep -n "Vector2" examples/sdlrenderer_texture2d_partial_rect_test.cpp` finds only the
  `#include` line itself — `Vector2` is never referenced anywhere in the file's actual code.
- Why it matters: purely cosmetic (no behavioral or build-time cost beyond a marginal extra header parse);
  worth a one-line cleanup, not a functional concern.
- FNA/XNA comparison: N/A.
- Related files: none.
- Suggested future action (not implemented by this audit): remove the unused include.

## Cross-File Observations

- The same unused `Vector2.hpp` include appears in the sibling `sdlrenderer_texture2d_setdata_getdata_test.cpp`
  (line 33) — likely both copy-pasted from a common template that once used `Vector2` for a different
  draw-position parameter; worth a single cleanup pass across the shard rather than two separate fixes.

## Missing or Weak Tests

None beyond F1's cosmetic nit — the 8-point check (4 border + 4 interior) is a well-chosen, sufficient
sample set to catch both "sub-rect not written" and "sub-rect write corrupted the border" failure modes
independently.

## Positive Findings

- Genuinely exercises the real re-upload code path (not a CPU-only round trip) with a real `SpriteBatch`
  draw and framebuffer readback — correctly targets the actual backend-specific risk the file's own
  comment identifies.
- The 8-point check design (corners + interior) is well-chosen to independently catch both "nothing
  written" and "wrong region written" failure modes.

## Final Assessment

A solid, correctly-targeted pixel test with only a cosmetic unused-include nit; no correctness issues
found in either the test or the production code path it exercises.
