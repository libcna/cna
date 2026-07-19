# Audit: src/CNA/Devices/Detail/SdlCameraBackend.cpp

## Metadata

- Source file: `src/CNA/Devices/Detail/SdlCameraBackend.cpp`
- Audit status: AUDITED
- Subsystem: `cna-devices` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A — all of `CNA::Devices` is a NOXNA extension gated behind the `CNA_DEVICES` CMake
  option (default OFF), independent of `CNA_NOXNA`; XNA 4.0/WP7 has no equivalent for any of this shard's
  features (camera, file dialogs, message boxes, system tray, locale, power, system info, URL launching,
  display info, clipboard)
- Graphics backend relevance: none directly (device/OS-integration subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Implements SdlCameraBackend: real SDL_OpenCamera/SDL_AcquireCameraFrame lifecycle, permission polling, and pixel-format handling.

## Executive Verdict

Healthy — careful, correct SDL resource and memory management throughout.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Correctly handles a real, easy-to-miss row-padding bug class**: `TryAcquireFrame()` checks whether `surface->pitch` equals the tight `width*4` row stride before choosing a single `memcpy` vs. a row-by-row compacting copy — the latter path correctly strips SDL surface row padding that `Texture2D::SetDataRGBA()`'s own tightly-packed assumption would otherwise corrupt. Correctly handles SDL memory ownership throughout: `SDL_free()` for camera-id and format lists, `SDL_ReleaseCameraFrame()` for the acquired surface, `SDL_CloseCamera()` in the destructor. Explicitly and correctly treats a device that doesn't honor the requested RGBA8 format as unusable (`CameraState::NotSupported`) rather than attempting ad-hoc pixel conversion, matching the documented first-implementation scope. Honestly notes SDL3's own documented quirk (an empty supported-format list on Emscripten does not mean "no camera", just "ask SDL to pick, then request RGBA anyway").

### Testing
No dedicated test needed (declaration-only) or covered indirectly.

## Detailed Findings

**Correctly handles a real, easy-to-miss row-padding bug class**: `TryAcquireFrame()` checks whether `surface->pitch` equals the tight `width*4` row stride before choosing a single `memcpy` vs. a row-by-row compacting copy — the latter path correctly strips SDL surface row padding that `Texture2D::SetDataRGBA()`'s own tightly-packed assumption would otherwise corrupt. Correctly handles SDL memory ownership throughout: `SDL_free()` for camera-id and format lists, `SDL_ReleaseCameraFrame()` for the acquired surface, `SDL_CloseCamera()` in the destructor. Explicitly and correctly treats a device that doesn't honor the requested RGBA8 format as unusable (`CameraState::NotSupported`) rather than attempting ad-hoc pixel conversion, matching the documented first-implementation scope. Honestly notes SDL3's own documented quirk (an empty supported-format list on Emscripten does not mean "no camera", just "ask SDL to pick, then request RGBA anyway").

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated test needed (declaration-only) or covered indirectly.

## Positive Findings

Careful handling of a real row-padding/pitch bug class most naive implementations miss; correct SDL resource ownership throughout; honest, evidence-based SDL-quirk documentation.

## Final Assessment

See findings above.
