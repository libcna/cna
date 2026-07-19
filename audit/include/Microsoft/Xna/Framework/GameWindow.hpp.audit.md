# Audit: include/Microsoft/Xna/Framework/GameWindow.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GameWindow.hpp`
- Audit status: AUDITED (full read, 228 lines, header-only declarations)
- Subsystem: `xna-framework-core` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type (`Microsoft.Xna.Framework.GameWindow`); FNA reference:
  `src/GameWindow.cs` (abstract base) + `src/FNAPlatform/FNAWindow.cs` (concrete SDL-backed
  subclass) + `src/FNAPlatform/SDL3_FNAPlatform.cs` (`ApplyWindowChanges`,
  `INTERNAL_HandleOrientationChange`, `INTERNAL_ConvertOrientation`)
- Main related tests: not independently located in this pass

## Purpose
Declares `GameWindow`, the XNA-facing wrapper around the platform window: title, client bounds,
resizing/borderless flags, display orientation, and the `BeginScreenDeviceChange`/
`EndScreenDeviceChange` fullscreen-and-display-switch protocol used by `GraphicsDeviceManager`.

## Executive Verdict
Needs attention. The header explicitly and correctly discloses one architectural deviation (FNA's
abstract-base-plus-per-platform-subclass hierarchy collapsed into one concrete SDL-backed class —
reasonable, and disclosed in the class doc comment at lines 23-28). However, cross-checking the
declared contract of `EndScreenDeviceChange` and `SetSupportedOrientations` against FNA's actual
platform implementation (not just its abstract declaration) surfaces two further, **undisclosed**
behavioral deviations, detailed in `GameWindow.cpp.audit.md` where the implementation lives: (1) the
orientation model is driven by window-aspect-ratio heuristics on every platform instead of real SDL
display-orientation events gated to mobile, and (2) `EndScreenDeviceChange` never centers the window
or moves it to the display named by `screenDeviceName`, despite this header's own doc comment
(line 158: "The name of the display adapter") describing exactly that contract.

## Checklist Results

### Positive: abstract-hierarchy collapse is disclosed
Lines 23-28 explicitly state: "FNA defines GameWindow as abstract with per-platform subclasses; CNA
collapses that hierarchy into one concrete SDL-backed class." This matches the project's own
convention of documenting intentional deviations rather than silently diverging.

### MEDIUM: `EndScreenDeviceChange`'s doc comment promises adapter-targeted placement the implementation does not perform
Line 158's `@param screenDeviceName` doc ("The name of the display adapter") describes the same
contract as FNA's real `screenDeviceName` parameter, which `SDL3_FNAPlatform.ApplyWindowChanges`
uses to resolve a target display index and re-center the window on it via
`SDL_SetWindowPosition(..., SDL_WINDOWPOS_CENTERED_DISPLAY)`. See `GameWindow.cpp.audit.md` for the
implementation-side confirmation that no such placement occurs — this header's own doc comment is
consequently misleading about what the method actually does.

### LOW: orientation-related declarations give no hint of the aspect-ratio-heuristic model
`getCurrentOrientationProperty()` (lines 79-83) and `SetSupportedOrientations()` (lines 195-199) are
documented as ordinary property/setter semantics with no indication that, unlike FNA (where
orientation tracking is inert on desktop and driven by genuine SDL display-orientation events on
mobile), CNA derives orientation from client-window aspect ratio on every platform including
desktop. See `GameWindow.cpp.audit.md` for the concrete mechanism.

## Detailed Findings
1. **[MEDIUM] Doc comment promises adapter-targeted window placement the implementation omits** —
   line 158; implementation-side gap detailed in `src/Microsoft/Xna/Framework/GameWindow.cpp.audit.md`.
2. **[LOW] No disclosure of the aspect-ratio orientation heuristic** — lines 79-83, 195-199;
   mechanism detailed in the `.cpp` report.

## Cross-File Observations
- `friend class Game` / `friend class GraphicsDeviceManager` (lines 31-32) is a reasonable
  encapsulation choice for `setWindowInternal`/`refreshCachedSDLState`-style internals; to be
  cross-checked against actual call sites when `Game.cpp`/`GraphicsDeviceManager.cpp` are audited.
- `GetNativeSdlWindowEXT()` (lines 91-102) is correctly `NOXNA`-tagged and its doc comment correctly
  scopes its intended callers (`CNA::Devices::DisplayInfo`) away from the strict XNA API surface.

## Missing or Weak Tests
Not independently located in this pass; a test asserting `EndScreenDeviceChange` actually repositions
the window when given a differing `screenDeviceName` (or documents that it intentionally does not,
if that is accepted as a permanent NOXNA simplification) would directly catch finding #1.

## Positive Findings
The disclosed abstract-to-concrete collapse (lines 23-28) is exactly the right way to document an
architectural deviation from FNA — this is a good model other files' `NOXNA`/deviation comments
should follow.

## Final Assessment
One MEDIUM finding (doc/implementation mismatch on adapter-targeted placement) and one LOW finding
(undisclosed orientation heuristic) — both detailed further in the paired `.cpp` audit report.
