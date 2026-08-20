# Audit: src/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.cpp`
- Audit status: AUDITED (**static analysis only** — this backend requires Windows + DirectDraw via the `free-direct`
  sibling repo; not buildable or runnable on this Linux sandbox, per decision D-P4)
- Subsystem: `backend-dx3` shard
- File type: C++ implementation (1031 lines) — real CPU 2D compositor over DirectDraw
- Related header/implementation: `include/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.hpp` (audited separately,
  same shard)
- XNA/FNA relevance: implements `SpriteBatch`/`Texture2D`/`RenderTarget2D`/`BlendState` semantics on real
  `IDirectDraw`/`IDirectDrawSurface`, intentionally 2D-only (no 3D XNA surface at all).
- Graphics backend relevance: one of the 14 confirmed backends; per D-6, its `free-direct` dependency (external
  sibling repo providing the actual DirectDraw implementation) is out of audit scope — this report covers only the
  CNA-side adapter code in this file, treating `free-direct`'s API as an opaque, correctly-documented dependency.
- FNA reference: cross-checked the four `BlendState` preset formulas (Opaque/AlphaBlend/NonPremultiplied/Additive)
  against `BlendState.cpp`'s documented factor/function values and the standard D3D blend equation
  (`out = src*SrcBlend (op) dst*DstBlend`, applied per-channel with each channel's own Color/Alpha factor pair).
- Main related tests: `examples-tests-dx3` (9 files, not yet audited at time of writing) — none of which can be
  executed in this sandbox either (Windows-only).

## Purpose

Implements a full 2D-only `IGraphicsBackend` backed by real `IDirectDraw`/`IDirectDrawSurface` objects from the
`free-direct` sibling library: a "shadow backbuffer" offscreen surface that all drawing targets (since
`free-direct`'s own primary surface is never directly writable per this file's own well-documented investigation),
a single identity `Blt()` for `Present()`, offscreen-surface-backed textures/render-targets, and a from-scratch CPU
compositor (`CompositeQuad`) implementing real per-formula alpha blending, bilinear/point sampling, and
wrap/clamp/mirror addressing for anything the hardware `BltFast` identity fast-path can't handle. This is a
substantial, genuine 2D rendering engine, in the same category of ambition as the Software backend's rasterizer
(audited separately) rather than a thin wrapper.

## Executive Verdict

**Mostly healthy.** The blend-math, exception-safety-during-construction, and documentation quality are all
excellent (see Positive Findings — I initially suspected the `NonPremultiplied`/`Additive` alpha-channel formulas
of squaring `srcAlpha` in error, but independently re-derived them against `BlendState.cpp`'s real
`AlphaSourceBlend=SourceAlpha` factor and confirmed `srcAlpha²` is the mathematically correct GPU blend-equation
result for that specific factor combination, not a bug). One genuine, concrete robustness gap remains: a failed
resize (`SetVirtualResolution` → `Impl::CreateSurfaces`) unconditionally tears down the working primary/backbuffer
surfaces *before* confirming the replacement surfaces can be created, leaving the backend in an unusable
null-surface state with no rollback on failure (F1).

## Checklist Results

### API / XNA / FNA parity

`DetectBlendMode` (lines 183-198) correctly requires *both* the four blend factors *and* both blend functions to
match a preset by exact value (not by `BlendState` identity, since the backend never sees the object, only raw
ints) — explicitly and correctly guards against misdetecting a custom `BlendFunction::Subtract` state that happens
to share Opaque's factors as if it were really Opaque (the comment at lines 178-182 states this reasoning
explicitly, and the code matches it: `bothAdd` is checked in every branch before comparing factors). I
independently re-derived all four formulas in `CompositeQuad` against `BlendState.cpp`'s real preset definitions:

- **Opaque** (ColorSrc=One/ColorDst=Zero, alpha same): `out = src` — matches code exactly (lines 332-339, straight
  overwrite, no blending with `dp`'s prior value).
- **AlphaBlend** (ColorSrc=One, AlphaSrc=One, Dst=InvSrcAlpha both): `outRGB = srcRGB + dstRGB*(1-srcA)`,
  `outA = srcA*1 + dstA*(1-srcA)` — matches code exactly (lines 340-352, note the alpha channel correctly uses
  `srcA*255` with an implicit factor of 1, *not* `srcA*srcA`, since `AlphaSrcBlend=One` here, not `SourceAlpha`).
- **NonPremultiplied** (ColorSrc=SourceAlpha, AlphaSrc=SourceAlpha, Dst=InvSrcAlpha both): `outRGB =
  srcRGB*srcA + dstRGB*(1-srcA)`, `outA = srcA*srcA + dstA*(1-srcA)` — matches code exactly (lines 353-363); the
  `srcA*255*srcA` term for the alpha channel specifically (line 361) is **correct**, not a copy-paste bug, because
  `AlphaSrcBlend=SourceAlpha` here means the alpha channel's own blend factor genuinely is `srcAlpha`, applied to
  the alpha *value* which is also `srcAlpha` — hence the literal `srcAlpha²` result matches real D3D/XNA hardware
  blending for this exact preset.
- **Additive** (ColorSrc=SourceAlpha, AlphaSrc=SourceAlpha, Dst=One both): `outRGB = srcRGB*srcA + dstRGB`,
  `outA = srcA*srcA + dstA` — matches code exactly (lines 364-371), same `srcAlpha²` reasoning as NonPremultiplied.

### Behavioral correctness

The `Present()` implementation's choice to use a single identity `Blt()` rather than `Flip()` (lines 534-546) is
well-reasoned and explicitly justified against `free-direct`'s own documented auto-present-on-dirty-Blt behavior —
this reads as a correct, deliberate design choice, not an oversight, and the comment shows the author actually
traced through `free-direct`'s behavior rather than assuming it.

`Clear()`'s choice to avoid `DDBLT_COLORFILL` (lines 519-532) is backed by a concrete, cited investigation
("free-direct's own `FillColor()` hardcodes the written alpha byte to 255... a real bug, found in review and fixed
here") — this is exactly the kind of cross-repository verification this audit's own D-6 decision anticipated would
be needed when auditing a backend whose real logic lives partly in a sibling repo; good evidence the CNA-side
authors already did this verification themselves.

### Logic

**F1 (Detailed Findings)** — the one substantive logic/robustness gap found.

### Memory/resource lifetime

Constructor exception safety is correct: if `Dx3GraphicsBackend`'s constructor body throws after `impl_` (a
`unique_ptr<Impl>`) has already been constructed via its member initializer, C++ guarantees `impl_`'s destructor
runs during stack unwinding, and `~Impl()` (lines 451-456) correctly null-guards every `Release()` call — verified
that whichever partial state exists at any throw point (`DirectDrawCreate` failing before `dd` is set;
`SetCooperativeLevel` failing after `dd` is set; `CreateSurfaces` failing at various points) is always safely
destructible with no double-release or leaked-but-unreachable COM object. `CreateSurfaces` itself (lines 462-493)
correctly releases a partially-created `primary` if the subsequent `backBuffer` creation fails (lines 484-489),
avoiding a leak on that specific failure path — but see F1 for the *different* problem this same function creates
on a **resize** (not initial construction) failure.

`Dx3RenderTargetBackend`'s destructor (lines 702-706) correctly calls `UnbindAsRenderTarget()` before releasing its
own surface — preventing the `Impl::currentTargetSurface` pointer from ever dangling after the bound render
target is destroyed while still bound (verified `UnbindAsRenderTarget()`'s own guard at lines 732-740 only clears
the slot if it still points at *this* surface, correctly handling the case where a different render target was
bound in the meantime).

### C++ correctness

`Dx3TextureBackend`/`Dx3RenderTargetBackend`/`Dx3SpriteBatchBackend` are all correctly non-copyable where they
manage a unique COM-like resource (explicit `= delete` copy ctor/assignment on the first two; the third has no
raw resource of its own, correctly). `CreateVertexBuffer`/`CreateIndexBuffer16` (lines 1003-1011) call
`[[noreturn]] ThrowNo3D(...)` with no following `return nullptr;` — correct and warning-free, since `[[noreturn]]`
lets the compiler's control-flow analysis know the function body never falls through, unlike if a non-`[[noreturn]]`
throw helper were used.

### Performance

`FillSurfaceColor` (used by every `Clear()` call, lines 110-129) does a scalar per-pixel, per-channel write loop
after `Lock()` rather than a bulk fill (e.g. precomputing one 32-bit pixel value and writing 4 bytes at a time via
a wider store, or a row-level `memset`-style fill when possible) — a real but minor, `LOW`-severity performance
observation given `Clear()` typically runs once or a few times per frame and this backend's own stated design
priority is correctness (explicitly rejecting the faster hardware `DDBLT_COLORFILL` specifically because it was
observed to be behaviorally wrong, per the `Clear()` comment) — not elevated further given that documented
correctness-over-speed stance.

`CompositeQuad`'s per-pixel compositing loop (lines 304-376) re-evaluates `BarycentricWeights` for both candidate
triangles at every pixel in the quad's bounding box, discarding the result for whichever triangle the pixel isn't
inside — standard, reasonable approach for a CPU 2D compositor of this scope, consistent with the equivalent
Software/SdlRenderer-adjacent designs already audited.

### Thread safety

N/A — consistent with every other backend audited so far.

### Architecture

Clean 2D/3D boundary matching SdlRenderer's own discipline: every 3D-only method (`ClearColorAndDepth` family,
`SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`, `CreateVertexBuffer`, `CreateIndexBuffer16`,
`DrawColoredPrimitives`, `DrawIndexedColoredPrimitives`) is explicitly overridden to throw via the shared
`ThrowNo3D()` helper (lines 993-1020) — the *correct* pattern this audit's `IGraphicsBackend.hpp` report flagged as
generally risky when left to silent interface defaults. `CreateOcclusionQuery` is deliberately **not** overridden
(the header's own comment, lines 129-134, explains this was a deliberate Phase X7 correction from an earlier,
inconsistent throwing override) — a good example of a design decision that was revisited and fixed, documented
honestly as a correction rather than silently changed.

### Maintainability

1031 lines, proportionate to a genuine CPU 2D compositor with real, distinct per-preset blend math and
bilinear/point/wrap/mirror/clamp sampling — comparable in scope and quality to the Software backend's rasterizer.
Comment density and quality matches this audit's other top-tier files (SdlRenderer, Software) — nearly every
non-trivial decision cites its own design-decision number (`plans/plan_dx3.md`) or task ID (`DX3-NN`) and explains *why*,
not just *what*.

### Portability

N/A beyond the inherent Windows-only nature of DirectDraw itself (out of scope to re-litigate — this is the
backend's entire raison d'être, not a portability bug).

### Robustness

**F1** below is the substantive finding.

### Testing

Not independently assessed (queued for `examples-tests-dx3`, 9 files) — none of which can be executed in this
Linux sandbox regardless (Windows-only), consistent with decision D-P4.

## Detailed Findings

### F1 — `Impl::CreateSurfaces` destroys the working primary/backbuffer surfaces before confirming the replacement surfaces succeed, so a failed resize leaves the backend permanently unusable

- Severity: MEDIUM
- Confidence: HIGH
- Category: robustness / resource lifecycle
- Location/symbol: `Dx3GraphicsBackend::Impl::CreateSurfaces` (lines 462-493), called from both the constructor
  (line 514, where there is nothing yet to lose) and `SetVirtualResolution` (line 576, where there is)
- Evidence: `CreateSurfaces` unconditionally releases and nulls `backBuffer`/`primary` at its very start (lines
  464-465) *before* attempting `SetDisplayMode` (line 467) or either `CreateSurface` call (lines 474, 483). If
  `SetVirtualResolution(newWidth, newHeight)` is called on an already-running backend and any of those three calls
  subsequently fails, the exception propagates out of `SetVirtualResolution` with `impl_->primary` and
  `impl_->backBuffer` both already `nullptr` — the *previous*, previously-working surfaces are gone, not restored.
- Why it matters: every other method that touches these surfaces (`Clear()`'s `FillSurfaceColor`, `Present()`'s
  `Blt()`, `ReadBackbuffer()`'s `ReadSurfacePixels`) dereferences `impl_->primary`/`impl_->ActiveSurface()`
  unconditionally with no null check — a `LPDIRECTDRAWSURFACE` that is `nullptr` after a failed resize would cause
  the very next `Clear()`/`Present()`/`ReadBackbuffer()` call to invoke a virtual method through a null pointer
  (undefined behavior / crash), not a clean, catchable second exception. A game that catches the resize failure and
  tries to continue (e.g. falling back to the old resolution, or just logging and carrying on) would then crash on
  its next frame instead.
- FNA/XNA comparison: N/A directly (this is a CNA-internal resource-lifecycle question, not an XNA behavior
  question) — but note that a real `GraphicsDevice.Reset()` failure in XNA/D3D does *not* destroy the
  previously-working device state; the expectation this finding measures against is "a failed resize should leave
  the backend at least as usable as before the attempt," which this implementation does not meet.
  the constructor's own call to `CreateSurfaces` (line 514) is unaffected by this finding since there are no
  prior surfaces to lose there — the risk is specific to the resize path.
- Related files: `include/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.hpp` (declares `SetVirtualResolution`,
  whose header comment at lines 72-79 of the `.hpp` already documents a *different*, narrower known limitation —
  physical letterbox scale not being re-applied after a later resize — but does not mention this failure-path gap).
- Suggested future action (not implemented by this audit): restructure `CreateSurfaces` to build the new
  `primary`/`backBuffer` surfaces first and only release the old ones after the new ones are confirmed to exist
  (or, at minimum, leave the old surfaces in place and only swap them in on success), so a failed resize degrades
  to "resize didn't happen, old surfaces still work" rather than "backend is now permanently broken."

## Cross-File Observations

- The `Present()`/`Flip()` reasoning (lines 536-546) and the `Clear()`/`DDBLT_COLORFILL` alpha-clobbering
  discovery (lines 521-524) both required directly reading `free-direct`'s own source
  (`../free-direct/src/directdraw/DirectDraw.cpp`, per the comments' own citations) — worth confirming during any
  future `free-direct`-adjacent work that these citations remain accurate if that sibling repo changes, since this
  file's correctness is partly contingent on behavior this audit did not itself independently re-verify against
  the sibling repo (out of scope per D-6, but worth flagging as a dependency-freshness risk).
- `Dx3SpriteBatchBackend::Draw`'s general-path quad-corner construction (lines 926-951) explicitly reuses the same
  rotation/scale/origin formula already vetted in the Software backend's audit (`SoftwareSpriteBatchBackend::Draw`)
  — consistent, deliberate code-pattern reuse across backends rather than an independent, potentially-divergent
  reimplementation.

## Missing or Weak Tests

Given F1 is a resize-failure path, the most valuable (if only testable on Windows) missing test would be a
`SetVirtualResolution` call engineered to fail (e.g., an unsupported display mode) followed by an assertion that
`Clear()`/`Present()` still work afterward — not assessed against `examples-tests-dx3`'s actual content yet.

## Positive Findings

- All four blend-mode formulas are correct, including the two ostensibly-suspicious `srcAlpha²` terms this audit
  initially flagged and then confirmed correct via independent re-derivation against `BlendState.cpp`'s real
  per-channel blend factors — a good example of this audit's own "verify before asserting" discipline paying off
  (a shallower read would have wrongly flagged this as a bug).
- Constructor and `CreateSurfaces`-during-construction exception safety is genuinely correct with no leaks on any
  traced failure path.
- The documented history of real bugs found and fixed during this backend's own development (the
  `DDBLT_COLORFILL` alpha-clobbering discovery, the `Flip()`-vs-`Blt()` reasoning) matches the high documentation
  bar set by SdlRenderer and Software, this audit's other two most mature backend files so far.

## Final Assessment

A well-engineered, thoroughly-documented 2D compositor backend with correct blend math and constructor-time
exception safety, let down by one concrete resize-failure robustness gap (F1) that would benefit from a
build-then-swap restructuring of `CreateSurfaces`.
