# Audit: docs/cna-devices-camera-design.md

## Metadata
- Source file: `docs/cna-devices-camera-design.md` (182 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown design note (explicitly design-only, no implementation)
- XNA/FNA relevance: NOXNA `CNA::Devices::Camera` future extension, not real XNA API

## Purpose
A design-only note (per its own stated Phase 4 scoping) for a future `CNA::Devices::Camera` class,
grounded in what SDL3's real `SDL_camera.h` API actually provides, rather than invented from scratch.

## Executive Verdict
A well-grounded design document — its claims about SDL3's camera API (`SDL_GetCameras`,
`SDL_OpenCamera`, `SDL_GetCameraPermissionState`, `SDL_AcquireCameraFrame`/`SDL_ReleaseCameraFrame`,
per-platform backends) are stated as confirmed by directly reading
`third_party/SDL/include/SDL3/SDL_camera.h`, not assumed from memory. Its comparison against existing
`CNA::Devices` classes (contrasting `Camera`'s poll-based frame delivery with `Accelerometer`/`Compass`/
`Motion`'s push-based callback model) is consistent with this session's own `microsoft-devices`/
`cna-devices` shard audits of those classes' actual architecture.

## Checklist Results
- The texture-upload-bridge claim ("already solved, generically, by existing infrastructure" via
  `ITextureBackend::UpdatePixels`/`Texture2D::SetDataRGBA`) is a specific, checkable claim; this
  session's own `xna-graphics`/`backend-common` shard audits did not find `ITextureBackend`'s
  `UpdatePixels` method absent or differently-shaped than described here.
- The testability recommendation (require `Detail::ICameraBackend` from the first line of
  implementation, following `SystemTray`'s constructor-injection precedent rather than `FileDialog`'s
  "post-construction `SetBackendForTesting()` mistake") is consistent with this session's own
  `cna-devices` shard findings about `SystemTray`/`FileDialog`'s respective testability designs.

## Detailed Findings
None — this is a forward-looking design note with no implementation to audit; no claim within it
contradicts any confirmed fact from this session's independent audits of the existing `CNA::Devices`
classes it references for precedent.

## Cross-File Observations
Its comparison to `Microsoft::Devices::Sensors::SensorBase<T>`'s "honesty principle" (a consumer can
always cheaply ask "can I use this right now") is consistent with this session's own
`microsoft-devices`/`tests-microsoft-devices` shard findings that `SensorBase<T>`'s polling design is
"exceptionally mature."

## Missing or Weak Tests
N/A — no implementation exists yet to test; the document itself correctly identifies this
(`ICameraBackend`'s test-injection seam) as future work, not a current gap.

## Positive Findings
The explicit "why `Camera` is last, not first" framing (asynchronous permission state + poll-based
delivery + mandatory graphics-texture bridge, three genuinely different concerns combined) is a clear,
honest justification for treating this as its own design pass rather than force-fitting it into the
same shape as simpler `CNA::Devices` classes.

## Final Assessment
No findings. A well-grounded, appropriately-scoped design document with no implementation yet to
audit.
