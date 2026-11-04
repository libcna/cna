# `GraphicsDeviceManager` contract and coverage

This page describes the current `Microsoft::Xna::Framework::GraphicsDeviceManager`
contract. It is not a task snapshot. The readable reference is FNA's
`src/GraphicsDeviceManager.cs` from the local FNA checkout; measured Microsoft XNA behavior takes
precedence where the two differ. The CNA declaration is
`modules/runtime/include/Microsoft/Xna/Framework/GraphicsDeviceManager.hpp`; its implementation is
`modules/runtime/src/GraphicsDeviceManager.cpp`.

As of 2026-11-04, `ApplyChanges()` resets an unchanged-profile device and replaces it when
`CanResetDevice` refuses the candidate (XNA `ChangeDevice` IL_00c4-0120). The former limitations that said multisampling, back-buffer format and
depth/stencil format changed only stored `PresentationParameters` are closed. Preferred-size validation and direct-device disposal forwarding follow the reference behavior
and are covered by native and C API regressions below.

## Public surface and defaults

| XNA/FNA member | CNA status | Current contract |
|---|---|---|
| `GraphicsProfile` | Supported with a default-source adaptation | Changing the applied profile disposes the old device and constructs a distinct replacement. FNA may read a managed assembly resource; CNA starts from `CNA::GetProjectGraphicsProfileEXT()` and still honors an explicit setter. |
| `GraphicsDevice` | Supported with an ownership adaptation | Returns the device owned by `Game`; see [lifecycle adaptations](#lifecycle-adaptations). |
| `IsFullScreen` | Supported | Becomes `PresentationParameters::IsFullScreen` and a platform fullscreen/windowed request during reset. A platform may refuse a physical mode change. |
| `PreferMultiSampling` | Supported | `false` requests zero samples. `true` requests up to 8; the renderer chooses a supported value and the applied value is written back to the device's presentation parameters. |
| `PreferredBackBufferFormat` | Supported | Reaches the renderer's presentation-format reset. The device reports the renderer-normalized applied format. |
| `PreferredBackBufferWidth`, `PreferredBackBufferHeight` | Supported | Defaults are 800×480. Nonpositive values throw `ArgumentOutOfRangeException` with parameter `value` before changing the preference. Positive values are stored for later device negotiation. |
| `PreferredDepthStencilFormat` | Supported | Reaches the renderer's presentation-format reset and recreates or reconfigures its depth/stencil storage as required. The device reports the normalized applied format. |
| `SynchronizeWithVerticalRetrace` | Supported | Maps to `PresentInterval::One` or `Immediate`; reset forwards the corresponding swap interval to the renderer. |
| `SupportedOrientations` | Supported | Applied on orientation-aware mobile platforms and by CNA's keyboard-orientation test mode. Desktop back-buffer dimensions otherwise remain in the requested order. |
| `DefaultBackBufferWidth`, `DefaultBackBufferHeight` | Supported | `800` and `480`, respectively. |
| `GraphicsDeviceManager(Game*)` | Supported with a lifecycle adaptation | Rejects null, registers both graphics services, establishes the XNA defaults and leaves first device setup to `Game::DoInitialize()`. It does not call `ApplyChanges()` from the constructor. |
| `ApplyChanges()` | Supported | Applies pending preferences through the reset or replacement pipeline below; does nothing when no preference or resized-buffer request is pending. |
| `ToggleFullScreen()` | Supported | Flips `IsFullScreen` and calls `ApplyChanges()`. |
| `Dispose()` | Supported with an ownership adaptation | Removes only its matching graphics-device service and disposes the device, retaining the manager registration until C++ destruction. Native `Disposed` fires on every explicit call; the C adapter notifies once. |

The other FNA defaults are preserved: windowed, multisampling off, `SurfaceFormat::Color`,
`DepthFormat::Depth24`, vertical synchronization on and `DisplayOrientation::Default`.

The protected `Dispose(bool)` and `On*` hooks are present. `CanResetDevice`, `FindBestDevice` and
`RankDevices` are also present, but intentionally do useful CNA work instead of reproducing FNA's
`NotImplementedException` stubs: they respectively accept a reset when the device already has the requested profile, construct
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
4. If `CanResetDevice` refuses the prepared candidate, creation disposes the old device, prepares
   that candidate again and constructs a distinct replacement, then raises `DeviceCreated`.
   Otherwise the game window receives `BeginScreenDeviceChange` / `EndScreenDeviceChange`.
5. On the reset path CNA applies its presentation-scaling mode, then calls
   `GraphicsDevice::Reset()` with the candidate parameters and adapter.
6. Reset raises `DeviceResetting`, normalizes the color/depth formats, applies the window size and
   fullscreen request, updates the renderer's virtual resolution and presentation formats,
   changes the actual MSAA storage, writes the applied sample count back, applies the swap
   interval, refreshes the viewport and finally raises `DeviceReset`.

The manager forwards the device's reset events rather than raising duplicates. Consequently one
initialized-manager same-profile `ApplyChanges()` produces one `DeviceResetting` and one `DeviceReset` in that
order. A renderer may clamp a request or select a supported fallback; callers must inspect the
device's applied `PresentationParameters` instead of assuming the preferred value was accepted.

This reset path is exercised for live back-buffer size, back-buffer format, depth/stencil format,
MSAA and present-interval changes. `GraphicsDevice::RecreateRendererForMultiSampleCount()` is a
separate CNAEXT escape hatch that destroys and rebuilds a renderer; it is not the manager's normal
MSAA path.

## Events and frame control

| Member | Current behavior |
|---|---|
| `Disposed` | Raised by every explicit native Dispose call (XNA Dispose(bool) IL_00b6-00cf); the C adapter retains its one-shot notification. |
| `DeviceCreated` | Raised after each successful `CreateDevice()`, including replacement. As in FNA, its supplied sender is forwarded. |
| `DeviceDisposing` | Raised with the manager as sender on manager disposal, device replacement, or forwarded from direct disposal of the managed device after `CreateDevice()`. Direct device disposal releases the frame lease before notification; subsequent manager cleanup does not repeat that notification. |
| `DeviceResetting`, `DeviceReset` | Forward the managed device's events after `CreateDevice()` installs subscriptions; both use the manager as sender, matching FNA's `On*` asymmetry. |
| `PreparingDeviceSettings` | Raised before initial/forced creation and before reset; a refused reset candidate is prepared again for creation (XNA ChangeDevice IL_00b6-0120 and CreateDevice IL_001a-0022). |
| `BeginDraw()` | Returns false without a device, while an applied fullscreen device has an inactive window (XNA EnsureDevicePlatform IL_0000-0016, ignoring modal overlay visibility), or while renderer recovery says drawing is unsafe; otherwise acquires the renderer-thread context for the frame. The recovery gate and context lease are CNA adaptations. |
| `EndDraw()` | Presents only after a successful `BeginDraw()` and releases the context lease even when presentation throws. |

## Lifecycle adaptations

FNA allocates and owns the `GraphicsDevice` inside its manager. CNA's `Game` eagerly owns its
initial device, which the first manager creation configures. Later explicit `CreateDevice()` calls
and profile changes dispose and replace it, preserving `Game.Window` (XNA CreateDevice IL_0000-00eb).
Disposed predecessor objects remain allocated until Game destruction so raw C++ references remain
safe. Native renderer resources are released on disposal. Game's content binding follows the replacement.

`Game::DoInitialize()` calls `CreateDevice()` before Initialize/LoadContent. `RunOneFrame()` only
ticks (XNA WindowsGameHost.RunOneFrame IL_0000-0028); use `Run()` or `RunFrameEXT()` to initialize.
A pre-initialization `ApplyChanges()` creates/configures the first managed device; initialization's
later CreateDevice call replaces it.

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
  defaults, service/device lifecycle, event count/sender semantics and orientation. Direct-device
  disposal checks notification, `Game::UnloadContent()`, lease release, repeated cleanup and
  subscription removal before manager destruction;
- `PresentationLifecycleTests` and `viewport_reset_after_resize_test` for reset ordering, applied
  parameters, repeated resize and viewport behavior;
- `msaa_runtime_change_test`, `presentation_msaa_contract_test` and renderer MSAA tests for a live
  `PreferMultiSampling` transition and applied-count write-back;
- `easygl_depth_format_test` and `software_presentation_reset_contract_test` for live depth-format
  replacement;
- `GraphicsDeviceManagerSmoke.c` for the C ABI preferences, operations and subscriptions. Native
  and strict-C regressions cover zero/negative rejection and unchanged prior sizes; native tests
  also check exception metadata and accepted positive boundaries without allocating those sizes.

Renderer-specific format/MSAA availability is not a manager promise. See
[`rendertarget-support.md`](rendertarget-support.md),
[`depthstencilstate-support.md`](depthstencilstate-support.md), the
[renderer feature matrix](graphics-renderer-feature-matrix.md) and each renderer's own page for those
capability boundaries.

*TQ-037 reconciles these lifecycle contracts with the shipped XNA 4.0 Game assembly; runtime and
platform qualification are recorded in the campaign report.*
