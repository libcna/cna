# Audit: include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp` (111 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header (pure function + process-wide opt-out flag)
- XNA/FNA relevance: `Accelerometer`/`Gyroscope`-adjacent CNA convenience extension; not part of the strict XNA 4.0/WP7 contract (disclosed as such); FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Converts raw SDL accelerometer/gyroscope data (portrait device frame) to the XNA/WP7 landscape coordinate convention, for both landscape rotations a demo's non-resizable, wider-than-tall window can reach; provides a process-wide opt-out for this remap.

## Executive Verdict
Correct, and honestly self-critical: the file's own doc comment (lines 18-35) documents a real correction made to its own prior claim — a previous version stated the two-rotation-only behavior came from an `android:screenOrientation` manifest attribute, which a direct grep of the actual manifest disproved (no such attribute is set); the real mechanism is SDL's own runtime `SDLActivity.setOrientationBis()` given a non-resizable, wider-than-tall window and no `SDL_HINT_ORIENTATIONS` hint. The correction is scoped precisely: the *two-rotation-only* assumption itself was not found wrong, only the *reason* previously given for it — a good example of narrowing a correction to what was actually verified false, rather than discarding the whole claim.

## Checklist Results
- `SetAndroidLandscapeRemapEnabled()`/`IsAndroidLandscapeRemapEnabled()`'s own doc comment (lines 84-104) correctly cites two independent primary sources (an archived MSDN Magazine article on real WP7 `Accelerometer`/`Gyroscope` behavior, and SDL3's own header comment) both stating raw axes are never remapped for display orientation on real hardware — correctly framing `ConvertAndroidPortraitToXnaLandscape()` as a **CNA convenience deviation**, not part of the strict WP7 contract, with an opt-out for callers who want strict WP7-compatible raw axes.
- The two `AndroidSensorLandscapeOrientation` enum values are documented with the specific portrait-top-points-which-way relationship for each, giving a concrete, checkable physical description rather than a bare label.

## Detailed Findings
None.

## Cross-File Observations
`AndroidMotionBackend.cpp`'s `ApplyLandscapeRemapIfEnabled()` reuses this same function (audited separately) rather than reimplementing the sign logic — confirmed via that file's own read, avoiding a possible three-way drift across `Accelerometer.cpp`/`Gyroscope.cpp`/`Motion.cpp`'s independent call sites.

## Missing or Weak Tests
Not independently located in this pass; the function's design (explicit `AndroidSensorLandscapeOrientation` parameter rather than querying SDL directly) is specifically called out as enabling platform-independent unit testing.

## Positive Findings
The manifest-attribute correction is a genuine example of intellectual honesty in this codebase's own audit trail: distinguishing "the conclusion was right, the stated mechanism was wrong" from either "everything was fine" or "throw out the whole claim."

## Final Assessment
No findings.
