# Audit: examples/sdlrenderer_readback_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_readback_test.cpp` (117 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — foundational `GetBackBufferData` readback test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_readback …)` /
  `cna_register_backend_test(NAME SDL_Renderer_Readback …)`, `cmake/Tests/SdlRendererTests.cmake:20-24`. Header
  comment traces to Task 666 (`plans/plan_graphics.md` Tasks 666-861, the whole SDL_Renderer pixel-test audit phase).
- XNA/FNA relevance: `GraphicsDevice.GetBackBufferData` (`Microsoft::Xna::Framework::Graphics`), `SpriteBatch`
  draw-then-readback verification methodology used throughout this project's other backend test suites.
- Related production code: `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`ReadBackbuffer`, lines 591-641 — already audited in detail in
  `audit/src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp.audit.md`),
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`GetBackBufferData` overloads, lines 1768-1799+).

## Purpose

This is the "prerequisite of prerequisites" for the entire SDL_Renderer-specific pixel-verification test
methodology: before Task 666's fix, `SdlGraphicsBackend` never overrode `IGraphicsBackend::ReadBackbuffer` at
all, so every draw-then-readback test on this backend was structurally impossible (the shared default throws
`"ReadBackbuffer: not implemented in this backend"`). The test draws a 20×20 red sprite at `(10,10)` over a
green-cleared 64×64 canvas and reads back one pixel inside the sprite and one outside it, explicitly requesting
`PresentationMode::NativeBackBuffer` so `SDL_RenderReadPixels`'s physical-coordinate contract maps 1:1 to the
logical coordinates `GetBackBufferData` uses.

## Executive Verdict

**Healthy.** The test is small but genuinely discriminating: it can only pass if (a) the sprite actually lands
at the requested screen position with the requested size, (b) the readback correctly samples that exact pixel,
and (c) the background outside the sprite is untouched — a real assertion of `SpriteBatch::Draw` + `Clear` +
`GetBackBufferData` all working together, not a "compiles and doesn't crash" placeholder.

## Checklist Results

### API / XNA / FNA parity
`Texture2D::CreateFromPixels(device, 1, 1, red)` (line 62) and `SpriteBatch::Draw(Texture2D&, Rectangle
destinationRectangle, Rectangle sourceRectangle, Color)` (line 73) are exercised. The 4-argument
`Draw(Texture2D&, const Rectangle&, const Rectangle&, Color)` overload used here is the **non-optional-Rectangle**
overload declared `NOXNA` in `SpriteBatch.hpp` (lines 178-181) — a deliberate CNA convenience overload,
distinct from the real XNA-parity overload at lines 273-275 which takes `std::optional<Rectangle>
sourceRectangle`. This is not a defect (the NOXNA marking is correct per this project's own convention for a
non-XNA-signature overload of an XNA method), but it means this test (and every other file in this batch using
the same call pattern) exercises the CNA extension overload, not the literal XNA-signature one — worth noting
for completeness, not a finding.
`GraphicsDevice::GetBackBufferData(const Rectangle* rect, Color* data, int startIndex, int elementCount)`
(line 78, 82) matches FNA's own `GetBackBufferData<T>(Rectangle?, T[], int, int)` shape (modulo the
templated-vs-`Color*` simplification already established project-wide).

### Behavioral correctness
Re-derived the expected geometry by hand: canvas is 64×64, clear color `(0,255,0,255)` (green); sprite drawn at
destination `Rectangle(10,10,20,20)` from a 1×1 red source texture — so screen pixels `[10,30)×[10,30)` should
be red, everything else green. `insideReg(15,15,1,1)` (line 77) sits well inside `[10,30)` — a correct choice,
not edge-adjacent, so it isn't sensitive to any off-by-one in rect inclusivity. `outsideReg(0,0,1,1)` (line 81)
sits at the canvas origin, unambiguously outside the sprite. The tolerance thresholds (`>200`/`<50`, lines
84-85) are generous enough to absorb ordinary blend/rounding noise while still discriminating red from green
decisively.

### Logic
Two straight-line `GetBackBufferData` calls with no branching; `result_` is set once from the conjunction of
both checks (line 94) — correct aggregate pass/fail semantics matching this shard's established idiom
(`result_ = (a && b) ? 0 : 1`).

### Memory/resource lifetime
`redTex_`/`sb_` are `unique_ptr`-owned and constructed once in `Initialize()`; no explicit `Dispose()` call, so
cleanup relies on `Game`'s own shutdown path destructing `SdlRendererReadbackTest`'s members in reverse
declaration order — acceptable for a short-lived, single-frame test executable, consistent with every other
file in this shard.

### C++ correctness
No unsafe casts; `std::vector<uint8_t>` literal for the 1×1 red pixel (line 61) is correctly RGBA-ordered
`{255,0,0,255}` matching `Texture2D::CreateFromPixels`'s documented byte layout.

### Performance
N/A — single-frame test.

### Thread safety
N/A.

### Architecture
Correctly chooses `PresentationMode::NativeBackBuffer` rather than the default
`FixedHeightDynamicWidth` — independently verified against `SdlGraphicsBackend.cpp`'s
`ToSdlLogicalPresentation`-equivalent mapping (`CnaPresentationMode::NativeBackBuffer` → SDL's
`SDL_LOGICAL_PRESENTATION_DISABLED`, line 315) and `ReadBackbuffer`'s own physical/logical mismatch guard (lines
591-608) — the header comment's claim that this mode gives 1:1 correspondence, required for exact-pixel
readback, is accurate and matches the actual `SdlGraphicsBackend::ReadBackbuffer` implementation, not merely
asserted.

### Maintainability
117 lines; clear, minimal, single-purpose.

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
The test correctly checks **both** "inside is red" and "outside is still green" rather than only the former —
catching a hypothetical bug where the whole canvas got tinted red (e.g. an incorrect blend/clear order) that a
single-point check would miss.

### Testing
This file is itself the foundational readback test; no separate test file covers `GetBackBufferData` more
generally for this backend (the deeper edge cases — render-target-bound readback, post-unbind readback — are
covered by sibling files in this same shard, e.g. `sdlrenderer_rendertarget2d_construction_test.cpp` and
`sdlrenderer_getbackbufferdata_after_rt_unbind_test.cpp`, not duplicated here, which is the correct scoping).

### Cross-file consistency
The header comment's narrative (pre-fix: `ReadBackbuffer` unconditionally threw "not implemented") was
cross-checked against the current `SdlGraphicsBackend.cpp`, which does now implement `ReadBackbuffer` fully
(lines 591-641, matching the already-audited sibling report's account) — the "before" state described is
consistent with a real, resolved gap, not a fabricated narrative.

## Detailed Findings

None. No CRITICAL/HIGH/MEDIUM/LOW findings in this file.

## Cross-File Observations

- Shares the `NativeBackBuffer` + exact-pixel-readback idiom with every other file in this batch
  (`sdlrenderer_rendertarget2d_construction_test.cpp`, `..._sample_test.cpp`, `..._usage_test.cpp`,
  `..._depth_decision_test.cpp`, `..._mrt_throws_test.cpp`) — a consistent, well-understood pattern across the
  shard, not reinvented per file.

## Missing or Weak Tests

None for this file's stated scope (foundational readback correctness). Deeper `GetBackBufferData` scenarios
(multi-row regions, `startIndex`/`elementCount` slicing beyond the trivial 1-pixel case used here) are not
covered by this file, but are reasonably out of scope given its stated "foundational prerequisite" purpose —
worth flagging as a gap only if no other file in the full `examples-tests-sdlrenderer` shard (67 files) covers
multi-pixel/multi-row readback; not independently confirmed one way or the other in this batch of 8.

## Positive Findings

- Both the "inside" and "outside" assertions are genuinely discriminating pixel checks, not placeholders.
- The choice of sample points (well inside the sprite, at the canvas origin) avoids edge-case ambiguity that
  would otherwise make the test fragile to off-by-one rectangle semantics.
- Header comment accurately documents both the historical bug and the specific technical subtlety
  (physical-vs-logical coordinate mapping) that had to be solved — independently verified against the real
  backend implementation, not just trusted.

## Final Assessment

A correct, appropriately-scoped foundational test. Its header narrative and technical claims both check out
against the current production code.
