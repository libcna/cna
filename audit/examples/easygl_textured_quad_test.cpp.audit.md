# Audit: examples/easygl_textured_quad_test.cpp

## Metadata

- Source file: `examples/easygl_textured_quad_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `Texture2D` + `SpriteBatch` + backbuffer-readback
  integration test (Task 86)
- File type: C++ example/integration-test executable (`TexturedQuadTest : Microsoft::Xna::Framework::Game`, `main()`)
- Related production code: `Texture2D::CreateFromPixels` (`Texture2D.cpp:811`), `GraphicsDevice::GetBackBufferData`
  (`GraphicsDevice.cpp:1768-1799`), `SpriteBatch::Draw(Texture2D&, Rectangle, Rectangle, Color)`
- XNA/FNA relevance: exercises real XNA-facing `Texture2D`/`SpriteBatch`/`GraphicsDevice.GetBackBufferData` surface
  (`CreateFromPixels` itself is `NOXNA`, per `Texture2D.hpp:228`)
- Main related tests: this file is itself the only test of this specific scene (1×1 solid-color texture drawn
  full-screen); no golden-image counterpart in this shard as audited.

## Purpose

Smoke-tests the minimal texture-to-screen pipeline: builds a 1×1 solid-red RGBA texture via the NOXNA
`Texture2D::CreateFromPixels` convenience factory, draws it full-screen through `SpriteBatch`, and confirms the
rendered center pixel is red via `GraphicsDevice::GetBackBufferData`. Placement under `examples/` as a standalone
`easygl_*_test.cpp` executable matches every sibling in `examples-tests-easygl`.

## Executive Verdict

**Mostly healthy** — the file's actual, asserted textured-draw check is correct and grounded in real, verified
production APIs, but the test's own comment claims a second check ("clear+readback before adding SpriteBatch")
that is never actually asserted (F1); the pixel value it reads back is silently discarded.

## Checklist Results

### API / XNA / FNA parity
`Texture2D::CreateFromPixels(GraphicsDevice&, int, int, const std::vector<uint8_t>&)` (line 37) is confirmed
`NOXNA` in `Texture2D.hpp:228` — correctly not presented as a real XNA constructor. `GetBackBufferData(const
Rectangle*, Color*, int, int)` (line 57, 68) matches the real overload set in `GraphicsDevice.hpp:274-289`, itself
matching FNA's generic `GraphicsDevice.GetBackBufferData<T>(Rectangle?, T[], int, int)` overload
(`GraphicsDevice.cs:860`) in spirit (CNA specializes to `Color*` instead of a generic `T[]`, a reasonable,
documented CNA convention elsewhere in the codebase). `SpriteBatch::Draw(texture, destRect, srcRect, color)` (line
62-65) is a standard XNA overload.

### Behavioral correctness
The final, asserted check (lines 59-88) is correct and does verify what its name claims: clears to black, draws a
1×1 red texture stretched to the full viewport via `Rectangle(0,0,W,H)` destination / `Rectangle(0,0,1,1)` source,
then reads back the center pixel and requires `R==255, G==0, B==0` (alpha intentionally not checked, consistent
with the file's own header comment which only claims to assert `R=255,G=0,B=0`).

### Logic
See **F1** — the first `Clear`+`GetBackBufferData` pair (lines 51-57) computes a `pixel` value that is completely
overwritten (line 67: `pixel = Color(0,0,0,0);`) before ever being read, so its own inline comment ("should be the
clear colour") is not actually verified by any assertion.

### Memory/resource lifetime
`redTex_` (a `Texture2D` value member, not a pointer) and `sb_` (`std::unique_ptr<SpriteBatch>`) are both
constructed in `Initialize()` after the base `Game::Initialize()` call and used only within the single `Draw()`
call before `Exit()` — no dangling-pointer or use-after-free risk.

### C++ correctness
No manual buffer arithmetic; `std::vector<uint8_t> px` sized exactly `{255,0,0,255}` for a 1×1 RGBA texture, matches
`CreateFromPixels`'s expected `width*height*4` byte layout.

### Testing
This file is itself a test; see F1 for its own internal test-coverage gap.

## Detailed Findings

### F1 — First `Clear`+`GetBackBufferData` readback result is discarded without any assertion

- Severity: MEDIUM
- Confidence: HIGH
- Category: testing / robustness
- Location/symbol: `TexturedQuadTest::Draw()`, lines 51-57 vs. 60-72
- Evidence:
  ```cpp
  device.Clear(Color(255, 0, 0, 255));
  device.SetDepthTestEnabled(false);
  // Read back the centre pixel — should be the clear colour.
  const Rectangle region(W / 2, H / 2, 1, 1);
  Color pixel(0, 0, 0, 0);
  device.GetBackBufferData(&region, &pixel, 0, 1);
  // Now draw the 1×1 red texture and read back again.
  device.Clear(Color(0, 0, 0, 255));
  ...
  pixel = Color(0, 0, 0, 0);
  device.GetBackBufferData(&region, &pixel, 0, 1);
  const bool pass = (pixel.getRProperty() == 255 && ...);
  ```
  `pixel` from the first `GetBackBufferData` call is never inspected before being reassigned to
  `Color(0,0,0,0)` two lines later — the comment "should be the clear colour" implies an expectation that is never
  checked by any `if`/assert/printf.
- Why it matters: the file's own stated design ("check clear+readback before adding SpriteBatch") is presented as
  part of the test's coverage, but a regression in `GraphicsDevice::Clear()` or the plain (no-region-offset)
  `GetBackBufferData` path would go completely undetected here — only the combined Clear+SpriteBatch+Draw path is
  actually asserted. This narrows the file's real coverage to strictly less than what a reader would infer from its
  own comments.
- FNA/XNA comparison: N/A (test-only gap, not a behavioral parity issue).
- Related files: none — self-contained.
- Suggested future action (not implemented by this audit): either assert on the first `pixel` value (`R==255 &&
  G==0 && B==0`) before it's overwritten, or remove the dead first readback and its misleading comment.

## Cross-File Observations

- Uses the same `done_`/`result_` single-frame-then-`Exit()` pattern as most other files in this shard.

## Missing or Weak Tests

- See F1. No alpha-channel assertion anywhere in the file (consistent with the file's own stated scope, not a gap
  relative to its claimed purpose).
- Only tests the opaque, full-viewport-stretch case; no scaled/rotated/partial-source-rect textured draw is
  exercised by this file (reasonable given the file's narrow, explicitly-scoped purpose).

## Positive Findings

- The one check that *is* asserted is correct, well-grounded in the real `Texture2D`/`SpriteBatch`/
  `GetBackBufferData` APIs (all cross-checked against their actual signatures during this audit), and exercises a
  genuinely meaningful end-to-end path (texture upload → sprite draw → backbuffer readback).
- Correctly marks `CreateFromPixels` as CNA-only via its `NOXNA` declaration rather than presenting it as if it
  were part of the XNA 4.0 `Texture2D` surface.

## Final Assessment

A small, mostly-correct smoke test whose one real weakness is self-inflicted: it describes and appears to perform
a two-stage check but only ever asserts the second stage, silently discarding the first. The asserted behavior
itself is correct and grounded in real API signatures.
