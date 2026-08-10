# GraphicsDeviceManager — Feature Coverage

Compares CNA's `GraphicsDeviceManager` against the FNA reference
(`/rv/data/library/github.com/FNA-XNA/FNA/src/GraphicsDeviceManager.cs`).

Status labels:
- **✅ supported** — implemented and tested, matches FNA behaviour
- **⚠ partial** — implemented but with a known limitation or deviation
- **❌ missing** — not implemented
- **🔧 NOXNA** — CNA extension not present in XNA/FNA

---

## Public properties

| FNA property | C++ getter / setter | Status | Notes |
|---|---|---|---|
| `GraphicsProfile` | `getGraphicsProfileProperty()` / `setGraphicsProfileProperty()` | ✅ supported | |
| `GraphicsDevice` | `getGraphicsDeviceProperty()` | ✅ supported | Returns the Game-owned device |
| `IsFullScreen` | `getIsFullScreenProperty()` / `setIsFullScreenProperty()` | ✅ supported | `SDL_SetWindowFullscreen` failure is non-fatal (soft-skip); PP field always correct |
| `PreferMultiSampling` | `getPreferMultiSamplingProperty()` / `setPreferMultiSamplingProperty()` | ⚠ partial | PP stores `MultiSampleCount=8` when true; renderer MSAA cannot change at runtime (construction-time only) |
| `PreferredBackBufferFormat` | `getPreferredBackBufferFormatProperty()` / `setPreferredBackBufferFormatProperty()` | ✅ supported | |
| `PreferredBackBufferHeight` | `getPreferredBackBufferHeightProperty()` / `setPreferredBackBufferHeightProperty()` | ✅ supported | |
| `PreferredBackBufferWidth` | `getPreferredBackBufferWidthProperty()` / `setPreferredBackBufferWidthProperty()` | ✅ supported | |
| `PreferredDepthStencilFormat` | `getPreferredDepthStencilFormatProperty()` / `setPreferredDepthStencilFormatProperty()` | ⚠ partial | PP field stored; EasyGL does not recreate the depth buffer at runtime |
| `SynchronizeWithVerticalRetrace` | `getSynchronizeWithVerticalRetraceProperty()` / `setSynchronizeWithVerticalRetraceProperty()` | ✅ supported | Maps to `PresentInterval::One` (true) or `PresentInterval::Immediate` (false) |
| `SupportedOrientations` | `getSupportedOrientationsProperty()` / `setSupportedOrientationsProperty()` | ✅ supported | |

---

## Public static constants

| FNA | CNA | Status | Notes |
|---|---|---|---|
| `DefaultBackBufferWidth = 800` | `static constexpr intcs DefaultBackBufferWidth = 800` | ✅ supported | |
| `DefaultBackBufferHeight = 480` | `static constexpr intcs DefaultBackBufferHeight = 480` | ✅ supported | |

---

## Public events

| FNA event | CNA member | Status | Notes |
|---|---|---|---|
| `Disposed` | `Disposed` | ✅ supported | Raised from `Dispose(bool)` |
| `DeviceCreated` | `DeviceCreated` | ✅ supported | Raised at end of `CreateDevice()` |
| `DeviceDisposing` | `DeviceDisposing` | ✅ supported | |
| `DeviceReset` | `DeviceReset` | ✅ supported | Raised at end of `ApplyChanges()` |
| `DeviceResetting` | `DeviceResetting` | ✅ supported | Raised at start of `ApplyChanges()` |
| `PreparingDeviceSettings` | `PreparingDeviceSettings` | ✅ supported | Fired from `INTERNAL_CreateGraphicsDeviceInformation` |

---

## Public methods

| FNA method | CNA method | Status | Notes |
|---|---|---|---|
| `GraphicsDeviceManager(Game)` | `GraphicsDeviceManager(Game*)` | ✅ supported | Registers as `IGraphicsDeviceManager` + `IGraphicsDeviceService`; calls `ApplyChanges()` |
| `ApplyChanges()` | `ApplyChanges()` | ⚠ partial | See §ApplyChanges deviation below |
| `ToggleFullScreen()` | `ToggleFullScreen()` | ✅ supported | Flips `IsFullScreen` and calls `ApplyChanges()` |
| `Dispose()` | `Dispose()` | ✅ supported | Calls `Dispose(true)`, unregisters services |

---

## Protected virtual methods

| FNA method | CNA method | Status | Notes |
|---|---|---|---|
| `Dispose(bool)` | `Dispose(bool)` | ✅ supported | |
| `OnDeviceCreated` | `OnDeviceCreated` | ✅ supported | |
| `OnDeviceDisposing` | `OnDeviceDisposing` | ✅ supported | |
| `OnDeviceReset` | `OnDeviceReset` | ✅ supported | |
| `OnDeviceResetting` | `OnDeviceResetting` | ✅ supported | |
| `OnPreparingDeviceSettings` | `OnPreparingDeviceSettings` | ✅ supported | |
| `CanResetDevice(GDI)` | `CanResetDevice(GDI)` | ⚠ partial | FNA throws `NotImplementedException`; CNA returns `true` when device != nullptr |
| `FindBestDevice(bool)` | `FindBestDevice(bool)` | ⚠ partial | FNA throws `NotImplementedException`; CNA returns a sensible default GDI |
| `RankDevices(List<GDI>)` | `RankDevices(vector<GDI>)` | ⚠ partial | FNA throws `NotImplementedException`; CNA is a no-op (single SDL adapter) |

---

## IGraphicsDeviceManager (explicit interface)

| FNA | CNA | Status |
|---|---|---|
| `IGraphicsDeviceManager.CreateDevice()` | `CreateDevice()` override | ✅ supported |
| `IGraphicsDeviceManager.BeginDraw()` | `BeginDraw()` override | ✅ supported |
| `IGraphicsDeviceManager.EndDraw()` | `EndDraw()` override | ✅ supported — calls `Present()` |

---

## NOXNA extensions (CNA-only, not in FNA)

| CNA | Purpose |
|---|---|
| `GraphicsDeviceManager()` (default ctor) | Allows GDM construction without a Game (used in headless tests) |
| `PresentationMode` enum | Scaling policy: Letterbox / Overscan / Stretch / NativeBackBuffer / FixedHeightDynamicWidth |
| `getPreferredPresentationModeProperty()` / `setPreferredPresentationModeProperty()` | Controls the NOXNA scaling policy |

---

## ApplyChanges — key deviation from FNA

**FNA** ends `ApplyChanges()` with:
```csharp
graphicsDevice.Reset(gdi.PresentationParameters, gdi.Adapter);
```
`GraphicsDevice.Reset()` in FNA triggers a full device reset: it can change back-buffer
format, sample count, depth format, and present interval simultaneously.

**CNA** ends `ApplyChanges()` with `applyToExistingRenderer(gdi)`, which:
1. Calls `GraphicsDevice::SetPresentationParameters(pp)` — stores PP + updates swap interval.
2. Calls `SDL_SetWindowFullscreen` (soft-fail).
3. Calls `SDL_SetWindowSize`.
4. Calls `GraphicsDevice::SetPresentationMode` (NOXNA scaling).
5. Calls `GraphicsDevice::SetVirtualResolution` — updates renderer virtual size + viewport.

**Consequence**: changes to `PreferredBackBufferFormat`, `PreferredDepthStencilFormat`, and
`PreferMultiSampling` are reflected in the device PP but are **not applied to renderer GPU
resources** until the renderer is recreated. Changing `BackBufferWidth/Height` and
`SynchronizeWithVerticalRetrace` are fully applied at runtime.

---

## Service registration

| | FNA | CNA |
|---|---|---|
| Registers as `IGraphicsDeviceManager` | ✅ in ctor | ✅ in `registerServices()` (since Task 225) |
| Registers as `IGraphicsDeviceService` | ✅ in ctor | ✅ in `registerServices()` (since Task 225) |
| `Game::DoInitialize()` calls `CreateDevice()` | ✅ | ✅ (since Task 225) |
| `Dispose()` unregisters services | ✅ | ✅ |

---

## Defaults comparison

| Property | FNA default | CNA default |
|---|---|---|
| `IsFullScreen` | false | false ✅ |
| `PreferMultiSampling` | false | false ✅ |
| `PreferredBackBufferFormat` | `SurfaceFormat.Color` | `SurfaceFormat::Color` ✅ |
| `PreferredBackBufferWidth` | 800 | 800 ✅ |
| `PreferredBackBufferHeight` | 480 | 480 ✅ |
| `PreferredDepthStencilFormat` | `DepthFormat.Depth24` | `DepthFormat::Depth24` ✅ |
| `SynchronizeWithVerticalRetrace` | true | true ✅ |
| `SupportedOrientations` | `DisplayOrientation.Default` | `DisplayOrientation::Default` ✅ |

---

*Last updated: Task 230 (2026-06-27). Covers CNA commit `4d881ef`.*
