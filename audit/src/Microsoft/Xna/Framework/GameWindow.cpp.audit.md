# Audit: src/Microsoft/Xna/Framework/GameWindow.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/GameWindow.cpp`
- Audit status: AUDITED (full read, 415 lines)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/GameWindow.cs`,
  `src/FNAPlatform/FNAWindow.cs`, `src/FNAPlatform/SDL3_FNAPlatform.cs`
  (`ApplyWindowChanges` lines 471-575, `INTERNAL_HandleOrientationChange`/
  `INTERNAL_ConvertOrientation` lines 814-865, orientation-support gating lines 237-270)
- Main related tests: not independently located in this pass

## Purpose
Implements `GameWindow`'s SDL3-backed property accessors, the `BeginScreenDeviceChange`/
`EndScreenDeviceChange` protocol, and orientation/bounds/screen-name state refresh driven by direct
SDL3 queries (`SDL_GetWindowSize`, `SDL_GetDisplayForWindow`, `SDL_GetDisplayName`, etc.).

## Executive Verdict
Needs attention. Most of the file (property get/set pairs, `SetTitle`, `MinimizeEXT`/`RestoreEXT`,
event-raising) is correct and matches FNA's platform-layer behavior closely. Two real, undisclosed
deviations were confirmed by direct comparison against FNA's actual SDL3 platform implementation
(not just its abstract `GameWindow.cs` declaration): `EndScreenDeviceChange` never centers or
repositions the window onto the named display (a behavior FNA's own source explicitly comments is
"per XNA behavior", not merely FNA-internal plumbing), and the orientation model substitutes a
window-aspect-ratio heuristic for FNA's real SDL-display-orientation-event mechanism, applied
unconditionally on every platform rather than gated to mobile as in FNA.

## Checklist Results

### MEDIUM: `EndScreenDeviceChange` never centers or moves the window to the named display
Lines 167-207. FNA's real implementation, `SDL3_FNAPlatform.ApplyWindowChanges`
(`src/FNAPlatform/SDL3_FNAPlatform.cs` lines 471-575), resolves `screenDeviceName` to a display
index by matching it against `GraphicsAdapter.Adapters[i].DeviceName`, and — whenever the resize,
fullscreen, or display-name state changes — repositions the window with
`SDL_SetWindowPosition(sdlWindow, pos, pos)` using `SDL_WINDOWPOS_CENTERED_DISPLAY`-style
coordinates for the resolved display index. FNA's own comment on this call (line 535) states
plainly: "Window always gets centered on changes, per XNA behavior" — i.e. this is documented XNA
behavior, not an FNA-only implementation detail.

CNA's `EndScreenDeviceChange` (lines 167-207) only calls `SDL_SetWindowSize` and
`SDL_SetWindowFullscreen`; it stores `screenDeviceName` into `screenDeviceName_` as a plain string
(line 193) with **no lookup of which physical display that name corresponds to, and no
`SDL_SetWindowPosition` call anywhere in the function**. Concretely: calling
`window.EndScreenDeviceChange(anotherAdapter.DeviceName, 1280, 720)` to move a game window to a
second monitor — a normal, documented XNA usage the header's own `@param screenDeviceName` doc
comment (`GameWindow.hpp` line 158) describes — has no effect on window placement in CNA; the window
simply resizes in place on whichever display it already occupies. This also means the window is
never re-centered after an ordinary resize, unlike real XNA/FNA.

### MEDIUM: orientation tracking substitutes a window-aspect-ratio heuristic for FNA's real
SDL-display-orientation-event mechanism, applied on every platform
Lines 315-354 (`refreshCachedSDLState`) and 391-404 (`orientationFromBounds`). FNA's real mechanism
(`SDL3_FNAPlatform.cs` lines 814-865): orientation changes are driven by genuine
`SDL_EVENT_DISPLAY_ORIENTATION` events, converted via `INTERNAL_ConvertOrientation` from SDL's own
`SDL_DisplayOrientation` enum (`SDL_ORIENTATION_LANDSCAPE` -> `LandscapeLeft`,
`SDL_ORIENTATION_LANDSCAPE_FLIPPED` -> `LandscapeRight`, `SDL_ORIENTATION_PORTRAIT[_FLIPPED]` ->
`Portrait`), and this entire mechanism is gated behind `SupportsOrientationChanges()`, which itself
is hard-gated to iOS/Android only (`SupportsOrientations = OSVersion.Equals("iOS") || ...`, line
238). On desktop, FNA's `CurrentOrientation` is inert — it never changes after construction.

CNA's `orientationFromBounds` (lines 391-404) instead derives orientation purely from client-area
aspect ratio: `Height > Width` -> `Portrait`, otherwise -> `LandscapeLeft` (never `LandscapeRight`,
which is unreachable from this path since no SDL display-orientation query is ever consulted), and
`refreshCachedSDLState` (called from `setWindowInternal`, `updateFromSDL`, and
`EndScreenDeviceChange`) applies this heuristic **unconditionally on every platform**, including
desktop where FNA has no such behavior at all. Concretely: simply resizing a desktop game window to
be taller than it is wide fires `OrientationChanged` and flips `CurrentOrientation` to `Portrait` in
CNA — a purely invented behavior with no FNA/XNA counterpart, since real desktop XNA/FNA orientation
never changes from window shape.

A related, smaller consequence: because `SetSupportedOrientations` (`GameWindow.cpp` lines 248-271)
performs real cascading-fallback selection logic on top of this heuristic, whereas FNA's actual
`FNAWindow.SetSupportedOrientations` (`FNAWindow.cs` lines 159-174) is an intentional no-op on
desktop/FNA that only logs a warning ("we can't support that reliably across multiple mobile
platforms... this method is essentially a no-op"), CNA's version is doing real, non-trivial state
mutation where FNA deliberately does nothing.

### LOW: `EndScreenDeviceChange`'s resize-before-fullscreen-check ordering does not mirror FNA's guard
Lines 172-190. FNA's `ApplyWindowChanges` only resizes the window in the *not-going-fullscreen*
branch (`if (!wantsFullscreen) { ... SDL_SetWindowSize ... }`), and additionally special-cases a
still-hidden window by sizing it to the display mode instead when going fullscreen (lines 550-561).
CNA calls `SDL_SetWindowSize(window_, clientWidth, clientHeight)` unconditionally whenever
`clientWidth > 0 && clientHeight > 0`, regardless of whether `hasPendingScreenDeviceChange_`/
`pendingFullScreen_` requests fullscreen. In practice this is unlikely to be user-visible on SDL3
(fullscreen windows generally ignore an explicit windowed size while flagged fullscreen), but it is
a real, unremarked divergence from FNA's explicit branch structure and worth a one-line comment if
intentional.

### Positive: Android `SDL_SetWindowSize` exclusion is a reasonable, narrowly-scoped platform guard
Lines 176-181's `#ifndef __ANDROID__` around the `SDL_SetWindowSize` call is a sensible guard (fixed
native display size on Android makes an explicit resize meaningless/risky) and does not affect the
fullscreen or centering logic.

## Detailed Findings
1. **[MEDIUM] `EndScreenDeviceChange` never centers or repositions the window onto the named
   display** — lines 167-207, cf. FNA `SDL3_FNAPlatform.cs` lines 471-575 (explicit "per XNA
   behavior" comment at line 535).
2. **[MEDIUM] Orientation model uses an unconditional window-aspect-ratio heuristic instead of
   FNA's real, mobile-gated SDL-display-orientation-event mechanism** — lines 315-354, 391-404,
   248-271; cf. FNA `SDL3_FNAPlatform.cs` lines 237-270, 814-865, and `FNAWindow.cs` lines 159-174.
3. **[LOW] Resize call in `EndScreenDeviceChange` is not guarded by the windowed/fullscreen branch
   FNA uses** — lines 172-190, cf. FNA `SDL3_FNAPlatform.cs` lines 490-514, 547-566.

## Cross-File Observations
- `GraphicsDeviceManager.cpp` (not yet audited in this pass) is the expected caller of
  `EndScreenDeviceChange` with a real adapter `DeviceName` — worth confirming there whether the
  multi-monitor-placement gap (finding #1) is reachable from any currently-exposed CNA API, or
  whether `GraphicsDeviceManager` never actually passes a differing display name in practice.
- The orientation heuristic (finding #2) interacts with `TouchPanel`/`PresentationParameters`'s own
  `DisplayOrientation` fields in real FNA (`GraphicsDeviceManager.cs`,
  `Input/Touch/TouchPanel.cs`) — worth checking when those files are audited whether CNA propagates
  the heuristic-derived orientation to the same downstream consumers FNA does, which would spread
  this deviation's effects further than `GameWindow` alone.

## Missing or Weak Tests
Not independently located in this pass. Tests worth prioritizing when the `tests-*` shard for this
area is audited: (a) `EndScreenDeviceChange` with a `screenDeviceName` naming a different display
than the window currently occupies, asserting window placement; (b) resizing a desktop-simulated
window narrow/tall and asserting whether `OrientationChanged` firing is intended behavior.

## Positive Findings
The bulk of the file — property accessors, `SetTitle`, `MinimizeEXT`/`RestoreEXT`,
`queryClientBoundsFromSDL`/`queryScreenDeviceNameFromSDL`'s null-window and null-name fallbacks, and
all `SDL_*` error paths correctly wrapped via `makeSdlError` — is correct, defensively written, and
matches FNA's platform-layer behavior closely.

## Final Assessment
Two MEDIUM findings (no display-targeted window placement in `EndScreenDeviceChange`; an
unconditional, undisclosed aspect-ratio orientation heuristic replacing FNA's real mobile-gated
mechanism) and one LOW finding (resize-call branch ordering). Both MEDIUM findings should be added
to `AUDIT_CROSS_CUTTING_FINDINGS.md` as they represent behavior XNA/FNA explicitly documents as
intentional (window centering, mobile-only orientation) that CNA silently does differently.
