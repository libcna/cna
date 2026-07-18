# Audit: examples/sdlrenderer_rendertarget2d_sample_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_rendertarget2d_sample_test.cpp` (133 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `RenderTarget2D`-sampled-as-`Texture2D` test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_rendertarget2d_sample …)` /
  `cna_register_backend_test(NAME SDL_Renderer_RenderTarget2D_SampleAfterUnbind …)`,
  `cmake/Tests/SdlRendererTests.cmake:259-263`. Header traces to Task 705 (confirmed live:
  `git log` shows `e6876f5d fix(Task 705): unchecked downcast in SdlSpriteBatchBackend::Draw was UB`).
- XNA/FNA relevance: `RenderTarget2D` IS-A `Texture2D` (XNA inheritance relationship), so any `Texture2D`-
  accepting API (here, `SpriteBatch::Draw`) must accept a `RenderTarget2D` transparently once unbound.
- Related production code: `include/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.hpp`
  (`SdlTextureBackend`/`SdlRenderTargetBackend` as sibling, not parent/child, classes — both independently derive
  `ITextureBackend`), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SdlSpriteBatchBackend::Draw`'s three overloads — already flagged as fixed in the sibling backend audit
  report).

## Purpose

Proves a real, previously-broken code path: `RenderTarget2D`, once unbound, must be drawable via the exact same
`SpriteBatch::Draw(Texture2D&, ...)` path as any plain `Texture2D`. The header comment documents that this test
**found a real bug while being written**: `SdlSpriteBatchBackend::Draw`'s three overloads all performed an
unconditional `static_cast<const SdlTextureBackend&>(texture)`, which is undefined behavior when the argument is
actually an `SdlRenderTargetBackend` (a sibling class, not a subclass, of `SdlTextureBackend`) — fixed by
switching to the already-existing virtual `GetNativeTexture()`/`GetWidth()`/`GetHeight()` accessors. The test
draws a Blue background + Red corner marker into a 16×16 `RenderTarget2D`, unbinds it, draws the RT itself onto
the backbuffer via `SpriteBatch`, and reads back both regions to confirm the pattern survived byte-for-byte.

## Executive Verdict

**Healthy.** This is a genuinely valuable regression test for a real UB bug, and its own narrative was
independently corroborated: the sibling `SdlGraphicsBackend.cpp` audit report (already on file) documents the
exact same fix at the exact same call sites ("Task 705, repeated at all three call sites"), and the class
hierarchy claim (`SdlRenderTargetBackend`/`SdlTextureBackend` are siblings, both independently deriving
`ITextureBackend`) is confirmed directly from the current header (lines 1-2 vs. 25-26 area class declarations,
no inheritance between them).

## Checklist Results

### API / XNA / FNA parity
`RenderTarget2D`'s `IS-A Texture2D` relationship (this test's core premise) matches FNA:
`RenderTarget2D : Texture2D` in `RenderTarget2D.cs`. Confirmed in CNA:
`include/Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp` derives from `Texture2D`, and
`RenderTarget2D.cpp`'s constructor delegates into `Texture2D`'s own constructor (line 52) with the
render-target-specific `IRenderTargetBackend` passed in as the texture's backend — so `sb_->Draw(rt, ...)`
(line 96) genuinely reaches `SpriteBatch::Draw(const Texture2D&, ...)` with no special-casing, exactly as real
XNA code relying on the inheritance would expect.

### Behavioral correctness
Traced the exact bug this test guards against: before the fix, `SdlSpriteBatchBackend::Draw`'s
`static_cast<const SdlTextureBackend&>(texture)` on an actual `SdlRenderTargetBackend` argument reads the wrong
object layout (unrelated sibling classes, not a valid downcast) — reading `width`/`height`/`texture` member
offsets that don't correspond to `SdlRenderTargetBackend`'s actual layout is classic UB (could silently produce
garbage dimensions/texture handle, or crash, depending on how the two classes' layouts happen to alias). The
fix (virtual `GetNativeTexture()`/`GetWidth()`/`GetHeight()`) is the textbook-correct solution — dispatch through
the interface rather than downcast to a concrete sibling type.

### Logic
Two independent pixel checks (marker corner, background) rather than a single blended sample — correctly
isolates "did the whole pattern get corrupted" (would fail both) from "did just the marker get lost/shifted"
(would fail only the first) as separately diagnosable failure modes.

### Memory/resource lifetime
`RenderTarget2D rt(dev, rtSize, rtSize)` (line 85) is a local stack object; `dev.SetRenderTarget(nullptr)` (line
91) unbinds before `rt` goes out of scope — correct ordering (unbind happens while `rt` is still alive and being
sampled, matching real XNA usage where a render target is typically unbound, sampled, then eventually
disposed/rebound, not disposed-while-still-referenced).

### C++ correctness
No unsafe casts remain in the *test* file itself (it only calls the public `SpriteBatch::Draw`/
`GraphicsDevice::SetRenderTarget` API); the unsafe cast this test catches lives in the *production* backend code,
already fixed per the header comment and the sibling backend audit.

### Performance
N/A — single-frame test.

### Thread safety
N/A.

### Architecture
This test is a strong example of the "sibling-class downcast" class of bug this project's `ITextureBackend`
interface exists specifically to prevent — worth calling out as an architecturally sound regression guard, not
just a pixel test.

### Maintainability
133 lines; clear single-purpose structure (render into RT → unbind → sample as texture → verify).

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
Correctly uses `PresentationMode::NativeBackBuffer` (line 122) for exact-pixel readback, consistent with the
rest of this shard.

### Testing
This file is the dedicated regression test for the Task 705 UB fix. No other file in this batch duplicates its
specific "render target sampled as texture" scenario (the construction test's Part 3 tests bind/unbind
*isolation*, not *re-sampling the RT's own content as a texture afterward* — a materially different property).

### Cross-file consistency
Header narrative independently cross-checked against and corroborated by the sibling
`SdlGraphicsBackend.cpp.audit.md` report's own account of the identical fix — consistent across both documents,
not a one-sided claim.

## Detailed Findings

None. No CRITICAL/HIGH/MEDIUM/LOW findings in this file.

## Cross-File Observations

- This test and `sdlrenderer_rendertarget2d_construction_test.cpp` both exercise
  `SdlSpriteBatchBackend::Draw` against an `SdlRenderTargetBackend`-backed texture (the construction test's
  "backbuffer marker" checks draw plain `Texture2D`s, not the RT itself, but its final unbind-and-draw-again
  step is architecturally similar) — between the two, the Task 705 fix's three call sites (the file's header
  comment specifies "all 3 overloads") are collectively well covered by this batch, though this audit did not
  independently verify that literally all three `Draw` overloads (not just the one this test happens to call)
  are exercised somewhere in the full 67-file shard.

## Missing or Weak Tests

Only one of `SdlSpriteBatchBackend::Draw`'s three overloads (per the header comment) is exercised by this
specific file (the destination-rect + source-rect overload, via `sb_->Draw(rt, Rectangle(0,0,W,H),
Rectangle(0,0,rtSize,rtSize), Color::White)`, line 96). Whether the other two overloads are independently
exercised against an `SdlRenderTargetBackend` argument elsewhere in the full shard was not confirmed in this
8-file batch — worth flagging as a coverage question for whoever audits the remaining ~59 files in
`examples-tests-sdlrenderer`.

## Positive Findings

- A genuinely valuable regression test for a real, previously-shipped UB bug (unchecked downcast between
  sibling classes) — not a synthetic scenario.
- Two independently-diagnosable pixel checks (marker vs. background) rather than one blended assertion.
- Header narrative independently corroborated against both the production backend code and a separately-written
  sibling audit report — internally consistent.

## Final Assessment

A strong, accurate regression test for a real fixed bug. No defects found in this file; one minor coverage
question flagged (whether all three `Draw` overloads get RT-as-texture coverage somewhere in the full shard) for
follow-up outside this batch's scope.
