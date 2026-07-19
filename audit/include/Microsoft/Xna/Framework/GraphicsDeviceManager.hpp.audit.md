# Audit: include/Microsoft/Xna/Framework/GraphicsDeviceManager.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GraphicsDeviceManager.hpp`
- Audit status: AUDITED (full read, 361 lines, header-only declarations)
- Subsystem: `xna-framework-core` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type (`Microsoft.Xna.Framework.GraphicsDeviceManager`); FNA
  reference: `src/GraphicsDeviceManager.cs` (585 lines)
- Main related tests: not independently located in this pass

## Purpose
Declares `GraphicsDeviceManager`: the XNA device-configuration/lifecycle manager exposing
`IGraphicsDeviceService`/`IGraphicsDeviceManager`, preference properties (back buffer
size/format, fullscreen, multisampling, vsync, orientation), `ApplyChanges()`/`CreateDevice()`,
and the `DeviceCreated`/`DeviceDisposing`/`DeviceReset`/`DeviceResetting`/`PreparingDeviceSettings`
event surface.

## Executive Verdict
Needs attention. The property/event declarations match FNA's public contract well, and several
inline comments elsewhere in this class's paired `.cpp` correctly disclose intentional deviations
(see that report). However, comparing this class's actual reset-event wiring against FNA's real
`GraphicsDeviceManager.cs` uncovers a genuine, confirmed HIGH-severity event-forwarding gap:
`GraphicsDeviceManager` never subscribes to its own `GraphicsDevice`'s `DeviceResetting`/
`DeviceReset` events (as FNA does explicitly), so a device-lost-then-reset cycle triggered from
inside the backend itself (confirmed to exist and reachable, see `GraphicsDeviceManager.cpp.audit.md`)
never reaches any code listening on `GraphicsDeviceManager`'s public `DeviceReset`/`DeviceResetting`
events — the conventional `IGraphicsDeviceService` surface most resource-reload code subscribes to.

## Checklist Results

### HIGH: no wiring from `GraphicsDevice`'s own reset events to this class's events (see .cpp for full analysis)
The header declares `DeviceReset`/`DeviceResetting` (lines 69-73) as ordinary
`System::EventHandler<System::EventArgs>` members with doc comments ("Raised after the graphics
device has reset" / "Raised before the graphics device resets") that give no indication these are
only raised from `ApplyChanges()`/`CreateDevice()`'s own call sites and never forwarded from
`Graphics::GraphicsDevice`'s identically-named events — a real, confirmed divergence detailed fully
in the paired `.cpp` report, since the wiring (or lack of it) lives entirely in the implementation.

### LOW: constructor null-check does not use the project's established `System::ArgumentNullException`
Line 85's `explicit GraphicsDeviceManager(Game* game)` doc comment says "must not be null" but gives
no hint of which exception type is thrown; the `.cpp` implementation (see paired report) throws
`std::invalid_argument` instead of `System::ArgumentNullException`, which is already the established
convention for this exact null-game-argument pattern across at least 10 other files in this codebase
(`Audio/WaveBank.cpp`, `Audio/Cue.cpp`, `Audio/SoundBank.cpp`, `Audio/AudioEngine.cpp`,
`Audio/SoundEffectInstance.cpp`, `GamerServices/AvatarRenderer.cpp`,
`GamerServices/AvatarDescription.cpp`, `Net/NetworkSession.cpp`, `Media/MediaLibrary.cpp`, etc.), and
matches FNA's own `ArgumentNullException` throw for this exact case (`GraphicsDeviceManager.cs` line
194: `throw new ArgumentNullException("The game cannot be null!");`).

## Detailed Findings
1. **[HIGH] No forwarding from `GraphicsDevice`'s own `DeviceResetting`/`DeviceReset` events** —
   declared lines 69-73; full analysis and concrete failure scenario in
   `src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp.audit.md`.
2. **[LOW] Constructor's null-game guard should use `System::ArgumentNullException`, not
   `std::invalid_argument`** — declared line 85; cf. FNA `GraphicsDeviceManager.cs` line 194 and
   this codebase's own established convention in 10+ other files.

## Cross-File Observations
- `Graphics::IGraphicsDeviceService` (base interface, audited separately) declares the
  `getDeviceCreatedEvent()`/etc. accessor methods this class implements (lines 108-130) — the
  accessor-based indirection itself is a reasonable NOXNA C++ adaptation of the C# interface's
  event redeclaration pattern.
- `PresentationMode` (lines 34-46, `NOXNA`) is a CNA-original scaling-policy enum correctly tagged
  and correctly kept out of the XNA-facing property surface's semantics (it's an additive property,
  not a replacement for any real XNA member).

## Missing or Weak Tests
Not independently located in this pass. A test that resets `Graphics::GraphicsDevice` directly
(bypassing `GraphicsDeviceManager::ApplyChanges()`/`CreateDevice()`) and asserts whether
`GraphicsDeviceManager::DeviceReset` fires would directly catch finding #1.

## Positive Findings
The property surface itself (back buffer dimensions/format, depth/stencil format, vsync,
multisampling, orientation, fullscreen) is complete and matches FNA's `GraphicsDeviceManager.cs`
property-for-property.

## Final Assessment
One HIGH finding (missing device-event forwarding, full analysis in the `.cpp` report) and one LOW
finding (exception-type inconsistency for the null-game constructor guard).
