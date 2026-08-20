# Audit: src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp`
- Audit status: AUDITED (full read, 587 lines)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/GraphicsDeviceManager.cs` (585 lines,
  read in full for this audit), especially `ApplyChanges()` (lines 270-328),
  `IGraphicsDeviceManager.CreateDevice()` (lines 515-561), and
  `INTERNAL_CreateGraphicsDeviceInformation()` (lines 423-509)
- Main related tests: not independently located in this pass

## Purpose
Implements device creation/reset orchestration: builds a `GraphicsDeviceInformation` from current
preferences, notifies the game window of pending fullscreen/size/display changes, raises the
device-lifecycle events, and applies the result to the owned `Graphics::GraphicsDevice`.

## Executive Verdict
Needs attention. `INTERNAL_CreateGraphicsDeviceInformation()`'s back-buffer-orientation-swap logic
(lines 448-504) is verified line-for-line equivalent to FNA's own (`GraphicsDeviceManager.cs` lines
423-509), including the `supportsOrientations_` gate correctly restricted to iOS/Android
(`platformSupportsOrientations()`, lines 17-32, explicitly comments this matches FNA's own
`SDL3_FNAPlatform.SupportsOrientations` gating). However, a confirmed HIGH-severity gap exists in how
`DeviceResetting`/`DeviceReset` are raised: this class raises its own copies of these events directly
from `ApplyChanges()`/`CreateDevice()` (lines 240, 244, 310) instead of subscribing to
`Graphics::GraphicsDevice`'s identically-named events the way FNA's real
`IGraphicsDeviceManager.CreateDevice()` does (`graphicsDevice.DeviceResetting += OnDeviceResetting;
graphicsDevice.DeviceReset += OnDeviceReset;`, FNA `GraphicsDeviceManager.cs` lines 556-557) — and
`Graphics::GraphicsDevice.cpp` has a confirmed, real, independent code path
(`createBackend()`'s `deviceEventCallback`, lines 1459-1478) that raises `GraphicsDevice`'s own
`DeviceResetting`/`DeviceReset` for a genuine backend-detected device-lost recovery, completely
outside any `GraphicsDeviceManager::ApplyChanges()`/`CreateDevice()` call.

## Checklist Results

### HIGH: `GraphicsDeviceManager` never forwards `GraphicsDevice`'s own device-lost/reset events; a real backend-triggered reset silently bypasses `IGraphicsDeviceService` listeners
FNA's real `IGraphicsDeviceManager.CreateDevice()` (`GraphicsDeviceManager.cs` lines 550-557) wires
its own `OnDeviceDisposing`/`OnDeviceResetting`/`OnDeviceReset` handlers directly onto the freshly
created `GraphicsDevice`'s own `Disposing`/`DeviceResetting`/`DeviceReset` events:
```csharp
graphicsDevice.Disposing += OnDeviceDisposing;
graphicsDevice.DeviceResetting += OnDeviceResetting;
graphicsDevice.DeviceReset += OnDeviceReset;
```
This means **any** reset of the underlying `GraphicsDevice` — whether triggered through
`GraphicsDeviceManager.ApplyChanges()`, or through a game calling `GraphicsDevice.Reset(...)`
directly (a public XNA API a game is free to call), or through a platform-driven device-lost
recovery — is forwarded to `GraphicsDeviceManager`'s own public `DeviceResetting`/`DeviceReset`
events, which is the surface most `IGraphicsDeviceService`-consuming code (e.g. content-reload
logic) actually subscribes to.

CNA's `GraphicsDeviceManager` never performs this subscription anywhere in this file — grepping the
whole file for `+=` finds exactly one subscription, to `game_->getWindowProperty().ClientSizeChanged`
(lines 523-527); there is no `graphicsDevice_->DeviceReset +=` / `DeviceResetting +=` anywhere.
Instead, `ApplyChanges()` (lines 240, 244) and `CreateDevice()` (implicitly, via
`applyToExistingBackend()` at line 309 followed by `OnDeviceCreated` at line 310) raise this class's
own `DeviceResetting`/`DeviceReset` directly around their own call into `applyToExistingBackend()`,
which itself calls `graphicsDevice_->Reset(pp, adapter)` (line 582) — a `Graphics::GraphicsDevice`
method independently confirmed (`GraphicsDevice.cpp` lines 391, 438) to raise `GraphicsDevice`'s
*own*, separate `DeviceResetting`/`DeviceReset` events every time it runs.

This duplication "happens to work" for the one path both classes agree on (a preference change
routed through `GraphicsDeviceManager.ApplyChanges()`), since each class fires its own copy of the
event independently and a listener normally only subscribes to one class's version. But it breaks
down completely for resets that do **not** go through `GraphicsDeviceManager`: `GraphicsDevice.cpp`
lines 1444-1478 (`createBackend()`) installs a real `deviceEventCallback` that a graphics backend
invokes for a genuine, backend-detected device-lost condition:
```cpp
case CNA::Internal::Backends::BackendDeviceEvent::Resetting:
    deviceStatus_ = GraphicsDeviceStatus::NotReset;
    DeviceResetting.Raise(this, System::EventArgs::Empty);
    break;
case CNA::Internal::Backends::BackendDeviceEvent::Reset:
    deviceStatus_ = GraphicsDeviceStatus::Normal;
    DeviceReset.Raise(this, System::EventArgs::Empty);
    break;
```
(this is explicitly the D3D9-class device-lost/reset scenario the surrounding comment cites: "plans/plan_dx9.md
D9-34: forward a REAL, backend-detected device-lost/reset event to this GraphicsDevice's own public
XNA events. Nine of the ten backends never call this.") When this callback fires,
`GraphicsDevice::DeviceResetting`/`DeviceReset` raise correctly — but because
`GraphicsDeviceManager` never subscribed to them, `GraphicsDeviceManager::DeviceResetting`/
`DeviceReset` (the events exposed via `IGraphicsDeviceService`, which is the conventional place
resource-reload code hooks into per XNA convention) **never fire**. Concretely: on the one backend
that implements real device-lost detection (per the cited comment, one of ten), a genuine D3D9
device-lost-then-recovered cycle (e.g. an alt-tab or display-mode change on Windows) would silently
fail to notify any `IGraphicsDeviceService.DeviceReset` subscriber, even though the underlying
`GraphicsDevice` itself correctly tracked and raised the event.

**Fix shape**: in `CreateDevice()` (and/or the constructor), subscribe
`graphicsDevice_->DeviceResetting += [this](...) { OnDeviceResetting(this, ...); };` and the same for
`DeviceReset`/`Disposing`, mirroring FNA's wiring, then remove the manual `OnDeviceResetting`/
`OnDeviceReset` calls from `ApplyChanges()`/`CreateDevice()` so the event fires exactly once per real
reset regardless of what triggered it.

### Positive: `INTERNAL_CreateGraphicsDeviceInformation`'s orientation-swap logic matches FNA exactly
Lines 448-504 vs. FNA `GraphicsDeviceManager.cs` lines 423-509: the `useResizedBackBuffer_` early-out,
the `supportsOrientations_`-gated min/max width/height swap for `Portrait`, and the
`preferMultiSampling_`/`MultiSampleCount` defaulting (capped at 8, with CNA's own comment correctly
noting the lack of an `FNA3D_GetMaxMultiSampleCount`-equivalent query) are all faithful ports. The
`platformSupportsOrientations()` helper (lines 17-32) correctly restricts orientation support to
iOS/Android with an explicit citation of the FNA behavior it mirrors — this is a **positive
counter-example** to the undisclosed orientation-heuristic problem found in `GameWindow.cpp`
(`refreshCachedSDLState`/`orientationFromBounds`), since this file demonstrates the project already
understands and correctly implements the real FNA platform gate elsewhere in the same subsystem. This
strengthens that earlier finding: `GameWindow.cpp`'s unconditional heuristic isn't just a deviation
from FNA, it's an internal inconsistency with this sibling file's own, correct, desktop-excluded
orientation policy.

### Positive: reachability of `EndScreenDeviceChange`'s adapter-name argument confirmed
Lines 233-237 and 302-306 confirm `EndScreenDeviceChange` is genuinely called with
`gdi.getAdapterProperty()->getDeviceNameProperty()` — i.e. the real adapter device name, exactly as
FNA does (`GraphicsDeviceManager.cs` lines 314-318, 541-545). This confirms the
`GameWindow::EndScreenDeviceChange` finding (no display-targeted window placement, see
`GameWindow.cpp.audit.md`) is reachable through this class's normal `ApplyChanges()`/`CreateDevice()`
paths, not merely a theoretical gap in an unused parameter.

### Positive: constructor's documented no-`ApplyChanges()` deviation is well-reasoned and self-consistent
Lines 59-84's comment explains precisely why `ApplyChanges()` is deliberately not called from the
`Game*`-taking constructor (CNA's `Game` always pre-owns its `GraphicsDevice`, unlike FNA, so calling
it here would double-reconfigure the backend), and directly cites FNA's own `ApplyChanges()` comment
warning against exactly that mistake. This is exactly the right way to document an intentional
behavioral difference.

### LOW: constructor's null-game guard uses `std::invalid_argument` instead of `System::ArgumentNullException`
Line 64. See `GraphicsDeviceManager.hpp.audit.md` for the full cross-file convention comparison; FNA
throws `ArgumentNullException` for this exact case (`GraphicsDeviceManager.cs` line 194).

### LOW: `registerServices()`'s already-registered guard uses `std::invalid_argument` instead of `System::ArgumentException`
Line 515-516: `throw std::invalid_argument("A GraphicsDeviceManager is already registered with this
Game.")`. FNA throws `ArgumentException("Graphics Device Manager Already Present")` for the identical
case (`GraphicsDeviceManager.cs` lines 211-214), and `System::ArgumentException` is already used
elsewhere in this exact directory (`BoundingBox.cpp` line 465) — the same inconsistency pattern as
the null-game guard above, just the sibling exception type.

## Detailed Findings
1. **[HIGH] `GraphicsDeviceManager` never subscribes to `GraphicsDevice`'s own
   `DeviceResetting`/`DeviceReset`/`Disposing` events; a real backend-detected device-lost recovery
   bypasses `IGraphicsDeviceService` listeners entirely** — lines 240, 244, 310 (this class's manual
   raises) vs. `GraphicsDevice.cpp` lines 1459-1478 (the independent backend-callback path) and FNA
   `GraphicsDeviceManager.cs` lines 556-557 (the forwarding wiring this port omits).
2. **[LOW] Null-game constructor guard uses `std::invalid_argument`, not
   `System::ArgumentNullException`** — line 64.
3. **[LOW] Already-registered guard uses `std::invalid_argument`, not `System::ArgumentException`** —
   lines 515-516.

## Cross-File Observations
- Confirms `GameWindow::EndScreenDeviceChange`'s adapter-name parameter is genuinely reachable with a
  real device name from this class's normal operation (see above) — strengthens that finding from
  theoretical to concretely reachable.
- Confirms `GameWindow.cpp`'s unconditional orientation heuristic is inconsistent with this sibling
  file's own correct, desktop-excluded `platformSupportsOrientations()` gate — worth citing both
  together in the cross-cutting findings doc as one project-internal inconsistency, not two
  unrelated FNA deviations.
- `Graphics::GraphicsDevice`'s `deviceEventCallback` (device-lost forwarding) is cited by its own
  comment as implemented by only one of ten backends (plans/plan_dx9.md D9-34) — worth flagging again when
  that specific backend and `GraphicsDevice.cpp` are formally audited under Task #4's
  `xna-graphics` shard, to confirm which backend and cross-reference this finding there.

## Missing or Weak Tests
Not independently located in this pass. A test creating a `GraphicsDeviceManager`, then calling
`graphicsDevice->Reset(...)` directly (bypassing the manager) and asserting whether
`GraphicsDeviceManager::DeviceReset` fires, would directly and unambiguously confirm finding #1.

## Positive Findings
`INTERNAL_CreateGraphicsDeviceInformation`'s orientation/multisample/format logic is a faithful,
well-verified port of FNA's equivalent. The constructor's documented deviation from calling
`ApplyChanges()` is a model example of how to disclose an intentional behavioral difference.

## Final Assessment
One HIGH finding (missing `GraphicsDevice`-event forwarding, with a confirmed concrete failure
scenario via the D3D9-class device-lost callback path) and two LOW findings (exception-type
inconsistencies). Recommend adding the HIGH finding to `AUDIT_CROSS_CUTTING_FINDINGS.md` given its
game-visible consequence (silently broken automatic resource-reload notification after a real
device-lost recovery).
