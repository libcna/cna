# Audit: examples/sdlrenderer_graphics_capability_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_graphics_capability_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `CNA::GraphicsCapability`/`GraphicsDevice::SupportsCapability()`
  on SDL_Renderer. Twin of `dx3_graphics_capability_test.cpp`/`canvas_graphics_capability_test.cpp` per the file's
  own header comment.
- File type: standalone `Game`-subclass executable, CTest-registered (`SDL_Renderer_GraphicsCapability` /
  `cna_test_sdl_graphics_capability`, `cmake/Tests/SdlRendererTests.cmake:358-360`).
- XNA/FNA relevance: none directly (`CNA::GraphicsCapability` is a `NOXNA` CNA extension, not an XNA type) — but
  the 3D methods it probes (`SetDepthTestEnabled`, `VertexBuffer` construction) are XNA-facing.
- Related production code: `include/CNA/GraphicsCapability.hpp` (the 8-value enum),
  `include/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.hpp` (`SupportsCapability` override, lines
  145-151), `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`SupportsCapability` line 1323-1326,
  `SetDepthTestEnabled` line 494-496), `src/Microsoft/Xna/Framework/Graphics/VertexBuffer.cpp` (constructor,
  lines 29-40).

## Purpose

Confirms two independent things about the `GraphicsCapability` query mechanism on this intentionally-2D-only
backend: (1) `GraphicsDevice::SupportsCapability()` reports `false` for every one of the 8 currently-enumerated
`CNA::GraphicsCapability` values, and (2) that this is purely advisory — calling the corresponding real 3D method
anyway (`SetDepthTestEnabled(true)`, constructing a `VertexBuffer`) still throws exactly as it did before the
capability-query feature existed, i.e. `SupportsCapability()` is a look-before-you-leap check, not an enforcement
gate that changes the underlying method's own behavior.

## Executive Verdict

**Healthy** — every claim the test makes is verified against the actual current implementation; the capability
enum, the backend's unconditional-`false` override, and the throw-behavior of the two probed 3D methods all match
exactly what this file asserts.

## Checklist Results

### API / XNA / FNA parity

N/A directly — `CNA::GraphicsCapability`/`SupportsCapability()` is a `NOXNA` CNA-specific extension
(`include/CNA/GraphicsCapability.hpp`'s own doc comment confirms this is a capability-query mechanism layered on
top of XNA, not part of the XNA 4.0 surface). `SetDepthTestEnabled`/`VertexBuffer` construction themselves are
correctly XNA-facing and this file does not misuse their signatures.

### Behavioral correctness

Read `include/CNA/GraphicsCapability.hpp`: the enum has *exactly* 8 values (`ThreeD`, `DepthStencilBuffer`,
`MultiSampleAntiAliasing`, `MultipleRenderTargets`, `AnisotropicFiltering`, `WireFrame`, `OcclusionQuery`,
`CustomEffects`) — this test's 8 `check(!dev.SupportsCapability(...))` calls (lines 52-59) cover all 8, with none
missing and none duplicated. `SdlGraphicsBackend::SupportsCapability()` (header lines 146-151) unconditionally
returns `false` regardless of the `capability` argument (explicitly unnamed:
`bool SupportsCapability(CNA::GraphicsCapability /*capability*/) const override { return false; }`) — matching
the test's assertion of "none supported" for every value.

`SetDepthTestEnabled(true)` reaches `GraphicsDevice::SetDepthTestEnabled` (line 494-496), which forwards
unconditionally to `backend_->SetDepthTestEnabled(enabled)` — `SdlGraphicsBackend`'s override is
`ThrowNo3D("SetDepthTestEnabled")` (`SdlGraphicsBackend.cpp:791`), throwing `std::runtime_error`. Constructing
`VertexBuffer vb(dev, 4)` reaches the 4-arg delegate constructor
(`VertexBuffer.cpp:29-40`, `backend_(device.GetBackend().CreateVertexBuffer(vertexCount))`), which on this
backend is `ThrowNo3D("CreateVertexBuffer")` (also `std::runtime_error`) — no argument validation runs before
this call for `vertexCount=4` (a normal positive value), so the throw genuinely originates from the 2D/3D
capability boundary this test targets, not an unrelated argument-range check. Both match the `Throws<F>()` helper
(lines 38-42), which specifically catches `std::runtime_error` — the exact type both throw sites use.

### Logic

Since `SupportsCapability()`'s implementation ignores its `capability` parameter entirely, the 8 individual
`check()` calls are not really testing 8 independently-dispatched code paths (there is only one, parameter-free
`return false`) — they are testing the same trivial branch 8 times. This is not a defect (an unconditional
"nothing is supported" answer genuinely is correct for a 2D-only backend, and the enum could grow future values
this test would need updating for), but is worth noting: passing this test does not, by itself, prove the
*enum's* capability semantics are correctly per-value-dispatched anywhere — only that this particular backend
opts out of the entire mechanism uniformly. The real per-value dispatch logic (if any exists) would need to be
verified on a backend that actually supports a subset of capabilities (out of scope for this file).

### Robustness

`Throws<F>()`'s narrow `catch (const std::runtime_error&)` (not `std::exception`) correctly requires the exact
exception category `ThrowNo3D` actually uses, rather than accepting any thrown type — a meaningfully stricter
check than a bare `catch (...)` would be.

### Testing

Complete coverage of the enum's current 8 values plus a meaningful "the check doesn't prevent the throw" sanity
check via 2 representative 3D methods (one non-constructing, one constructing). A natural extension (not present)
would be probing one method per capability category (e.g. an actual MRT call for `MultipleRenderTargets`,
`OcclusionQuery` construction for that capability) rather than reusing just two methods to represent "3D is
unsupported" broadly — but given `SupportsCapability()`'s parameter-independent implementation (see Logic above),
this would mostly be redundant given the current implementation, and the two chosen probes are reasonable
representative samples.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. One LOW observation:

### F1 — `SupportsCapability()`'s 8 checks all exercise the same parameter-independent branch, not 8 distinct dispatch paths

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / architecture-observation
- Location/symbol: `SdlGraphicsBackend::SupportsCapability` (header lines 146-151, unnamed parameter);
  test lines 52-59
- Evidence: the override's parameter is explicitly discarded (`CNA::GraphicsCapability /*capability*/`) and the
  body is a single `return false;` with no `switch`/`if` on the value at all.
- Why it matters: this is fully correct and intentional for this backend (genuinely 2D-only, so "none supported"
  is right for every current and any future capability value without needing per-value logic) — flagged only as
  an architecture observation for whoever later audits a *partially*-capable backend's `SupportsCapability()`
  implementation, where per-value dispatch correctness would actually need to be tested value-by-value rather
  than assumed from a passing all-false test like this one.
- FNA/XNA comparison: N/A (`NOXNA` extension).
- Related files: `include/CNA/GraphicsCapability.hpp`; any backend with a real per-value `switch` in
  `SupportsCapability()` (out of scope for this shard).
- Suggested future action: none for this file; worth keeping in mind when auditing capability-query
  implementations on backends that actually support a subset of the enum.

## Cross-File Observations

- Twin files `dx3_graphics_capability_test.cpp`/`canvas_graphics_capability_test.cpp` (named in this file's own
  header comment) presumably share this exact structure for their own 2D-only backends — worth confirming they
  assert against the same 8-value enum when those shards are audited, so a future 9th `GraphicsCapability` value
  doesn't silently go unchecked on one backend but not another.

## Missing or Weak Tests

None significant. See F1 for a forward-looking observation about backends with genuine partial capability
support (not applicable to SDL_Renderer itself).

## Positive Findings

- Exhaustively covers all 8 current `GraphicsCapability` enum values with no gaps or duplicates.
- Correctly demonstrates the "advisory, not enforcing" contract of `SupportsCapability()` by proving the
  underlying throw still fires even when the capability was already known to be unsupported — a genuinely useful
  behavioral guarantee for callers who might otherwise assume checking first makes the call itself safe.
- Uses a precise `std::runtime_error`-typed throw helper rather than a loose `catch (...)`.

## Final Assessment

A small, complete, and accurate test. Every assertion was independently confirmed against the current
`GraphicsCapability` enum and `SdlGraphicsBackend`/`GraphicsDevice` implementation; no discrepancies found.
