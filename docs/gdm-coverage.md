# `GraphicsDeviceManager` contract and coverage

This page describes the current `Microsoft::Xna::Framework::GraphicsDeviceManager`
contract. It is not a task snapshot. The readable reference is FNA's
`src/GraphicsDeviceManager.cs` from the local FNA checkout; measured Microsoft XNA behavior takes
precedence where the two differ. The CNA declaration is
`modules/runtime/include/Microsoft/Xna/Framework/GraphicsDeviceManager.hpp`; its implementation is
`modules/runtime/src/GraphicsDeviceManager.cpp`.

As of 2026-10-31, the XNA/FNA surface is present and `ApplyChanges()` reaches a real
`GraphicsDevice::Reset()`. The former limitations that said multisampling, back-buffer format and
depth/stencil format changed only stored `PresentationParameters` are closed. Two current API
defects are called out below and are also recorded in [`handoff.md` §5.1](../handoff.md#51-defects).

## Public surface and defaults

| XNA/FNA member | CNA status | Current contract |
|---|---|---|
| `GraphicsProfile` | Supported with a default-source adaptation | The property is applied to the live device before reset. FNA may read a managed assembly resource; CNA starts from `CNA::GetProjectGraphicsProfileEXT()` and still honors an explicit setter. |
| `GraphicsDevice` | Supported with an ownership adaptation | Returns the device owned by `Game`; see [lifecycle adaptations](#lifecycle-adaptations). |
| `IsFullScreen` | Supported | Becomes `PresentationParameters::IsFullScreen` and a platform fullscreen/windowed request during reset. A platform may refuse a physical mode change. |
| `PreferMultiSampling` | Supported | `false` requests zero samples. `true` requests up to 8; the renderer chooses a supported value and the applied value is written back to the device's presentation parameters. |
| `PreferredBackBufferFormat` | Supported | Reaches the renderer's presentation-format reset. The device reports the renderer-normalized applied format. |
| `PreferredBackBufferWidth`, `PreferredBackBufferHeight` | **Defective validation** | Defaults and positive values work, but CNA currently accepts zero and negative values. FNA throws `ArgumentOutOfRangeException` for `value <= 0`. |
| `PreferredDepthStencilFormat` | Supported | Reaches the renderer's presentation-format reset and recreates or reconfigures its depth/stencil storage as required. The device reports the normalized applied format. |
| `SynchronizeWithVerticalRetrace` | Supported | Maps to `PresentInterval::One` or `Immediate`; reset forwards the corresponding swap interval to the renderer. |
| `SupportedOrientations` | Supported | Applied on orientation-aware mobile platforms and by CNA's keyboard-orientation test mode. Desktop back-buffer dimensions otherwise remain in the requested order. |
| `DefaultBackBufferWidth`, `DefaultBackBufferHeight` | Supported | `800` and `480`, respectively. |
| `GraphicsDeviceManager(Game*)` | Supported with a lifecycle adaptation | Rejects null, registers both graphics services, establishes the XNA defaults and leaves first device setup to `Game::DoInitialize()`. It does not call `ApplyChanges()` from the constructor. |
| `ApplyChanges()` | Supported | Applies pending preferences through the reset pipeline below; does nothing when no preference or resized-buffer request is pending. |
| `ToggleFullScreen()` | Supported | Flips `IsFullScreen` and calls `ApplyChanges()`. |
| `Dispose()` | Supported with an ownership adaptation | Idempotently unregisters services and raises the manager lifecycle events. It must not delete the `Game`-owned device. |

The other FNA defaults are preserved: windowed, multisampling off, `SurfaceFormat::Color`,
`DepthFormat::Depth24`, vertical synchronization on and `DisplayOrientation::Default`.

The protected `Dispose(bool)` and `On*` hooks are present. `CanResetDevice`, `FindBestDevice` and
`RankDevices` are also present, but intentionally do useful CNA work instead of reproducing FNA's
`NotImplementedException` stubs: they respectively accept a reset when a device exists, construct
the default candidate and leave the single candidate order unchanged.

## What `ApplyChanges()` applies

After device creation has installed the event forwarding, the ordinary path is:

1. A property setter records the preference and marks the manager dirty.
2. `ApplyChanges()` clones the current presentation parameters and prepares a
   `GraphicsDeviceInformation` containing the requested size, formats, fullscreen state, present
   interval, orientation, profile and multisample count.
3. `PreparingDeviceSettings` runs and may alter that candidate. Because the shared C++ event
   handler passes event arguments as `const`, a mutating C++ subscriber uses
   `PreparingDeviceSettingsEventArgs::getGraphicsDeviceInformationEXT()`.
4. The game window receives `BeginScreenDeviceChange` / `EndScreenDeviceChange`.
5. CNA applies the graphics profile and its presentation-scaling mode, then calls
   `GraphicsDevice::Reset()` with the candidate parameters and adapter.
6. Reset raises `DeviceResetting`, normalizes the color/depth formats, applies the window size and
   fullscreen request, updates the renderer's virtual resolution and presentation formats,
   changes the actual MSAA storage, writes the applied sample count back, applies the swap
   interval, refreshes the viewport and finally raises `DeviceReset`.

The manager forwards the device's reset events rather than raising duplicates. Consequently one
initialized-manager `ApplyChanges()` produces one `DeviceResetting` and one `DeviceReset` in that
order. A renderer may clamp a request or select a supported fallback; callers must inspect the
device's applied `PresentationParameters` instead of assuming the preferred value was accepted.

This reset path is exercised for live back-buffer size, back-buffer format, depth/stencil format,
MSAA and present-interval changes. `GraphicsDevice::RecreateRendererForMultiSampleCount()` is a
separate CNAEXT escape hatch that destroys and rebuilds a renderer; it is not the manager's normal
MSAA path.

## Events and frame control

| Member | Current behavior |
|---|---|
| `Disposed` | Raised once when the manager is disposed. |
| `DeviceCreated` | Raised after initial `CreateDevice()` setup. As in FNA, its supplied sender is forwarded. |
| `DeviceDisposing` | Raised by manager disposal, with the manager as sender. **Known defect:** disposal initiated directly on the managed `GraphicsDevice` releases the frame lease but is not forwarded to this event. |
| `DeviceResetting`, `DeviceReset` | Forward the managed device's events after `CreateDevice()` installs subscriptions; both use the manager as sender, matching FNA's `On*` asymmetry. |
| `PreparingDeviceSettings` | Raised before both initial setup and a later reset, with the mutable candidate described above. |
| `BeginDraw()` | Returns false without a device or while renderer recovery says drawing is unsafe; otherwise acquires the renderer-thread context for the frame. The recovery gate and context lease are CNA adaptations. |
| `EndDraw()` | Presents only after a successful `BeginDraw()` and releases the context lease even when presentation throws. |

## Lifecycle adaptations

FNA allocates and owns the `GraphicsDevice` inside its manager. CNA's `Game` eagerly owns a device
value, so the manager points to that existing object and resets it. `Game::DoInitialize()` calls
`CreateDevice()` once after a game's constructor; this is why the manager constructor deliberately
does not perform the same reset first. A pre-initialization `ApplyChanges()` therefore operates on
the eager device, while normal event forwarding is installed by the later `CreateDevice()` call.

A physical client-window resize is also intentionally different. FNA copies the new physical
client size into the back buffer and calls `ApplyChanges()`. CNA preserves the game's logical
back-buffer coordinate space for its presentation-scaling modes, so a client resize refreshes the
viewport from the window without rewriting the preferred logical size. A programmatic preferred
back-buffer change still takes the full reset path.

CNA extensions are visibly marked:

- the default constructor creates an unattached manager for headless/property tests; it cannot
  create a device without a `Game`;
- `PresentationMode` and its preferred-mode property choose `Letterbox` (the default), `Overscan`,
  `Stretch`, `NativeBackBuffer` or `FixedHeightDynamicWidth`;
- the frame recovery gate, renderer-thread context lease, keyboard orientation and project-wide
  graphics-profile default have no XNA/FNA equivalent.

## Native C API

The approved C ABI exposes manager creation/destruction, preferences, apply/create/toggle/frame
operations, the borrowed game-owned device and all manager events. Its ordinary
`PreparingDeviceSettings` callback is observational; the explicitly named `*_ext` subscription
provides the mutable candidate. The ownership and error-mapping rules are documented in
[`c-api/GAME_COMPONENTS.md`](c-api/GAME_COMPONENTS.md#the-graphics-device-manager); generated route
definitions live in `modules/c-api/spec/GraphicsDeviceManager.toml`.

## Evidence and related capability pages

The focused regression evidence includes:

- `GraphicsDeviceManagerTests`, `GraphicsDeviceManagerPlatformTests` and the orientation suites for
  defaults, service/device lifecycle, event count/sender semantics and orientation;
- `PresentationLifecycleTests` and `viewport_reset_after_resize_test` for reset ordering, applied
  parameters, repeated resize and viewport behavior;
- `msaa_runtime_change_test`, `presentation_msaa_contract_test` and renderer MSAA tests for a live
  `PreferMultiSampling` transition and applied-count write-back;
- `easygl_depth_format_test` and `software_presentation_reset_contract_test` for live depth-format
  replacement;
- `GraphicsDeviceManagerSmoke.c` for the C ABI preferences, operations and subscriptions.

Renderer-specific format/MSAA availability is not a manager promise. See
[`rendertarget-support.md`](rendertarget-support.md),
[`depthstencilstate-support.md`](depthstencilstate-support.md), the
[renderer feature matrix](graphics-renderer-feature-matrix.md) and each renderer's own page for those
capability boundaries.

*Reconciled against FNA and current CNA source on 2026-10-31.*
