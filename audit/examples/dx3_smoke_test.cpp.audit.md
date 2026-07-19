# Audit: examples/dx3_smoke_test.cpp

## Metadata
- Source file: `examples/dx3_smoke_test.cpp` (138 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-tests-dx3` shard
- File type: standalone backend integration-test executable (`Game` subclass)
- XNA/FNA relevance: exercises `GraphicsDevice::Clear`/`GetBackBufferData`/`Present` (public XNA
  API) against the DX3 backend's DirectDraw device/swap-chain foundation

## Purpose
Foundation smoke test for the DX3 backend: real window, real `Clear()`+`Present()`+pixel-readback
via the shadow-backbuffer surface design (working around `free-direct`'s own
`IDirectDrawSurface::Lock()` never exposing a writable pointer for the primary surface).

## Executive Verdict
Correct, and Check D documents a real, previously-found-and-fixed bug: `free-direct`'s own
`FillColor()` (the `DDBLT_COLORFILL` path `Clear()` originally used) hardcodes the written alpha
byte to 255 unconditionally, silently discarding any non-opaque requested alpha — fixed by writing
all 4 channels directly via `Lock()`/`Unlock()` instead of the colorfill blit. The test directly
verifies the fix with a non-opaque alpha (128) and asserts it survives exactly.

## Checklist Results
- Check B/D both read back a full 4x4 region and verify every pixel matches, not just a single
  sample point — a real "the whole clear operation is correct," not "one lucky pixel happened to be
  right."
- Check D's fixture (10,20,30,128) is deliberately non-opaque and distinct from Check B's fixture
  (20,40,60,255), so the two checks are not accidentally redundant.

## Detailed Findings
None.

## Cross-File Observations
The shadow-backbuffer + identity-`Blt()` present design this file exercises (Check C) matches the
already-recorded persistent-memory finding for this project's DX3 backend architecture (`free-
direct`'s `Lock()` has no writable ptr for the primary surface).

## Missing or Weak Tests
None identified for this file's stated scope.

## Positive Findings
Check D's header-comment disclosure of the real `FillColor()`-hardcodes-alpha-to-255 bug (found in
review, not hypothetical) is a valuable, specific piece of documented engineering history — a future
maintainer investigating why `Clear()` uses `Lock()`/`Unlock()` instead of a blit will find the
exact reason here.

## Final Assessment
No findings.
