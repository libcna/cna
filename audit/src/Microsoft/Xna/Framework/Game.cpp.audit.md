# Audit: src/Microsoft/Xna/Framework/Game.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Game.cpp`
- Audit status: AUDITED (full read, 1006 lines)
- Subsystem: `xna-framework-core` shard (last file of the shard — 78/78 complete)
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Game.cs` (1003 lines, read in full),
  especially `Initialize()` (lines 623-663), `Dispose(bool)` (lines 306-345), `Tick()` (lines
  423-520+), `DoInitialize()`/`CategorizeComponent()`/`SortUpdateable()`/`SortDrawable()` (lines
  760-842); and `src/FNAPlatform/SDL3_FNAPlatform.cs` lines 900-1220 (the real SDL event loop)
- Main related tests: not independently located in this pass

## Purpose
Implements `Game`'s construction/disposal, the fixed/variable timestep loop (`Tick`, `AdvanceElapsedTime`,
`UpdateEstimatedSleepPrecision`), component categorization/sorting, SDL event polling, and the
`Initialize`/`Draw`/`Update` default bodies.

## Executive Verdict
Needs attention despite a generally strong port. The timestep loop (`Tick()`,
`AdvanceElapsedTime()`, `UpdateEstimatedSleepPrecision()`) and component lifecycle
(`DoInitialize()`, `CategorizeComponent()`, `SortUpdateable()`/`SortDrawable()`,
`OnComponentAdded`/`OnComponentRemoved`) were verified line-for-line equivalent to FNA's real
`Game.cs`. However, `Initialize()` (lines 513-529) omits FNA's `DeviceDisposing +=
UnloadContent` wiring entirely, making `UnloadContent()` permanently dead code with no framework
call site anywhere — confirmed by a whole-file grep. `PollEvents()` (lines 887-940) also omits
four real FNA SDL3 event reactions with concrete, observable consequences. Two smaller,
established-pattern findings round out the report: `Dispose(bool)` omits a global-registry reset
FNA performs, and `AssertNotDisposed()` is the third instance in this shard of the same
exception-type-inconsistency pattern already flagged twice elsewhere this session.

## Checklist Results

### HIGH: `UnloadContent()` is never invoked anywhere — a dead virtual lifecycle hook
FNA's real `Initialize()` (`Game.cs` lines 649-662):
```csharp
graphicsDeviceService = (IGraphicsDeviceService) Services.GetService(typeof(IGraphicsDeviceService));
if (graphicsDeviceService != null)
{
    graphicsDeviceService.DeviceDisposing += (o, e) => UnloadContent();
    if (graphicsDeviceService.GraphicsDevice != null)
    {
        LoadContent();
    }
    else
    {
        graphicsDeviceService.DeviceCreated += (o, e) => LoadContent();
    }
}
```
CNA's `Initialize()` (lines 513-529):
```cpp
graphicsDeviceService_ = Services_.GetService<Graphics::IGraphicsDeviceService>();
if (graphicsDeviceService_ == nullptr || graphicsDeviceService_->getGraphicsDeviceProperty() != nullptr)
{
    LoadContent();
}
```
Two gaps relative to FNA, of very different severity:

1. **No `DeviceDisposing → UnloadContent` subscription at all.** A repository-wide grep for
   `UnloadContent` finds exactly two hits: the header declaration (`Game.hpp` line 253) and this
   file's own empty default body (line 509-511) — **no call site anywhere**. Since
   `GraphicsDeviceManager::Dispose(bool)` (audited separately) genuinely raises `DeviceDisposing`
   before releasing its owned `GraphicsDevice`, FNA guarantees any game overriding `UnloadContent()`
   gets a chance to release GPU-dependent resources at that point. In CNA, that override is simply
   never called by the framework, under any circumstance (device disposal, `Game::Dispose()`, or
   otherwise) — a confirmed, unconditional regression against the documented XNA lifecycle
   contract, not merely a narrow edge case.

2. **No `DeviceCreated → LoadContent` fallback subscription for the not-yet-created-device case.**
   CNA's condition calls `LoadContent()` immediately whenever the service is absent *or* the device
   already exists, matching FNA's immediate-call branch; but when the service exists and the device
   does not yet exist, FNA guarantees `LoadContent()` still eventually fires once the device is
   created, while CNA's code has no equivalent fallback — `LoadContent()` would never fire in that
   scenario. This branch is likely unreachable in CNA's actual architecture today (elsewhere
   documented: `Game` always eagerly owns a pre-constructed `GraphicsDevice_` before any
   `GraphicsDeviceManager` exists — see `GraphicsDeviceManager.cpp`'s own D9-103 comment), so this
   half of the finding is lower-priority than the `UnloadContent` gap, but is a real latent gap if
   that architectural invariant ever changes.

**Fix shape**: in `Initialize()`, after resolving `graphicsDeviceService_`, subscribe
`graphicsDeviceService_->getDeviceDisposingEvent() += [this](...) { UnloadContent(); };` (and,
defensively, a `DeviceCreated → LoadContent` fallback for the deferred-device branch) mirroring
FNA exactly.

### MEDIUM: `PollEvents()` omits several real FNA SDL3 event reactions
Lines 887-940 handle `SDL_EVENT_QUIT`, `KEY_DOWN` (F9/F10 debug context-loss simulation),
`WINDOW_RESIZED`/`PIXEL_SIZE_CHANGED`, `WILL_ENTER_BACKGROUND`/`DID_ENTER_FOREGROUND`, and
`WINDOW_FOCUS_LOST`/`GAINED`. Comparing against FNA's real SDL3 event loop
(`SDL3_FNAPlatform.cs` lines 900-1220) finds four handled cases with real consequences that CNA's
loop has no equivalent for at all (confirmed absent via a repository-wide grep for the relevant
SDL event names and behaviors):
1. **`SDL_EVENT_WINDOW_MOVED`** (FNA lines 1071-1087): when the window moves to a different
   display, FNA detects the adapter change and calls `game.GraphicsDevice.Reset(...)` with the new
   adapter — real multi-monitor support that CNA's loop has no equivalent for.
2. **`SDL_EVENT_WINDOW_EXPOSED`** (FNA lines 1064-1068): FNA calls `game.RedrawWindow()` — used to
   keep the game rendering during a blocking window-resize drag on platforms where that would
   otherwise freeze the last frame. CNA has no `RedrawWindow` equivalent anywhere (grep-confirmed).
3. **`SDL_EVENT_WINDOW_ENTER_FULLSCREEN`/`LEAVE_FULLSCREEN`** (FNA lines 1100-1114): FNA syncs
   `GraphicsDeviceManager.IsFullScreen` back to `true`/`false` when fullscreen is toggled by the OS
   /window manager outside the app's own request. CNA's `GraphicsDeviceManager::isFullScreen_` has
   no such synchronization path.
4. **`SDL_EVENT_WINDOW_MOUSE_ENTER`/`MOUSE_LEAVE`** (FNA lines 1090-1097): FNA calls
   `SDL_DisableScreenSaver()`/`SDL_EnableScreenSaver()` on mouse enter/leave. CNA calls
   `SDL_DisableScreenSaver()` unconditionally elsewhere (`GamerServices/Guide.cpp` line 345, an
   unrelated call site) but has no mouse-enter/leave-driven toggle.

Additionally, and consistent with (not a new instance of) the previously-confirmed
`GameWindow`/`GraphicsDeviceManager` orientation finding: `PollEvents()` also has no case for
`SDL_EVENT_DISPLAY_ORIENTATION` at all (FNA lines 1125-1136), reinforcing that CNA's orientation
model has no real SDL-orientation-event pathway anywhere in the Game loop, not just in
`GameWindow`'s own internals.

### LOW: `Dispose(bool disposing)` omits FNA's `ContentTypeReaderManager.ClearTypeCreators()` reset
Lines 599-634 vs. FNA `Game.cs` line 338 (`ContentTypeReaderManager.ClearTypeCreators();`, inside
the `if (disposing)` block). CNA's `Content::ContentTypeReaderManager` is confirmed to exist with
an equivalent `ClearTypeCreators()` static method (referenced by
`include/CNA/Internal/Xnb/XnbBuiltInReaders.hpp`'s own comments, which note tests call it in
`SetUp()` to reset global registry state). `Game::Dispose(bool)` never calls it, meaning a second
`Game` constructed later in the same process could observe residual custom type-reader
registrations from a previously-disposed `Game` — a minor but real state-leak relative to FNA's
explicit cleanup.

### LOW (third instance of a recurring pattern this session): `AssertNotDisposed()` uses `std::runtime_error` instead of `System::ObjectDisposedException`
Line 640: `throw std::runtime_error("The Game object was used after being disposed.");` vs. FNA's
`throw new ObjectDisposedException(name, ...)` (`Game.cs` lines 353-359). `System::ObjectDisposedException`
is confirmed present in sharp-runtime and already used by 28 other CNA files. This is the **third**
occurrence in this same `xna-framework-core` shard of the identical pattern (a project-provided,
widely-used sharp-runtime exception type available but not used, in favor of a raw `std::`
exception) — see `GameComponentCollection.hpp.audit.md` (`System::NotSupportedException`) and
`GraphicsDeviceManager.cpp.audit.md` (`System::ArgumentNullException`/`ArgumentException`) for the
other two. Given three independent confirmations within one shard, this is worth recording as a
standing recurring pattern in the cross-cutting findings doc rather than three unrelated one-off
notes.

## Detailed Findings
1. **[HIGH] `UnloadContent()` never invoked by the framework — no `DeviceDisposing` subscription**
   — `Initialize()`, lines 513-529; cf. FNA `Game.cs` lines 649-662.
2. **[MEDIUM] `PollEvents()` omits `WINDOW_MOVED` (multi-monitor reset), `WINDOW_EXPOSED`
   (resize-drag redraw), `ENTER/LEAVE_FULLSCREEN` (fullscreen state sync-back), and
   `MOUSE_ENTER/LEAVE` (screensaver toggle)** — lines 887-940; cf. FNA `SDL3_FNAPlatform.cs` lines
   1064-1114.
3. **[LOW] `Dispose(bool)` omits `ContentTypeReaderManager::ClearTypeCreators()`** — lines 599-634;
   cf. FNA `Game.cs` line 338.
4. **[LOW] `AssertNotDisposed()` uses `std::runtime_error` instead of
   `System::ObjectDisposedException`** — line 640; third instance of a recurring pattern this
   session.

## Cross-File Observations
- `setContentProperty()` (line 167-170) does `Content_ = value;`, a copy-assignment of a whole
  `Content::ContentManager` — flagged for confirmation when that class's own copy-assignment
  operator is audited under the `xna-content` shard (not yet reached).
- The `UnloadContent()` gap (finding #1) and `GraphicsDeviceManager`'s own confirmed
  event-forwarding gap (see `GraphicsDeviceManager.cpp.audit.md`) compound each other: even if
  `GraphicsDeviceManager` were fixed to forward `GraphicsDevice`'s real `DeviceDisposing` event
  correctly, `Game::Initialize()` still wouldn't be listening for it. Both fixes are independently
  necessary for the full FNA-equivalent content-reload lifecycle to work.
- Confirms the previously-flagged `GameWindow`/`GraphicsDeviceManager` orientation-model gap
  extends to the Game loop itself: no `SDL_EVENT_DISPLAY_ORIENTATION` handling anywhere in
  `PollEvents()`.

## Missing or Weak Tests
Not independently located in this pass. Priority tests for the `tests-*` shard: (a) subclass
`Game`, override `UnloadContent()` with an observable side effect, trigger a device
disposal/reset, and assert it fires; (b) construct two `Game` instances sequentially in one
process with a custom registered type-reader on the first, and assert the second doesn't inherit
it.

## Positive Findings
`Tick()`, `AdvanceElapsedTime()`, `UpdateEstimatedSleepPrecision()`, `DoInitialize()`,
`CategorizeComponent()`, `SortUpdateable()`/`SortDrawable()`, and `OnComponentAdded`/
`OnComponentRemoved`/`OnUpdateOrderChanged`/`OnDrawOrderChanged` were all verified line-for-line
equivalent to FNA's real `Game.cs`, including the sleep-precision-estimation algorithm's exact
constants (`4.0` ms upper bound, `PREVIOUS_SLEEP_TIME_COUNT = 128`). The Alt-Tab-vs-mobile
background/foreground event handling (lines 923-930) is explicitly and correctly disclosed via an
inline comment citing the exact FNA source lines it mirrors and a project decision doc
(`docs/input-fna-fidelity.md` DEC-15) — exactly the right way to document a verified-intentional
choice. The Emscripten main-loop adaptation (lines 750-814) is cleanly isolated and doesn't
interfere with the non-Emscripten code path.

## Final Assessment
One HIGH finding (dead `UnloadContent()` hook — a real, confirmed regression against XNA's
documented content-reload lifecycle contract) and one MEDIUM finding (four omitted FNA SDL event
reactions with concrete consequences), plus two LOW findings (one a state-leak, one the third
instance of the recurring exception-type-inconsistency pattern). Recommend adding the HIGH finding
to `AUDIT_CROSS_CUTTING_FINDINGS.md` given its directly game-visible consequence, and consolidating
the three-instance exception-type pattern into its own cross-cutting entry.
