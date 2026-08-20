# Audit: src/CNA/Internal/Backends/Canvas/CanvasGraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/Canvas/CanvasGraphicsBackend.cpp`
- Audit status: AUDITED
- Subsystem: `backend-canvas` shard
- File type: C++ implementation (349 lines), Emscripten-only (HTML `<canvas>` 2D context)
- Related header/implementation: `include/CNA/Internal/Backends/Canvas/CanvasGraphicsBackend.hpp` (same shard);
  `CanvasTextureBackend.cpp`/`CanvasRenderTargetBackend.cpp`/`CanvasSpriteBatchBackend.cpp` (same shard, audited
  alongside)
- XNA/FNA relevance: intentionally 2D-only, mirroring SdlRenderer's/Dx3's/Ascii's own honest 2D-only design.
- Graphics backend relevance: one of the 14 confirmed backends; per `cmake/BackendSelection.cmake`, a GPU-free 2D
  alternative to EasyGL specifically for Emscripten targets that want `canvas.getContext('2d')` instead of WebGL2.
- FNA reference: N/A directly (2D-only backend); the `BlendStateToCompositeOp`/`DrawSprite` premultiply-alpha
  handling was cross-checked conceptually against the same `BlendState.AlphaBlend`-assumes-premultiplied-source
  convention already verified in the SdlRenderer/Software backend audits.
- Main related tests: `examples-tests-canvas` (2 files, not yet audited).

## Purpose

Implements `IGraphicsBackend` entirely via `EM_JS`-declared JavaScript functions operating on an HTML5
`CanvasRenderingContext2D`: window/viewport bookkeeping, `Clear()`/`Present()` (a no-op — the browser compositor
presents automatically), texture/render-target creation (delegated to sibling files), and `BlendState`-to-
`globalCompositeOperation` mapping (`BlendStateToCompositeOp`, exposed standalone and free of any `EM_JS`/JS calls
specifically so it can be unit-tested without a real browser context, per its own doc comment).

## Executive Verdict

**Mostly healthy.** Clean, honest 2D-only design with the same "throw for every 3D-only entry point" discipline
already praised in SdlRenderer/Dx3/Ascii. The constructor is also confirmed **not** to share EasyGL's
window-registration ordering bug (F1 in that report) — the only fallible statement (`if (!window_) throw`)
precedes `RegisterForWindow`, so no partially-constructed object can ever be registered. One narrow finding: the
non-Emscripten compile path of `ReadBackbuffer()` silently leaves its output buffer uninitialized rather than
zeroing it or asserting (F1).

## Checklist Results

### API / XNA / FNA parity
`BlendStateToCompositeOp` (lines 257-298) correctly maps exactly the 4 standard `BlendState` presets
(Opaque→`copy`, AlphaBlend→`source-over`+un-premultiply, NonPremultiplied→`source-over`, Additive→`lighter`) and
throws for anything else — `Canvas2D`'s `globalCompositeOperation` has no generic blend-equation model to fall
back on, so throwing (not silently approximating) is the correct, honest choice, consistent with Dx3's/
SdlRenderer's own equivalent-situation handling.

### Behavioral correctness
`CNA_Canvas2D_Clear`'s explicit `save()`/`setTransform(identity)`/`restore()` wrapping (lines 44-61) is correctly
reasoned: real XNA `Clear(color)` unconditionally overwrites every pixel, not blending with existing content — the
JS code correctly uses `globalCompositeOperation='copy'` for a non-zero-alpha clear specifically to get that
unconditional-overwrite semantic rather than `source-over`, which would incorrectly blend with whatever
composite mode a prior `SpriteBatch` draw left active.

### Logic
`getLogicalSize()` (lines 144-158) correctly special-cases `virtualHeight_ <= 0` (falls back to the real window
size) before applying the `FixedHeightDynamicWidth` derivation — matches the same math already verified correct
in `EasyGLGraphicsBackend::getLogicalSize()`'s audit (not re-derived in full here given the acknowledged
methodology of not re-verifying an already-proven-correct shared formula from scratch in every file that reuses
it).

### Memory/resource lifetime
Constructor/destructor pair (lines 113-127) is minimal and correct: `RegisterForWindow`/`UnregisterForWindow`
called symmetrically, with no fallible step between the null-check throw and the registration call — see
Cross-File Observations.

### C++ correctness
No unsafe casts; `BlendStateToCompositeOp`'s `isAdd`/`symmetric` boolean derivation (lines 263-264) correctly
requires both color and alpha factors to match before checking specific values, avoiding a false-positive preset
match for an asymmetric custom `BlendState` that happens to share one channel's factors with a real preset.

### Performance
N/A — 2D-only backend, no hot-path concerns beyond what's inherent to `Canvas2D`'s own JS-boundary call overhead
(unavoidable, not a CNA-side defect).

### Thread safety
N/A — Emscripten's default (non-pthreads) build model is single-threaded; consistent with every other backend
audited so far.

### Architecture
`BlendStateToCompositeOp` being deliberately extracted as a pure, `EM_JS`-free function specifically for
structural GTest coverage (per its own doc comment, `plans/plan_canvas.md` CANVAS-80) is a good, explicit test-ability
design choice — the kind of thing that should make this function's own correctness easy to verify without a real
browser.

### Maintainability
349 lines, proportionate; comment density and quality (each `EM_JS` block has a substantial rationale comment
citing specific plan task IDs and, in several cases, a previously-found-and-fixed bug) matches this audit's
highest-quality files (SdlRenderer, Software, Dx3).

### Portability
Correctly gated behind `#if defined(__EMSCRIPTEN__)` throughout, with a non-Emscripten fallback that at least
compiles (see F1 for the one behavioral gap in that fallback).

### Robustness
See F1.

### Testing
Not independently assessed (queued for `examples-tests-canvas`, 2 files).

## Detailed Findings

### F1 — `ReadBackbuffer()`'s non-Emscripten compile path leaves the output buffer uninitialized rather than zeroing it or asserting

- Severity: LOW-MEDIUM
- Confidence: MEDIUM (real gap in the code; real-world impact depends entirely on whether this backend is ever
  actually built/exercised outside an Emscripten target, which this audit did not independently confirm either way)
- Category: robustness
- Location/symbol: `CanvasGraphicsBackend::ReadBackbuffer` (lines 248-255): `#else (void)x; (void)y; (void)w;
  (void)h; (void)pixels; #endif` — every parameter including `pixels` is merely silenced, with no write to the
  buffer at all.
- Why it matters: if this backend is ever compiled (even just for structural/compile testing, e.g. CI verifying
  the file at least builds on a non-Emscripten host) and something calls `ReadBackbuffer` in that configuration,
  the caller's buffer is left with whatever garbage it already contained — silently, with no error — rather than
  a clear, loud failure (the pattern this codebase otherwise favors consistently, e.g. every `ThrowNo3D` call
  site in this same file). The class's own header comment states this backend is "Emscripten-only," which may
  make this purely theoretical in practice — flagged at LOW-MEDIUM rather than higher specifically because of
  that uncertainty.
- FNA/XNA comparison: N/A.
- Related files: none beyond this file's own `DrawSprite`/`Clear`/`ApplyBlendState` non-Emscripten branches, which
  have the same shape but lower stakes (they silence side-effect-only calls, not a data-producing read).
- Suggested future action (not implemented by this audit): either confirm this backend is never built outside
  Emscripten (making this dead code, not worth changing) or make the non-Emscripten fallback zero the buffer/throw
  for consistency with the rest of the codebase's "loud failure" discipline.

## Cross-File Observations

- **Resolves a cross-cutting question this audit has been tracking**: does `Canvas`'s `RegisterForWindow` call
  site share EasyGL's constructor-ordering bug (that report's F1)? **No** — the only fallible operation
  (`if (!window_) throw`) happens strictly before `RegisterForWindow(window_, this)` (lines 120-121), so no
  partially-constructed `CanvasGraphicsBackend` can ever be registered. Combined with WebGPU's own confirmed-safe
  constructor, this leaves only `SdlGpu` still to check among the four `RegisterForWindow` callers.
- The Porter-Duff `'copy'`-without-clip bug and the tint-math `A²`-darkening bug (both documented as found via
  formal spec verification in `CanvasSpriteBatchBackend.cpp`'s own header comments) are further evidence of the
  same genuine, iterative correctness-hunting discipline already praised in SdlRenderer's/Software's/Dx3's audits.

## Missing or Weak Tests

Not independently assessed (queued for `examples-tests-canvas`).

## Positive Findings

- Confirmed-correct constructor exception safety (no window-registration ordering risk).
- `BlendStateToCompositeOp`'s deliberate test-ability design (pure function, no `EM_JS`/JS dependency).
- Consistent, honest 2D-only discipline matching this codebase's other 2D-only backends.

## Final Assessment

A clean, well-reasoned backend with one narrow, low-confidence robustness gap (F1) and a confirmed-safe
constructor — a genuinely solid file.
