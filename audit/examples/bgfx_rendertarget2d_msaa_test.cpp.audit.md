# Audit: examples/bgfx_rendertarget2d_msaa_test.cpp

## Metadata

- Source file: `examples/bgfx_rendertarget2d_msaa_test.cpp` (236 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `RenderTarget2D` MSAA resolve pixel test
- File type: standalone `Game`-subclass executable, CTest-registered as `Bgfx_RenderTarget2D_MsaaResolve`
  (`cmake/Tests/BgfxTests.cmake:484-486`, run with `CNA_BGFX_RENDERER=VULKAN` in its environment — see below)
- XNA/FNA relevance: direct — `RenderTarget2D`'s `multiSampleCount` constructor parameter (`GraphicsDevice`
  MSAA render targets, `IEffectLights`-independent).
- FNA reference: N/A for the resolve mechanism itself (FNA delegates entirely to `FNA3D`'s
  backend-specific MSAA resolve; this is a CNA/Bgfx implementation-detail test, not an XNA API-shape test).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` —
  `BgfxMsaaRtFlag()` (lines 597-605), `BgfxRenderTargetBackend` ctor (668-706), `BindAsRenderTarget()`
  (715-723), `EnsureViewState()` (1325-1382), `BgfxSpriteBatchBackend::Draw()` (929-966, the
  `IBgfxSamplable`/`dynamic_cast` path at 957-959).

## Purpose

Proves that `BGFX_TEXTURE_RT_MSAA_X8`-flagged render targets genuinely anti-alias, not merely "don't
corrupt a solid fill" (a target that never resolves anything would trivially pass a solid-colour test
too). Renders a diagonal-edged, axis-aligned-hypotenuse triangle into a 32×32 `RenderTarget2D` at
`MultiSampleCount=0` and `=8`, samples the centre row back onto the backbuffer via `SpriteBatch`, and
checks: (a) the non-MSAA row is a hard binary transition (`IsBinary`), (b) the MSAA row contains
genuinely intermediate (partially-covered) pixel values (`HasIntermediate`). Both helper predicates
scan the whole 32-pixel row for any value in `(40, 215)`, a wide-enough band that a single blended
diagonal pixel is enough to flip the result — appropriately permissive for a differential (not exact)
test.

## Executive Verdict

**Healthy** — this is one of the more rigorously self-documented and internally-verified files in this
shard. Every non-trivial claim in its own header comment (the two prerequisite bug fixes, the
sandbox-specific OpenGL-vs-Vulkan-renderer environment note, the retry-only-the-backbuffer-read
convention) is independently corroborated against the actual current production code and `cmake/Tests/BgfxTests.cmake`.

## Checklist Results

### API / XNA / FNA parity
N/A in the strict sense — `MultiSampleCount` on `RenderTarget2D` is an XNA-facing constructor parameter,
but its *resolve mechanism* is entirely an FNA3D/CNA backend implementation detail with no C#-visible
semantics to diff against. `RenderTarget2D`'s constructor signature used here (device, size, size,
false, `SurfaceFormat::Color`, `DepthFormat::None`, multiSampleCount, `RenderTargetUsage::DiscardContents`,
line 103-104) matches FNA's full 7-arg overload shape.

### Behavioral correctness
Verified `BgfxRenderTargetBackend`'s constructor (`BgfxGraphicsBackend.cpp:668-706`): `BgfxMsaaRtFlag()`
maps `multiSampleCount>=8` to `BGFX_TEXTURE_RT_MSAA_X8` and `0` to plain `BGFX_TEXTURE_RT` (lines
597-605) — matches the test's own two `RenderAndReadRow(device, 0)` / `RenderAndReadRow(device, 8)`
calls (lines 194-195) exactly. `RenderAndReadRow`'s own retry loop (lines 133-155) only retries the
*backbuffer* `GetBackBufferData` read, never re-renders into the RT — matches the file's header claim
(lines 55-60) and the project-wide Task 406 "first-read-only" Bgfx quirk.

### Logic
The comment's claim that Bgfx's per-RT MSAA is independent of any backbuffer MSAA precondition (lines
13-18) is directly supported by `BgfxRenderTargetBackend`'s constructor never consulting any
device-level/backbuffer MSAA state — it's a pure per-texture flag (`bgfx::createTexture2D(..., msaaFlag
| ...)`, line 689-691), unlike the comment's description of Vulkan's own "piggyback on `sampleCount_`"
design (not re-verified here, out of this file's scope, but consistent with the stated architectural
contrast).

### C++ correctness
`RenderAndReadRow`'s `RenderTarget2D rt(...)` is stack-local and outlives its own use (destructed at end
of scope, after both the render and all 20 backbuffer-read retries) — correct RAII, no dangling backend
handle risk. `IsBinary`/`HasIntermediate` (159-177) are simple, side-effect-free scans; no UB.

### Robustness
Two of the header's engineering claims were cross-checked against the actual backend and found accurate
rather than assumed:
- **Task 873 fix** (`IBgfxSamplable`, `BgfxSpriteBatchBackend::Draw` lines 952-959): confirmed the draw
  path now does `dynamic_cast<const IBgfxSamplable*>(&texture)` instead of an unsafe
  `static_cast<const BgfxTextureBackend&>` — the exact fix the comment describes, still present.
- **`EnsureViewState()` RT-size fix** (lines 1344-1357): confirmed `viewWidth`/`viewHeight` are taken
  from `currentRtWidth_`/`currentRtHeight_` when `spriteViewId != 0`, falling back to the cached window
  size only for the backbuffer view — matches the comment's description of the pre-fix behaviour
  ("previously unconditionally overwrote... on every single Clear()/SubmitSprite() call") being
  genuinely fixed, not just claimed fixed.

### Testing
The **CNA_BGFX_RENDERER=VULKAN** environment override claimed in the header (lines 43-49) is confirmed
verbatim in `cmake/Tests/BgfxTests.cmake:484-486`
(`ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_BGFX_RENDERER=VULKAN"`), with the
surrounding comment block (lines 470-479) repeating the same legacy-GL-2.1-context rationale — this is
a consistently maintained, not stale, cross-file claim. This is a meaningful scope note for anyone
reading only the test file: **this specific CTest target does not exercise Bgfx's default OpenGL
renderer at all** in this project's own CI configuration; it is deliberately routed through bgfx's
Vulkan backend. A reader relying on "Bgfx" in the test's name to mean "the OpenGL path" would be
mistaken for this one specific test.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. One LOW/INFO observation:

### F1 — Test name suggests generic Bgfx coverage, but this specific CTest target never runs bgfx's default OpenGL renderer in CI
- Severity: LOW
- Confidence: HIGH (directly confirmed via `cmake/Tests/BgfxTests.cmake:484-486`)
- Category: maintainability / test-scope-clarity
- Location/symbol: `BgfxTests.cmake:484-486`, this file's own header comment lines 43-49
- Evidence: `cna_register_backend_test(NAME Bgfx_RenderTarget2D_MsaaResolve ... ENVIRONMENT
  "...;CNA_BGFX_RENDERER=VULKAN")`.
- Why it matters: not a defect — the file's own comment is explicit and honest about this — but anyone
  scanning `ctest -R Bgfx_` output and seeing this test pass could incorrectly conclude Bgfx's default
  OpenGL MSAA path works in this sandbox. It doesn't (per the same comment); only the Vulkan-routed
  variant is verified by this CI run.
- Suggested follow-up (not implemented by this audit): none required beyond what the comment already
  documents; could optionally rename the CTest target to make the Vulkan-routing explicit
  (e.g. `Bgfx_RenderTarget2D_MsaaResolve_ViaVulkanRenderer`), purely cosmetic.

## Cross-File Observations

- Shares its `IBgfxSamplable` prerequisite fix and `EnsureViewState()` RT-sizing fix with every other
  RenderTarget2D-sampling-after-unbind test in this shard (`bgfx_setrendertarget_null_restore_test.cpp`,
  the RenderTargetCube family below) — this audit independently confirmed the shared production code
  once here and treated the other files' identical claims about the same fix as corroborated rather
  than re-deriving it per file.
- Establishes the "retry only the backbuffer read, not the RT fill" pattern that
  `bgfx_rendertargetcube_mip_test.cpp`/`bgfx_rendertargetcube_msaa_test.cpp` explicitly credit by name
  (`bgfx_viewport_subregion_test.cpp's renderAndReadFresh`, per this file's own line 59) — verified
  this file's own `RenderAndReadRow` is a correct instance of that same pattern, not merely claiming to
  be.

## Missing or Weak Tests

None found for this specific file — the differential binary-vs-intermediate technique is an
appropriately strong, discriminating test for "did a real multisample resolve happen," and both the
`MultiSampleCount=0` control and `=8` treatment are exercised.

## Positive Findings

- The file's header comment is unusually rigorous: every claim (2 prerequisite bugs, 1 environment
  limitation, 1 retry convention) was checked against current source and found accurate, not stale —
  a genuinely good match for this project's "verify claims independently" audit mandate.
- The differential (binary vs. blended) technique is methodologically sound and specifically designed
  to reject the false-positive "solid fill looks fine either way" failure mode the header explicitly
  calls out.

## Final Assessment

A well-engineered, accurately self-documented test with no correctness defects found. Its only
noteworthy trait — that this specific registered CTest target runs against bgfx's Vulkan renderer, not
its default OpenGL one, due to a documented sandbox/driver limitation — is already transparently
disclosed in the file itself and in the CMake registration comment, so this is recorded here as context
rather than a defect.
