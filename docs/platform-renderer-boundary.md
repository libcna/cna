# Renderer-facing platform boundary

This is the implemented boundary between platform windows and graphics renderers, checked against
IGraphicsRenderer.hpp, GraphicsDevice.cpp and Game.cpp on 2026-11-01. The original PLAT-57 design
and completed Phase 4 migration tasks remain in Git history; the old proposed suspend/resume
notification table was not the final interface.

## Surface snapshot and construction

```cpp
struct RendererSurfaceInfo
{
    CNA::Platform::WindowId windowId;
    CNA::Platform::NativeWindowHandle nativeHandle;
    CNA::Platform::WindowSize drawableSize;
    float displayScale;
};
```

The value owns nothing. drawableSize is the platform window's physical pixel size, while client
bounds/input coordinates use logical units. displayScale comes from the window, not a division
of rounded dimensions. Consumers such as PlatformRendererSurfaceState normalize invalid/nonpositive
scale to one and reject replacement of the stable id/native handle.

GraphicsDevice creates/adopts its platform window before the renderer and snapshots those four
fields. GraphicsRendererCreateArgs also carries renderer construction policy (virtual resolution,
presentation mode, recovery, multisampling, swap interval, formats/profile and device callback).
Its window plumbing consists of the snapshot and optional narrow services:

| Service | Ownership and use |
|---|---|
| IPlatformGlContext | Borrowed from the platform. EasyGL owns/destroys the GL context it creates through the service and uses WindowId rather than an IPlatformWindow pointer. |
| IPlatformSurfacePresenter | Owned by GraphicsDevice; borrowed by the CPU renderer. It presents a completed RGBA frame and is destroyed after the renderer, before the window. |
| NativeWindowHandle | Borrowed discriminated value for renderer-local native surface creation; copying it does not retain a window. |

Descriptor needsGlContext/needsSurfacePresenter selects the service for the renderer actually
being constructed, including multi-renderer builds. A missing required service is refused; do not
pass IPlatform or IPlatformWindow through the common creation contract as an escape hatch.

SDLGPU and FNA3D are explicit SDL3-dependent exceptions. Their family adapters may resolve the
stable window id to SDL's window internally; common creation arguments do not expose SDL_Window.
See [generated renderer SDL audit](platform-renderer-sdl-audit.md).

## Platform, window and context lifetime

The platform must outlive its devices/windows/services. Game establishes that order by owning and
installing its platform before its other members; standalone GraphicsDevice consumers must also
respect the lifetime of their selected/ambient platform.

GraphicsDevice destroys its renderer first, then its CPU presenter, then its platform-window
wrapper, and releases the video acquisition afterwards. Renderer context leases ensure native
resources are destroyed in the owning context. A platform service is not an independently owned
resource merely because the renderer stores its pointer.

PresentationParameters.DeviceWindowHandle is an opaque platform compatibility token.
AdoptWindowHandle/GetWindowHandle round-trip it through the creating platform; it is distinct from
NativeWindowHandle. An adopted wrapper can borrow the caller's native window: owning the wrapper
does not transfer native-window ownership.

A native-handle replacement is not transparently supported by mutating the stored snapshot.
The common surface-state helper throws on a changed id/handle. Any future native-window
replacement needs an explicit renderer/window reconstruction path; no automatic swapchain
retargeting is promised here.

## Actual event and refresh flow

Game drains IPlatform.PollEvents into a reused batch and passes each mapped event to the input
bridge before its runtime reaction. GraphicsDevice does not own/drain a second event queue.
The current Game reaction intentionally does not filter all window events by its own WindowId;
that behavior is covered by the event transcript.

| Runtime input | Current calls/effect |
|---|---|
| Resized, PixelSizeChanged, DisplayScaleChanged | Update GameWindow state; refresh GraphicsDevice viewport/surface snapshot; invalidate the renderer surface using the event's window id. |
| FocusGained, fullscreen entered/left | Surface invalidation; fullscreen events also synchronize the manager's flag. |
| Exposed, Minimized, Maximized, Restored, DisplayChanged | Surface invalidation. These are not typed suspend/resume snapshot notifications. |
| WillEnterBackground | Mark Game suspended/inactive; the mobile loop waits rather than continuing normal rendering. |
| DidEnterForeground | Clear suspension, reset elapsed-time accumulation, reactivate and invalidate with id zero. |
| LowMemory / Terminating | Log memory pressure / clear suspension and exit. |

GraphicsDevice.UpdateViewportFromWindow re-queries the owned window and calls
`OnSurfaceChanged(const RendererSurfaceInfo&)` synchronously when the queries succeed.
There is no reason enum or suspended flag in that call. Query exceptions log and retain the
previous snapshot; renderer exceptions from OnSurfaceChanged propagate rather than being hidden.
Other viewport/reset paths can also refresh the snapshot, so this is not exactly one notification
per native event.

`OnSurfaceInvalidated(WindowId)` is a separate redraw/revalidation hook. Its default may do
nothing; actual renderer handling determines what needs reconfiguration. Invalidation does not
automatically imply a GraphicsDevice reset, device loss or swapchain recreation.

Real driver loss travels back through RendererDeviceEvent/deviceEventCallback and the resource
recovery path. It is distinct from a window resize/expose or application suspension.

## Coordinates, presentation and performance

The renderer registry is keyed by stable WindowId. Input and renderer transforms distinguish
platform logical client units, physical drawable pixels and renderer logical game coordinates,
including presentation viewport/letterbox offsets. A missing registered transform falls back to
1:1 mapping. Normalized touch events carry the client size used for their conversion.

WindowDescription carries render intent and pre-window framebuffer requirements. The platform
implementation maps those to its native toolkit; renderer drawing still goes directly to the
graphics API. CPU renderers hand over completed frames, not a series of drawing primitives.
Context binding, resource upload and presentation can require more than one native/service call;
there is no blanket one-call-per-frame promise.

## Verification boundary

Current source/interface checks establish the flow above. PlatformRendererSurfaceState tests
exercise non-unit density, resize and stable-handle validation. GraphicsDevicePlatformWindowTests
build a real SOFTWARE device on a traced fake window, through a decorated platform that reports a
surface presenter as its only display route (as TERMINAL does on a TTY), and check creation,
adoption, presenter-before-window teardown, constructor failure and size-query failure in every
build that contains SOFTWARE. They do not prove teardown for the native-handle renderers, whose
devices cannot be built on a fake window. Native window-system conformance and cross-implementation
event tests provide complementary evidence.
