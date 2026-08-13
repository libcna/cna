# Renderer-facing platform boundary (PLAT-57)

## Decision

A renderer does not receive `IPlatform`, `IPlatformWindow`, or an SDL window. Its ordinary
window-facing input is a small value snapshot:

```cpp
struct RendererSurfaceInfo
{
    CNA::Platform::WindowId windowId;
    CNA::Platform::NativeWindowHandle nativeHandle;
    CNA::Platform::WindowSize drawableSize;
    float displayScale;
};
```

The names are prescriptive for the Phase 4 implementation. `drawableSize` is the physical pixel
size returned by `IPlatformWindow::GetPixelSize()`, never the logical client size. `displayScale`
is the current logical-to-physical scale and is always positive; a platform without high-DPI
support supplies `1.0f`. `nativeHandle` is a non-owning value whose validity ends with the window.
`windowId` is the stable identity used by events and by the renderer registry.

The snapshot deliberately contains no title, position, focus state, input state, clipboard,
display enumeration or platform implementation pointer. A renderer does not need those to create
or maintain a graphics surface. Adding one later requires evidence from a renderer and a contract
change; reaching for `IPlatformWindow` as an escape hatch is not allowed.

## Narrow graphics services are the only exception

Three already-defined services may accompany the snapshot when the selected renderer needs the
matching operation:

- `IPlatformGlContext` creates/makes-current/swaps an OpenGL context and resolves entry points.
- `IPlatformVulkanSurface` creates and destroys a Vulkan presentation surface.
- `IPlatformSurfacePresenter` puts one finished CPU RGBA frame on screen.

These are surface plumbing, not drawing APIs. OpenGL/Vulkan/CPU drawing remains inside the
renderer; the platform boundary is crossed only for context or surface lifecycle and at most once
per presented frame. `GraphicsRendererCreateArgs` must carry only the applicable narrow service,
not `IPlatform*`, and a missing required service is a deterministic construction error naming the
capability. Native-handle renderers (DirectX, GDI, Glide, native WebGPU/bgfx paths) need no service
pointer at all.

The four deliberate SDL exceptions (`SDL_RENDERER`, `SDL_GPU`, `FNA3D`, `FREEDIRECT`) may keep
their SDL-specific construction internally. They still do not make SDL types part of the common
renderer interface.

## Lifetime and ownership

The ownership order is fixed:

1. `Game` owns the selected `IPlatform` for longer than every graphics object.
2. `GraphicsDevice` owns the `IPlatformWindow` and creates it before the renderer.
3. The renderer may copy `RendererSurfaceInfo`; copying does not extend the native handle's
   lifetime.
4. A GL context, Vulkan surface or CPU presenter is destroyed by the renderer before the renderer
   itself finishes destruction.
5. `GraphicsDevice` destroys the renderer before destroying its platform window.

An application-supplied window is first validated/wrapped by the active platform. The renderer
still sees the same `RendererSurfaceInfo`; it never interprets
`PresentationParameters::DeviceWindowHandle` itself. A platform that cannot adopt the supplied
kind refuses explicitly instead of casting an integer to its own native type.

The strict XNA integer window property round-trips through `IPlatformWindow::GetWindowHandle()`
and `IPlatform::AdoptWindowHandle()`. It is an opaque, non-owning compatibility token, distinct
from `NativeWindowHandle`: only the creating platform may interpret it. New renderer and interop
code uses the typed native handle instead.

The native handle is immutable for one renderer lifetime. If a platform must replace the native
window, `GraphicsDevice` tears down and recreates the renderer. Mutating a stored handle underneath
a live swapchain or context is forbidden.

## Lifecycle notifications

`GraphicsDevice`, not a renderer, consumes the platform event batch. It filters by `WindowId`,
re-reads `GetPixelSize()` and `GetDisplayScale()` from the window, and synchronously notifies the
renderer with a fresh `RendererSurfaceInfo` for only these changes:

| Notification | Platform event(s) | Renderer obligation |
|---|---|---|
| `DrawableSizeChanged` | `Resized`, `PixelSizeChanged`, `DisplayChanged` | Recreate/resize the swapchain or presentation target from `drawableSize`. Duplicate unchanged snapshots are harmless. |
| `DisplayScaleChanged` | `DisplayScaleChanged` | Refresh density-dependent presentation state; also honour any simultaneous drawable-size change. |
| `Suspended` | `Minimized`, `WillEnterBackground` | Stop acquiring/presenting a surface that the OS has made unavailable. |
| `Resumed` | `Restored`, `DidEnterForeground` | Revalidate the surface and redraw; the accompanying snapshot is authoritative. |

The implementation will expose one synchronous `IGraphicsRenderer::OnSurfaceChanged(...)` entry
point rather than callbacks registered against `IPlatform`. Delivery happens on the same thread
that drains events and completes before the next update/draw. There is no subscription lifetime,
cross-thread callback, or second event queue to reconcile.

Window close, focus, movement, title and input events are not renderer notifications. Destruction
needs no notification: C++ ownership already destroys the renderer before the window. Real
driver-detected device loss travels in the opposite direction through the existing
`RendererDeviceEvent` callback and is not conflated with platform surface lifecycle.

## Construction and event flow

```text
IPlatform::CreateWindow(WindowDescription with render intent)
  -> GraphicsDevice owns IPlatformWindow
  -> GraphicsDevice snapshots WindowId / native handle / pixel size / scale
  -> CreateGraphicsRenderer(RendererSurfaceInfo + one applicable narrow service)
  -> renderer talks directly to its graphics API

IPlatform::PollEvents(batch)
  -> Game / GraphicsDevice filter the owned WindowId
  -> GraphicsDevice refreshes the snapshot
  -> IGraphicsRenderer::OnSurfaceChanged(snapshot, reason)
```

`WindowDescription` also carries the small set of framebuffer requirements that genuinely must
be selected before an OpenGL window exists (depth, stencil, double buffering and samples).
`GraphicsDevice` describes those requirements; only the platform backend maps them to its native
windowing API. This keeps pre-window visual selection from becoming a hidden toolkit call in the
graphics module.

This keeps the draw path `Game -> GraphicsDevice -> Renderer`. It does not become
`Game -> GraphicsDevice -> Platform -> Renderer`, and there is no platform virtual call per
primitive, vertex, pixel or input event.

## Consequences for the remaining Phase 4 tasks

- PLAT-58 replaces `GraphicsRendererCreateArgs::SDL_Window*` with `RendererSurfaceInfo` and the
  applicable narrow service. Window creation itself moves to the platform in PLAT-62.
- PLAT-59 removed common `GetWindowInternal()` and `GetRendererInternal()`. The complete call-site
  audit found no runtime consumer for `SDL_Renderer*`; tests that need an SDL presenter query their
  already-known SDL window inside the SDL-dependent renderer module instead.
- PLAT-60 removes `SDL_Texture*` from `ITextureRenderer`; it is not part of this boundary.
- PLAT-61 keys the registry by `WindowId`, matching both this snapshot and `PlatformEvent`.
- PLAT-66 restates coordinate conversion in logical window coordinates using the authoritative
  drawable size and scale, without calling `SDL_GetWindowSize()`.
- GL, Vulkan and CPU-presentation migrations use their narrow services. Other renderer families
  use `NativeWindowHandle` and their own API directly.

Verification for the code tasks must include a non-SDL fake window with a non-1.0 scale, a resize
notification whose logical and pixel sizes differ, lifetime-order assertions, and compilation of
the STUB/HEADLESS/SOFTWARE/PORTABLEGL families without SDL declarations in the common interface.
