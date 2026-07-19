# Audit: examples/ascii_present_test.cpp

## Metadata
- Source file: `examples/ascii_present_test.cpp` (153 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-tests-ascii` shard
- File type: standalone backend integration-test executable (`Game` subclass)
- XNA/FNA relevance: exercises `GraphicsDevice::Clear`/`Present` (public XNA API) against the ASCII
  backend's real-window glyph-grid rendering

## Purpose
Pixel-tests the real presented window content: renders a known solid color, reads back the real
window's presented pixels (not the offscreen `gameTarget_`), and asserts the correct glyph/color
appears per-cell, plus verifies `SetCellSize()`/`GetCellSize()` genuinely affect the drawn grid
dimensions (not just accessor round-trip).

## Executive Verdict
Correct, with one especially careful piece of test engineering: the comment at lines 77-84
explains a real, non-obvious pitfall this test avoids — `Present()`'s real double-buffer swap makes
an immediate readback of the "current" buffer unreliable right after a swap (SDL_Renderer/OpenGL
present via a swap, not a copy), so the test calls a dedicated
`DrawQuantizedGridForTesting()` test-only redraw path (documented as drawing the identical
quantized grid without swapping) instead of trying to read back immediately after the real
`Present()`. This is a correctly-reasoned solution to a real double-buffering testability problem,
not a workaround masking a bug.

## Checklist Results
- Check A/B's exact pixel-position math (cell (0,0)'s corner vs. center, tied to the known '+'
  glyph's specific on/off bitmap rows) is precisely derived from the glyph bitmap, not guessed —
  cross-checked as consistent with `ascii_fontatlas_test.cpp`'s own popcount-based bitmap
  inspection of the same glyph ramp.
- Check D's grid-dimension verification (`GetLastGridDimensionsForTesting()`) genuinely tests that
  `SetCellSize()` affects what `Present()`/the redraw path draws, not merely that the accessor
  stores and returns the value — a stronger claim than a pure getter/setter round-trip test.
- `SetCellSize(0, 8)` is correctly asserted to throw `std::invalid_argument` — a real validation
  path, not merely undefined for a zero dimension.

## Detailed Findings
None.

## Cross-File Observations
The `DrawQuantizedGridForTesting()`/`ReadRealBackbufferForTesting()` test-only accessor pattern
here (bypassing `Present()`'s real swap for testability) is architecturally similar in spirit to
other backends' `*ForTesting()` escape hatches found elsewhere in this audit (e.g.
`VideoPlayerTestAccess.hpp`'s `SimulateAudioDeviceBecomingUnavailable()`), though this instance is a
cleaner design: it redraws identical content rather than mutating global state, so it carries no
comparable test-ordering side-effect risk.

## Missing or Weak Tests
None identified for this file's stated scope.

## Positive Findings
The double-buffer-swap-timing comment (lines 77-84) is an excellent example of documenting a real,
non-obvious testability pitfall and the specific reasoning behind the chosen workaround — exactly
the kind of "why" comment this project's own guidelines value.

## Final Assessment
No findings.
