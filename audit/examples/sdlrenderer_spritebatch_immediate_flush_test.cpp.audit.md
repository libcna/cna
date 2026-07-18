# Audit: examples/sdlrenderer_spritebatch_immediate_flush_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritebatch_immediate_flush_test.cpp` (146 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteSortMode::Immediate` per-draw-flush pixel test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_spritebatch_immediate_flush …)` /
  `cna_register_backend_test(NAME SDL_Renderer_SpriteBatch_ImmediateFlush …)`,
  `cmake/Tests/SdlRendererTests.cmake:62-64`. Header traces to Task 670.
- XNA/FNA relevance: `SpriteSortMode.Immediate` (each `Draw()` submitted to the device instantly, not queued).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`pushSprite`'s
  `sortMode_ == SpriteSortMode::Immediate` branch calling `flushSingle` directly instead of queuing, lines
  ~163-172), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp` (`SdlSpriteBatchBackend::Draw`,
  `SdlGraphicsBackend::Clear`).
- Also relevant: `tests/Microsoft/Xna/Framework/Graphics/SpriteBatchTests.cpp` —
  `SpriteBatchSortModeTest.ImmediateFlushesInsideDrawBeforeEnd` /
  `.ImmediateFlushesEachDrawSeparatelyInCallOrder` (confirmed present, lines 499 and 524) — the mock-backend
  tests this file's header explicitly says it complements rather than duplicates.

## Purpose

Proves the end-to-end, GPU-visible consequence of `SpriteSortMode::Immediate`'s dispatch contract: a plain
`GraphicsDevice::Clear()` call issued between a `SpriteBatch::Draw()` and the matching `End()`, all inside one
still-open `Immediate` session, must be interleaved into the real draw order exactly where it's called — not
deferred until `End()` alongside the sprite. Sequence: (1) draw a red sprite at `(100,100,60,60)`; (2)
`dev.Clear(kGreen)` — wipes the whole backbuffer; (3) `End()`. Under correct per-draw flush, the sprite is
already real pixels by step 2, so `Clear()` erases it — the sampled region must read green, not red.

## Executive Verdict

**Healthy.** The single check is a genuinely discriminating end-to-end test (not a restatement of the
mock-backend unit tests it explicitly cites as already covering the dispatch-level claim), and its own
technical premise ("`SdlSpriteBatchBackend::Draw()` calls `SDL_RenderTexture()` synchronously with no queue at
all") was independently confirmed against the current backend source.

## Checklist Results

### API / XNA / FNA parity
`SpriteBatch::Begin(SpriteSortMode::Immediate, BlendState::Opaque, samplerState, nullptr, nullptr, nullptr,
Matrix::getIdentityProperty())` (lines 103-106) and the 8-argument NOXNA `Draw(...)` overload (lines 108-109)
match their `SpriteBatch.hpp` declarations. FNA's `SpriteSortMode.Immediate` contract (each `AddSprite`/
`PushSprite` call flushes immediately rather than deferring to `End()`) is the behavior under test; this
project's `SpriteBatch::pushSprite` correctly mirrors that (`if (sortMode_ == SpriteSortMode::Immediate)
flushSingle(info); else spriteQueue_.push_back(info);` — confirmed by direct reading of `SpriteBatch.cpp`).

### Behavioral correctness
Re-derived the causal chain by hand: `sb_->Draw(...)` at line 108 immediately calls
`SpriteBatch::pushSprite` → `flushSingle` (since `sortMode_ == Immediate`) → `SdlSpriteBatchBackend::Draw`,
which issues a real `SDL_RenderTexture()` synchronously (no internal queue — confirmed by reading
`SdlGraphicsBackend.cpp` lines 124-183: every `Draw` overload calls `SDL_RenderTexture`/`SDL_SetTexture*Mod`
directly). `dev.Clear(kGreen)` at line 114 is likewise an immediate, unqueued `SdlGraphicsBackend::Clear()`
call. Since both operations are synchronous with no backend-side reordering, the sprite is genuinely real
pixels on the render target before `Clear()` runs, and `Clear()` genuinely wipes them — the expected outcome
(region `(130,130)` reads green, not red) is the correct, non-trivial prediction, and the alternative failure
mode the header describes (if `Immediate` were mistakenly treated like `Deferred`, the sprite would be queued
and only flushed at `End()`, i.e. *after* the Clear, making the region incorrectly read red) is a real,
plausible implementation bug this test would actually catch.

### Logic
Single linear sequence, no branching; `result_` set directly from the one `colourMatch` check (line 121).

### Memory/resource lifetime
`redTex_`/`sb_` `unique_ptr`-owned, constructed once in `Initialize()`, consistent with the shard's idiom.

### C++ correctness
No unsafe casts; `colourMatch` tolerance (`tol=60`, line 63) is generous but still discriminates red
(255,0,0) from green (0,255,0) decisively on both channels.

### Performance
N/A — single-frame test with one draw, one clear.

### Thread safety
N/A.

### Architecture
Correctly requires `PresentationMode::NativeBackBuffer` (line 135) for the same physical/logical-coordinate
reason established across this batch; consistent usage, not re-derived incorrectly here.

### Maintainability
146 lines, single-purpose, clearly commented; the header's own explanation of *why* the mock-backend tests
aren't sufficient (they can't prove `SdlSpriteBatchBackend` specifically issues a real, unqueued SDL command)
is accurate and avoids redundant test design.

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
The choice to interleave a plain `GraphicsDevice::Clear()` (rather than, say, a second `SpriteBatch::Draw`)
between the sprite draw and `End()` is a deliberately strong test: it proves the sprite is visible to a
*completely separate* device operation immediately, not merely that two sprites drawn in `Immediate` mode
appear in the right relative order (which a queued-but-internally-ordered implementation could satisfy without
actually being unqueued).

### Testing
Correctly scoped as the complement to the existing mock-backend `SpriteBatchSortModeTest` tests — confirmed
both cited test names (`ImmediateFlushesInsideDrawBeforeEnd`, `ImmediateFlushesEachDrawSeparatelyInCallOrder`)
exist in `SpriteBatchTests.cpp` as claimed, so the "already covered at the dispatch level" claim is accurate,
not an assumption.

### Cross-file consistency
The header's technical claim about `SdlSpriteBatchBackend::Draw()`'s synchronous, unqueued nature was
independently re-confirmed against the current `SdlGraphicsBackend.cpp` source (not merely trusted) — no
drift found between the comment and the actual implementation.

## Detailed Findings

None. No CRITICAL/HIGH/MEDIUM/LOW findings in this file.

## Cross-File Observations

- Complements `sdlrenderer_spritebatch_deferred_order_test.cpp` (same batch): that file proves `Deferred`'s
  *ordering* semantics; this file proves `Immediate`'s *timing* semantics — a clean split of two related but
  distinct `SpriteSortMode` contracts, not an overlap.
- Same `PresentationMode::NativeBackBuffer` / `GetBackBufferData` idiom used consistently across the shard.

## Missing or Weak Tests

None identified for this file's stated scope. A natural (not currently present, and not claimed to be) follow-on
would be interleaving a second `SpriteBatch::Draw` call (rather than a plain `Clear()`) to additionally confirm
draw-order interleaving with *itself* under `Immediate` — but the current single-`Clear()` design is already a
strong, sufficient proof of the specific "genuinely flushed before the next device operation" claim being made.

## Positive Findings

- Correctly avoids re-testing what the mock-backend unit tests already prove, focusing specifically on the one
  thing only a real backend can demonstrate.
- The interleaved-`Clear()` design is a stronger discriminator than a same-batch multi-sprite ordering check
  would have been.
- Header's technical claims about the backend's synchronous dispatch were independently verified against the
  current source, not just asserted.

## Final Assessment

A well-targeted, correctly-scoped end-to-end test whose single assertion genuinely discriminates the intended
behavior (immediate per-draw flush) from the plausible failure mode (deferred-to-End queuing), with all of its
technical premises independently confirmed against the current production code.
