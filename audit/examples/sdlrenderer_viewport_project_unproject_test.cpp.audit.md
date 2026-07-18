# Audit: examples/sdlrenderer_viewport_project_unproject_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_viewport_project_unproject_test.cpp` (149 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `Viewport` get/set round-trip + `Project`/
  `Unproject` 2D-orthographic integration test.
- CTest registration: `cna_sdl_test(cna_test_sdl_viewport_project_unproject
  examples/sdlrenderer_viewport_project_unproject_test.cpp)` /
  `cna_register_backend_test(NAME SDL_Renderer_Viewport_ProjectUnproject ...)`
  (`cmake/Tests/SdlRendererTests.cmake:289-291`).
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.Viewport.Project`/`Unproject`,
  `GraphicsDevice.Viewport` get/set.
- Related production code: `include/Microsoft/Xna/Framework/Graphics/Viewport.hpp` +
  `src/Microsoft/Xna/Framework/Graphics/Viewport.cpp`,
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`getViewportProperty`/`setViewportProperty`, lines 235-247),
  `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` (`SetViewport`'s default no-op body,
  line 675).
- FNA reference: `Viewport.cs`'s `Project`/`Unproject` (standard XNA formula: perspective divide,
  `[-1,1]→[0,Width/Height]` remap, Y-flip, depth remap by `MinDepth`/`MaxDepth`).
- Task provenance (`git log --all --oneline`): Task 710
  (`8dc328a5 verify(Task 710): Viewport Project/Unproject with live device + 2D ortho`).

## Purpose

Integration test tying `Viewport`'s already-unit-tested `Project`/`Unproject` math
(`tests/Microsoft/Xna/Framework/Graphics/ViewportTests.cpp`) to a real, live
`GraphicsDevice::getViewportProperty()`/`setViewportProperty()` round trip on the SDL_Renderer
backend, specifically using a **non-zero X/Y viewport offset** — a combination the file's own
header comment claims every existing `ViewportTests.cpp` `Project`/`Unproject` case omits — and a
genuine 2D orthographic projection (`CreateOrthographicOffCenter`) representative of how a 2D game
would convert between world and screen coordinates.

## Executive Verdict

**Healthy**, with one worthwhile architectural note (F1, not a defect in this file) about what the
"live device" round trip does and does not actually prove on this backend.

## Checklist Results

### Purpose
Correctly scoped as an SDL_Renderer-specific *integration* test rather than duplicating the
already-existing pure-math unit tests; the header comment (lines 1-16) explicitly and correctly
identifies what is genuinely new here (a live device + non-zero offset) versus what's already
covered elsewhere.

### API / XNA / FNA parity
`Viewport::Project`/`Unproject` (`Viewport.cpp` lines 60-106) were independently checked
line-by-line against the standard XNA formula: `matrix = world*view*projection`; perspective divide
guarded by `MathHelper::WithinEpsilon(a, 1.0f)`; `vector.X = ((X+1)*0.5)*Width + X_`; `vector.Y =
((-Y+1)*0.5)*Height + Y_` (Y-flip for XNA's top-left-origin, Y-down screen convention);
`vector.Z = Z*(MaxDepth-MinDepth)+MinDepth`. `Unproject` correctly inverts each of these steps in
reverse order using `Matrix::Invert(world*view*projection)`. This matches FNA's own
`Viewport.Project`/`Unproject` implementation pattern exactly (same remap constants, same
perspective-divide guard, same depth-remap direction).

### Behavioral correctness
Independently re-derived the expected results for all 5 checks:
- `CreateOrthographicOffCenter(0, W, H, 0, 0, 1)` passes `bottom=H, top=0` — i.e. world `Y=0` maps
  to NDC `Y=+1` and world `Y=H` maps to NDC `Y=-1` (standard XNA ortho convention: `top`→`+1`,
  `bottom`→`-1`). Feeding these through `Project`'s own Y-flip formula: NDC `Y=+1` →
  `screenY = ((-1+1)*0.5)*H + Y_ = Y_` (viewport's own top); NDC `Y=-1` →
  `screenY = ((1+1)*0.5)*H + Y_ = H + Y_` (viewport's own bottom) — exactly matching the test's own
  expectations at lines 100-103 (`topLeftWorld(0,0,0)` → `(vp.X, vp.Y)`) and 106-110
  (`bottomRightWorld(W,H,0)` → `(vp.X+W, vp.Y+H)`). This is a real, correct, non-trivial use of the
  "Y-flipped ortho projection" idiom real 2D games use to get a top-left-origin, Y-down screen
  coordinate system — not an accidental pass.
- The centre-point check (lines 112-115) and both `Unproject` round-trip checks (lines 117-126)
  follow the same math and are consistent with the above.
- The get/set round-trip checks (lines 74-85) were cross-checked against
  `GraphicsDevice::getViewportProperty()`/`setViewportProperty()` (`GraphicsDevice.cpp` lines
  235-247): `setViewportProperty` genuinely assigns `viewport_ = value` and the getter returns a
  reference to that same stored field, so the round trip is a real, meaningful check of
  `GraphicsDevice`'s own storage, not a tautology.

### Logic
`Near(a, b, eps=0.01f)` (line 37) is an appropriately tight tolerance for CPU-only floating-point
math with no GPU rounding involved (unlike the pixel-tolerance tests elsewhere in this shard) —
correctly proportioned to what's actually being measured here.

### Memory/resource lifetime
No resource lifetime concerns — `Viewport`/`Matrix`/`Vector3` are all value types on the stack;
`gdm_` is the only heap-owned member, standard for this shard.

### C++ correctness
No unsafe casts; `static_cast<float>(vp.getWidthProperty())` etc. are the correct explicit
int→float conversions for the sizes used in this test (well within `float`'s exact-integer range).

### Performance
N/A — a handful of 4x4 matrix multiplies and inversions in a one-shot test.

### Thread safety
N/A — consistent with the rest of the shard.

### Architecture
Correctly stays at the `Viewport`/`GraphicsDevice` API surface; see F1 for a note on what
`setViewportProperty` does and does not actually cause on this backend.

### Maintainability
149 lines, proportionate; the header comment's explicit statement of what's *new* here versus
what's already covered by `ViewportTests.cpp` (and this audit's own independent check that the
claim is true, see Cross-File Observations) is good practice for avoiding redundant-test bloat.

### Portability
N/A.

### Robustness
Not applicable in the input-validation sense — a fixed-scenario integration test.

### Testing
This file is itself a test; see Cross-File Observations/F1 for what it does not, and cannot,
prove about actual rendering-side viewport effects on this backend.

### Cross-file consistency
`Viewport(8, 4, 32, 16)` (line 80) and its round trip through
`GraphicsDevice::setViewportProperty`/`getViewportProperty` are consistent with those methods'
actual implementation (`GraphicsDevice.cpp` lines 235-247) — confirmed the getter returns the
identical stored value, not a recomputed or backend-derived one.

## Detailed Findings

No CRITICAL/HIGH findings. One MEDIUM-confidence architectural observation, not a defect in this
test file itself:

### F1 — `GraphicsDevice::setViewportProperty` forwards to `IGraphicsBackend::SetViewport`, which SDL_Renderer (along with Ascii/Canvas/Dx3) never overrides — the round-trip check genuinely validates `GraphicsDevice`'s own field storage, but proves nothing about any actual rendering-side effect of setting a custom Viewport on this backend

- Severity: MEDIUM (as an observation about the backend/architecture; not a defect of this test
  file, whose own claims it does not falsify)
- Confidence: HIGH (confirmed by direct code search across every backend)
- Category: architecture / cross-backend consistency
- Location/symbol: `GraphicsDevice::setViewportProperty` (`GraphicsDevice.cpp` lines 240-247,
  calls `backend_->SetViewport(...)`); `IGraphicsBackend::SetViewport`'s default body
  (`IGraphicsBackend.hpp` line 675: `virtual void SetViewport(...) {}`, empty); confirmed via
  repository-wide search that `D3D9GraphicsBackend`, `D3D11GraphicsBackend`, `BgfxGraphicsBackend`,
  `VulkanGraphicsBackend`, `WebGPUGraphicsBackend`, `EasyGLGraphicsBackend`,
  `HeadlessGraphicsBackend`, and `SoftwareGraphicsBackend` all override `SetViewport` with a real
  (or at least explicitly-acknowledged) implementation, while `SdlGraphicsBackend` has **no**
  `SetViewport` override anywhere in `SdlGraphicsBackend.cpp`/`.hpp` — nor do the `Ascii`,
  `Canvas`, or `Dx3` backends (a repository-wide grep for `SetViewport` found zero matches in any
  of their `.cpp`/`.hpp` files).
- Evidence: this test's round-trip check (lines 80-85) is genuinely correct and meaningful for what
  it tests (`GraphicsDevice`'s own stored `Viewport` value), and the `Project`/`Unproject` math
  checks that follow are pure CPU math over that stored value's `X_`/`Y_`/`Width_`/`Height_` fields
  — neither of these depends on `backend_->SetViewport` having any real effect, so this finding does
  not undermine anything this file actually asserts. It surfaces a separate, real question: on
  SDL_Renderer, does calling `GraphicsDevice::setViewportProperty(customViewport)` actually
  constrain or offset where subsequent draws land on screen (e.g. a split-screen or sub-region
  rendering use case), or does it silently have zero rendering effect while still reporting back
  the "expected" values through the getter?
- Why it matters: a hypothetical 2D game on this backend that sets a custom sub-viewport expecting
  SDL-side scissoring/offset (the way it would on every other backend that does implement
  `SetViewport` for real) would see no such effect — the value is stored and returned correctly
  by `GraphicsDevice`, but never reaches SDL_Renderer's own rendering state. This is a shared trait
  with `Ascii`/`Canvas`/`Dx3`, not something uniquely wrong with `SdlGraphicsBackend`, and it's
  plausible this is an accepted characteristic of this whole "direct-blit, no real
  rasterizer-viewport-transform" family of backends (SpriteBatch on SDL_Renderer draws via
  `SDL_RenderTexture` with explicit destination rectangles, not via a viewport-derived NDC
  transform the way a GPU shader-based backend would need) — but this audit found no comment,
  doc, or test anywhere in the shard that states this explicitly, and it was not raised by the
  separately-audited `SdlGraphicsBackend.cpp` report (whose own Detailed Findings, F1/F2, cover
  `stride` handling and `Texture3D`/`TextureCube`, not `SetViewport`).
- FNA/XNA comparison: in real XNA/FNA, `GraphicsDevice.Viewport` genuinely drives the rasterizer's
  viewport transform (`glViewport`/`D3D9Device::SetViewport`) for actual 3D draws — not directly
  applicable to SpriteBatch's own 2D destination-rectangle draws, so the practical impact for a
  SpriteBatch-only 2D game may be limited, but a game using `Viewport` for sub-region/split-screen
  restriction would see divergent behavior versus real XNA.
- Related files: `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`/`.hpp` (no
  `SetViewport` override to add one to, if this is judged worth fixing);
  `docs/sdl-renderer-2d-completeness.md` (a natural place to document this as an accepted
  limitation, alongside the already-documented `Texture3D`/`TextureCube` gap, if confirmed
  intentional).
- Suggested future action (not implemented by this audit): confirm with the SDL_Renderer backend
  owner whether this is an intentional, accepted limitation (in which case document it in
  `docs/sdl-renderer-2d-completeness.md` the same way the `Texture3D`/`TextureCube` gap already is)
  or a genuine gap worth closing via `SDL_SetRenderViewport`/`SDL_SetRenderClipRect`.

## Cross-File Observations

- Independently verified the file's own claim that "every existing `ViewportTests.cpp`
  `Project`/`Unproject` case omits [a non-zero X/Y offset]": every `Project`/`Unproject`-related
  `TEST(ViewportTest, ...)` case in `tests/Microsoft/Xna/Framework/Graphics/ViewportTests.cpp`
  (10 test cases, lines 133-346) constructs its `Viewport` as `Viewport(0, 0, W, H)` — confirmed by
  grep across the whole file; the only non-zero-offset `Viewport` constructions in that file
  (`Viewport vp(3, 7, 640, 480)` at line 99, `Viewport vp(1, 2, 640, 480)` at line 348) belong to
  unrelated `GetBounds`/other tests, not `Project`/`Unproject`. This test's claim to cover a
  genuinely new combination is therefore accurate, not just self-asserted.
- See F1 for the `SetViewport` no-op observation, relevant to whoever eventually cross-checks
  `SdlGraphicsBackend.cpp`'s own already-published audit report against this finding.

## Missing or Weak Tests

- No check exercises `MinDepth`/`MaxDepth` differing from the constructor's default `0`/`1` in
  this integration context (already covered for the pure-math case by
  `ViewportTests.cpp`'s `ProjectWithMinDepthGreaterThanMaxDepthProducesInvertedZWithoutThrowing`,
  so this is a minor, not a critical, gap for *this specific file's* scope).
- As noted in F1, no test anywhere in this shard (so far as this audit could find) directly proves
  or disproves whether `setViewportProperty` has any actual rendering-side effect on SDL_Renderer
  (e.g. drawing two sprites under two different custom sub-viewports and confirming they land in
  different screen regions) — this file's own round trip only proves the *value* round-trips
  through `GraphicsDevice`, not that rendering itself respects it.

## Positive Findings

- The Y-flipped orthographic-projection technique (`CreateOrthographicOffCenter(0,W,H,0,0,1)`) is
  exactly the idiom a real 2D game would use to get top-left-origin, Y-down screen coordinates from
  `Viewport.Project`, and this audit's independent re-derivation confirms the test's expected values
  are the mathematically correct output of that combination, not a coincidence.
- The claim of covering a "genuinely new" test combination (non-zero Viewport offset for
  `Project`/`Unproject`) was independently verified true against the existing unit-test file rather
  than taken at face value.
- Appropriately tight (`0.01`) floating-point tolerance, correctly distinguished from the much
  looser pixel-color tolerances used elsewhere in this shard's GPU-readback-based tests — shows
  awareness that this test's own error sources (pure CPU float math) are different in kind from a
  rasterized-pixel test's.

## Final Assessment

A well-designed, independently-verified-correct integration test for `Viewport::Project`/
`Unproject` against a live, non-zero-offset device viewport. Its own claims all check out under
independent re-derivation. The one substantive finding (F1) is about what `setViewportProperty`
does *not* do on this backend architecture-wide — a legitimate open question worth tracking, but
not a flaw in this test's own design or assertions.
