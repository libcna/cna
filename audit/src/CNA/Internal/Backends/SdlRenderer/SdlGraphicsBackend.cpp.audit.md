# Audit: src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
- Audit status: AUDITED
- Subsystem: `backend-sdlrenderer` shard
- File type: C++ implementation (832 lines)
- Related header/implementation: `include/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.hpp` (audited
  separately, same shard)
- XNA/FNA relevance: implements `SpriteBatch`/`Texture2D`/`RenderTarget2D`/`BlendState` semantics on top of SDL3's
  2D renderer API; intentionally does not implement any 3D XNA surface (`GraphicsDevice`'s 3D draw/state methods).
- Graphics backend relevance: one of the 14 confirmed backends — the intentionally-2D-only one built directly on
  `SDL_Renderer` (as opposed to Canvas/Ascii, which are 2D-only for different underlying-API reasons).
- FNA reference: cross-checked the sprite-rotation-pivot math (Task 671 fix) and `PresentInterval` mapping
  against FNA's own `SpriteBatch.GenerateVertexInfo`/`GraphicsDevice.PresentationParameters` semantics.
- Main related tests: `examples-tests-sdlrenderer` (67 files — the largest example-test shard after EasyGL/Bgfx/
  Vulkan, not yet audited at time of writing) plus `docs/sdl-renderer-2d-completeness.md`.

## Purpose

Implements a full 2D-only `IGraphicsBackend` on top of SDL3's `SDL_Renderer` API: texture creation/update,
render-target-via-`SDL_TEXTUREACCESS_TARGET`, blend-state translation to `SDL_ComposeCustomBlendMode`, scissor
rects, logical-presentation-based virtual-resolution scaling, backbuffer readback, and a `SpriteBatch` adapter that
maps XNA's `Draw()` overloads onto `SDL_RenderTexture(Rotated|Affine)`. Every 3D-only entry point
(`DrawColoredPrimitives`, vertex/index buffer creation, occlusion queries, depth/stencil clear/state) throws a
clear, named exception via a shared `ThrowNo3D()` helper rather than silently degrading — an honest, correctly-
implemented "2D-only by design" contract, matching `SupportsCapability()`'s unconditional `false` in the header.

## Executive Verdict

**Healthy**, and notably one of the most evidently battle-tested files audited so far in this pass: the majority
of its non-trivial logic (sprite rotation pivot placement, transform-matrix application via
`SDL_RenderTextureAffine`, texture min/mag filter mapping, blend-mode `Begin()`-clobbering, `PresentInterval::Two`
handling) carries an explicit "Task NNN finding" comment describing a *real, previously-shipped* bug and its fix,
with a clear before/after rationale each time. Two lower-severity observations remain (F1: an inconsistency in how
`stride` is handled for texture uploads vs. the Headless/Software backends; F2: `Texture3D`/`TextureCube`
construction silently returns `nullptr` rather than throwing, unlike every other unsupported-feature path in this
file — though this is a project-tracked, self-disclosed gap, not a hidden one).

## Checklist Results

### API / XNA / FNA parity

The sprite-rotation-pivot fix (Task 671, lines 223-241) is a precise, correctly-reasoned XNA-parity correction:
XNA's `Draw(destRect, ..., origin, ...)` requires `origin` (in source-texture pixel space) to map to exactly
`(destRect.X, destRect.Y)` on screen *invariant under rotation* — the code correctly derives this from FNA's own
`GenerateVertexInfo` formula (subtract origin before rotating, then translate) and reconciles it against
`SDL_RenderTextureRotated`'s different contract (`center` is a pivot *within* `dstrect`'s local space). The
`SDL_RenderTextureAffine` fallback for a non-identity `transformMatrix` (Task 675, lines 251-298) correctly applies
rotation first, then the transform, matching "FNA's real vertex pipeline order" per its own comment — and the
corner-to-parameter permutation for flip handling (lines 284-292) is a sound way to express `SpriteEffects` flips
through an API that has no flip parameter of its own.

`ToSdlBlendFactor`/`ToSdlBlendOperation` (lines 648-681) correctly map the 10 directly-representable `Blend`
enum values and the 5 `BlendFunction` values 1:1 to their `SDL_BlendFactor`/`SDL_BlendOperation` equivalents, and
correctly *throw* (not silently substitute) for `Blend::BlendFactor`/`InverseBlendFactor`/`SourceAlphaSaturation`,
which have no SDL equivalent — the right call given this backend's stated "must not silently...per unsupported
combination" policy (Task 700, cited in the comment).

### Behavioral correctness

`ReadBackbuffer` (lines 591-641) correctly distinguishes "reading the real window backbuffer" (where
`SDL_RenderReadPixels` operates in physical coordinates and must be mapped through
`SDL_GetRenderLogicalPresentationRect`) from "reading a bound custom render target" (where logical presentation
doesn't apply) — and, notably, *throws* rather than silently returning wrong pixels when the physical/logical size
mismatch would make exact-pixel readback untrustworthy (letterbox/stretch scaling active), which is exactly the
right call for a method whose callers (`GraphicsDevice::GetBackBufferData`, this project's own pixel-verification
test methodology) depend on byte-exact results.

`SetSwapInterval` (lines 519-536) correctly passes `PresentInterval::Two`'s raw value of 2 straight through to
`SDL_SetRenderVSync` (Task 713 fix) instead of collapsing every positive interval to 1, with a documented,
empirically-observed fallback (some drivers reject 2) rather than assuming success.

### Logic

**F1, F2 (Detailed Findings)** below. Everything else checked traces correctly: `applyLogicalPresentation`'s
`FixedHeightDynamicWidth` derivation (lines 340-350) recomputes `logicalWidth` from the real output aspect ratio
exactly once per call, consistent with `IGraphicsBackend.hpp`'s own documented `CnaPresentationMode` semantics for
that enum value.

### Memory/resource lifetime

`SdlTextureBackend`/`SdlRenderTargetBackend` both correctly `SDL_DestroyTexture` in their destructors, guarded by a
null check; `SdlGraphicsBackend`'s destructor correctly destroys its own `renderer` but explicitly does *not* own
or destroy `window` (documented in both the constructor and destructor with a clear ownership comment) — consistent
with `IGraphicsBackend.hpp`'s own established convention that the window is owned by `GraphicsDevice`/the platform
layer, not the backend. `SdlSpriteBatchBackend::Draw`'s three overloads all correctly reach the texture's native
handle via the virtual `GetNativeTexture()`/`GetWidth()`/`GetHeight()` rather than an unchecked
`static_cast<const SdlTextureBackend&>` — explicitly called out (Task 705, repeated at all three call sites) as
avoiding real UB, since the argument may actually be an `SdlRenderTargetBackend` (a *sibling* class, not a subclass
of `SdlTextureBackend`) when a render target is sampled back as a texture.

### C++ correctness

No unsafe casts found in the paths checked; the one `static_cast<CnaPresentationMode>(mode)` in `SetPresentationMode`
(line 511) is safe given `mode` is documented (and, per `GraphicsBackendCreateArgs`'s own convention) to always
originate from a real `CnaPresentationMode` ordinal passed through as `int` at the `IGraphicsBackend` interface
boundary, not arbitrary caller input.

### Performance

`SDL_GetRenderOutputSize` is called on every `Present()` (line 482) purely to detect an output-size change (e.g.
Android surface resize) — a cheap query, not a concern. Nothing else in the hot path (`Clear`/`Present`/`Draw`)
does unnecessary allocation or redundant SDL round-trips beyond what SDL's own API requires per draw call.

### Thread safety

N/A — consistent with every other backend audited so far; no shared state accessed cross-thread in this codebase's
usage pattern.

### Architecture

Clean, honest 2D/3D boundary: every 3D-only virtual is overridden specifically to throw via the shared
`ThrowNo3D()` local helper (lines 779-820) rather than relying on `IGraphicsBackend`'s own softer defaults — the
*correct* choice this audit's `IGraphicsBackend.hpp` report (Finding F1) flagged as generally risky when left to
silent defaults; this backend does not fall into that trap for any of its 3D methods.

### Maintainability

832 lines, proportionate to a real 2D backend with genuine SDL3 API-quirk workarounds. The density of "Task NNN
finding" comments (at least 8 distinct ones counted) is unusually high compared to other files audited so far —
a strong positive signal of iterative, evidence-driven maintenance, not a maintainability concern (each comment is
concise and specific, not a wall of text).

### Portability

`SDL_strcmp(name, "opengl"/"gpu"/"vulkan")` (lines 410-441) is purely diagnostic logging (which SDL renderer driver
got selected) — has no behavioral effect, so a platform where none of these three names match just falls through
to the generic `else` log branch; not a correctness issue.

### Robustness

Every SDL call that can fail (`SDL_CreateTexture`, `SDL_UpdateTexture`, `SDL_RenderTexture*`,
`SDL_SetRenderDrawColor`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_SetRenderDrawBlendMode`, …) is checked and
converted to a `std::runtime_error` carrying `SDL_GetError()`'s message — consistently applied throughout, a
genuinely thorough error-propagation discipline.

### Testing

Not independently assessed (queued for `examples-tests-sdlrenderer`, 67 files) — F1 in particular
(`stride`-handling) is a concrete, testable claim worth checking against existing coverage.

## Detailed Findings

### F1 — `SdlTextureBackend::UpdatePixels`/`SdlRenderTargetBackend::UpdatePixels` pass `stride` to SDL unconditionally, unlike Headless/Software's defensive `stride > 0 ? stride : rowBytes` fallback

- Severity: LOW-MEDIUM (confidence-limited — see below)
- Confidence: MEDIUM (the inconsistency is certain; whether it's ever actually exercised with `stride<=0` is not
  confirmed from this file alone)
- Category: robustness / cross-backend consistency
- Location/symbol: `SdlTextureBackend::UpdatePixels` (lines 45-49), `SdlRenderTargetBackend::UpdatePixels`
  (lines 726-729)
- Evidence: both simply forward `stride` straight into `SDL_UpdateTexture(texture, nullptr, rgba, stride)` with no
  validation or fallback. The `Headless` and `Software` backends' equivalent methods (audited separately, same
  overall interface contract) both compute `effectiveStride = stride > 0 ? stride : rowBytes` before using it,
  treating a non-positive stride as "caller wants tightly-packed rows" rather than passing it through literally.
- Why it matters: if any caller in this codebase ever invokes `Texture2D::SetData`/`UpdatePixels` with `stride=0`
  intending "infer from width," this backend would hand SDL a literal pitch of 0, which `SDL_UpdateTexture`'s own
  contract does not treat as "auto-compute" — likely a silently-wrong or rejected update rather than the same
  result the other two backends would produce for the identical call. Not confirmed as a live bug without checking
  the actual `Texture2D`/`RenderTarget2D` C++ call sites (queued for the `xna-graphics` shard audit) to see whether
  `stride=0` is ever actually passed through in practice, or whether the caller always resolves a real byte pitch
  before reaching any backend.
- FNA/XNA comparison: N/A directly (backend-internal contract, not XNA-facing).
- Related files: `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` (`ITextureBackend::UpdatePixels`'s
  own doc comment doesn't specify what `stride<=0` means, which is arguably the root ambiguity enabling this
  inconsistency); `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp` (the actual caller, to be checked).
- Suggested future action (not implemented by this audit): either document `ITextureBackend::UpdatePixels`'s
  `stride` contract explicitly (e.g. "0 means tightly packed") and make all backends honor it consistently, or
  confirm callers never pass `<=0` and note that invariant explicitly.

### F2 — `Texture3D`/`TextureCube` construction silently returns `nullptr` (no override), inconsistent with this backend's otherwise-loud "unsupported = throw" design — but this is a project-tracked, self-disclosed gap, not a new finding

- Severity: LOW
- Confidence: HIGH
- Category: architecture / robustness (documentation of an already-known issue, not a new discovery)
- Location/symbol: absence of `CreateTexture3D`/`CreateTextureCube` overrides anywhere in
  `SdlGraphicsBackend`/its header; confirmed by the constructor's own startup log message (lines 449-450):
  "Texture3D/TextureCube construction currently succeeds silently with no real backend (BLOCKED, Task 725, see
  docs/sdl-renderer-2d-completeness.md)"
- Evidence: every *other* unsupported feature on this backend (`CreateVertexBuffer`, `CreateIndexBuffer16`,
  `CreateOcclusionQuery`, all `Draw*`/depth-stencil-state methods) is explicitly overridden to throw via
  `ThrowNo3D()`. `CreateTexture3D`/`CreateTextureCube` are the two conspicuous exceptions — they fall through to
  `IGraphicsBackend`'s shared default, which returns `nullptr` without throwing (matching `IGraphicsBackend.hpp`'s
  own audited Finding F1 about silent-default risk in general).
- Why it matters: a caller that doesn't null-check the returned texture (or that only null-checks at higher layers
  inconsistently) could dereference a null `Texture3D`/`TextureCube` backend pointer. That said, this is not a
  fresh discovery — the code *itself* announces this is a known, tracked, currently-blocked gap
  (`docs/sdl-renderer-2d-completeness.md`, Task 725), so the finding here is really "confirmed still present and
  matches the project's own tracking," not a hidden defect this audit is the first to notice.
- FNA/XNA comparison: N/A.
- Related files: `docs/software-backend.md`-style equivalent, `docs/sdl-renderer-2d-completeness.md` (queued for
  the `docs` shard audit — worth checking whether that document's account of Task 725 is still accurate/current).
- Suggested future action (not implemented by this audit): none beyond what's already tracked — flagged here only
  so the cross-cutting findings doc has a record that this audit independently confirmed the gap is real and
  still present as of this pass.

## Cross-File Observations

- `SdlSpriteBatchBackend::Begin()`'s own comment (lines 67-73) documents a *previously real* bug where this method
  unconditionally reset the blend mode, clobbering whatever `SdlGraphicsBackend::ApplyBlendState` had just set —
  worth cross-referencing against `Microsoft::Xna::Framework::Graphics::SpriteBatch.cpp`'s call ordering
  (does it really call `GraphicsDevice::setBlendStateProperty` before `backend_->Begin()` on every `Begin()`, as
  this comment assumes?) when that file is audited.
- The startup diagnostic dump (lines 443-451) is a good practice worth highlighting positively in
  `AUDIT_CROSS_CUTTING_FINDINGS.md` as a pattern other backends could adopt — a one-time, explicit, human-readable
  statement of exactly what this backend does and doesn't support, printed at construction time.

## Missing or Weak Tests

Not independently assessed (queued for `examples-tests-sdlrenderer`). F1's `stride=0` scenario would be a cheap,
valuable addition if not already covered.

## Positive Findings

- Exceptionally well-documented bug-fix history — at least 8 distinct "Task NNN finding" comments, each precisely
  describing a real prior bug, its root cause, and the fix's reasoning. This is some of the highest-quality
  in-code documentation of iterative correctness work seen in this audit pass so far.
- Consistent, thorough SDL error-checking and conversion to descriptive exceptions.
- Correct, principled 2D/3D boundary enforcement — no silent-degradation traps for any of its (many) intentionally
  unsupported 3D features, aside from the one already-tracked Texture3D/TextureCube exception (F2).
- The logical-presentation / virtual-resolution scaling logic (letterbox/overscan/stretch/native/
  fixed-height-dynamic-width) is thorough and re-applies itself correctly on output-size changes (e.g. Android
  surface resize), not just at construction.

## Final Assessment

One of the most mature, well-tested files reviewed in this pass. Two low-severity observations (F1 needs
cross-file confirmation; F2 is an already-tracked, self-disclosed gap) — neither undermines the overall quality of
this backend's implementation.
