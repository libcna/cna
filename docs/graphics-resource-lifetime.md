# Graphics Resource Lifetime Rules

This document describes how GPU resources are created, tracked, and destroyed in CNA.
It applies to `Texture2D`, `VertexBuffer`, `IndexBuffer`, `RenderTarget2D`, and any
future class that inherits `GraphicsResource` and holds a renderer handle.

`docs/adr/0001-modern-gpu-ordering-lifetime.md` is the normative contract for ordering,
deferred-command retention, synchronization and physical native-handle retirement. This document
describes the public wrapper/event mechanics and the currently implemented renderer paths.

---

## 1. Ownership Model

Every GPU-backed resource owns a renderer-side object. Existing XNA resources hold a
`std::unique_ptr<IXxxRenderer>` (for example `IVertexBufferRenderer` or `ITexture2DRenderer`). A
modern resource whose accepted work may outlive its public wrapper may instead own an internal
shared record, as `Texture2DArray` does; that sharing never reaches the public API and exists only
for command/fence retention.

`GraphicsResource` itself does not hold a renderer pointer. The derived class is
responsible for declaring and managing `renderer_`.

The renderer object may hand its native handles to an internal retained record before the public
wrapper releases it. That renderer-internal record is not a second public owner: it exists only so
a command accepted before `Dispose()` cannot dereference a dead wrapper or free a handle still in
flight.

---

## 2. When Public and Native Ownership End

Public ownership ends **when `Dispose()` is called**, not when the C++ object is destroyed. Derived
classes achieve this by overriding `Dispose(bool)`:

```cpp
void VertexBuffer::Dispose(bool disposing)
{
    renderer_.reset();                       // releases this wrapper's renderer-side ownership
    GraphicsResource::Dispose(disposing);   // sets isDisposed_, fires events, unregisters
}
```

Resetting the renderer object may destroy an unused native handle immediately or enqueue its native
record for fence/completion-safe retirement. The native handle is never promised to disappear at
the instant the public object becomes disposed.

The destructor calls `Dispose(false)`, so if the user forgets to call `Dispose()` the wrapper's
renderer-side ownership is still released eventually. The `Disposing` event is not fired on that
path; `ResourceDestroyed` is still raised while the device is alive because deregistration must
occur. Call `Dispose()` explicitly when subscribers need the `Disposing` notification.

### Override chain

| Class              | Calls                              |
|--------------------|------------------------------------|
| `VertexBuffer`     | `renderer_.reset()` → `GraphicsResource::Dispose(bool)` |
| `IndexBuffer`      | `renderer_.reset()` → `GraphicsResource::Dispose(bool)` |
| `Texture2D`        | `renderer_.reset()` → `Texture::Dispose(bool)` → `GraphicsResource::Dispose(bool)` |
| `RenderTarget2D`   | `renderer_.reset()` → `Texture2D::Dispose(bool)` → … |
| `Texture2DArray`   | internal shared-record reset → `GraphicsResource::Dispose(bool)` |

---

## 3. GraphicsDevice Tracking List

`GraphicsDevice` maintains an internal `std::vector<GraphicsResource*> resources_` that
holds a raw (non-owning) pointer to every resource created with that device.

- **Registration**: `GraphicsResource` constructor calls `AddResourceReference(this)`.
- **Deregistration**: `GraphicsResource::Dispose(bool)` calls `RemoveResourceReference(this)`.

`RemoveResourceReference` uses swap-and-pop for O(1) removal; insertion order is not
preserved.

### Safe disposal order during `GraphicsDevice::Dispose()`

The device copies and clears the list before iterating:

```cpp
std::vector<GraphicsResource*> toDispose = std::move(resources_);
resources_.clear();
for (GraphicsResource* res : toDispose)
    static_cast<System::IDisposable*>(res)->Dispose();
destroyNativeResources();   // SDL context, Vulkan instance, etc.
```

This means:

1. All public resource-renderer ownership is released **before** the device renderer is torn down.
   The renderer drains or abandons its native retirement records while its context/device is still
   valid.
2. `RemoveResourceReference` is a no-op during device disposal (the list is already
   empty), so re-entrancy is safe.
3. The device renderer (`destroyNativeResources()`) is destroyed last.

Tracked resources are disposed by `GraphicsDevice::Dispose()`, so their later C++ destructors see
an already-disposed wrapper. A resource must not outlive the `GraphicsDevice` that owns it: the
resource keeps a non-owning device pointer, and escaping device tracking (including the move caveat
below) invalidates that protection.

---

## 4. ResourceCreated / ResourceDestroyed Events

`GraphicsDevice` exposes two events:

```cpp
System::EventHandler<ResourceCreatedEventArgs>   ResourceCreated;
System::EventHandler<ResourceDestroyedEventArgs> ResourceDestroyed;
```

- `ResourceCreated` is raised in the `GraphicsResource` constructor, **after**
  `AddResourceReference`.
- `ResourceDestroyed` is raised in `GraphicsResource::Dispose(bool)`, **before**
  `RemoveResourceReference`.

Both events are skipped if no handlers are subscribed (`EventHandler::Empty()` check).

`ResourceDestroyedEventArgs` carries the resource's `Name` string and `Tag` pointer
at the time of disposal — captured before `isDisposed_` is set.

Resources constructed without a device (e.g. `BlendState` default instances) do not
register, do not fire `ResourceCreated`, and do not fire `ResourceDestroyed`.

---

## 5. Move Semantics

Move construction and move assignment transfer `unique_ptr` ownership without
duplicating or releasing the GPU handle:

```cpp
VertexBuffer a(dev, 64);
VertexBuffer b = std::move(a);   // b owns the GPU buffer; a.renderer_ == nullptr
```

Move operations are declared in `.hpp` without `= default` because the renderer type is
forward-declared there and `unique_ptr`'s move requires a complete deleter type. The
`= default` is placed in the `.cpp` where the complete renderer header is included:

```cpp
// VertexBuffer.cpp
VertexBuffer::VertexBuffer(VertexBuffer&&) noexcept = default;
VertexBuffer& VertexBuffer::operator=(VertexBuffer&&) noexcept = default;
```

After a move, calling `Dispose()` on the moved-from object is safe: `renderer_.reset()`
on a null `unique_ptr` is a no-op.

The `GraphicsResource` base's `resources_` pointer entry is **not** updated during a
move. If you move a tracked resource, the device still holds the original address.
Avoid moving tracked resources out of their original storage location.

`Texture2DArray` deletes both move operations, so its tracked address cannot change.

---

## 6. Resources Without a Device

Some resources (e.g. `BlendState`, `SamplerState`) may be constructed without a
`GraphicsDevice`. In that case:

- `graphicsDevice_` is `nullptr`.
- `AddResourceReference` and `OnResourceCreated` are not called.
- `RemoveResourceReference` and `OnResourceDestroyed` are not called on Dispose.
- No tracking list entry is created.

---

## 7. Renderer-Specific Caveats

### EasyGL

- GL object IDs are freed by the renderer destructor (`glDeleteTextures`,
  `glDeleteBuffers`, `glDeleteFramebuffers`). This must happen while the SDL/OpenGL
  context is current.
- Existing immediate GL calls execute in public order. A future modern resource must detach a
  current binding and retain any CNA-side deferred record on disposal; it must not add a new
  caller-side "unbind first" rule.
- If the GL context is lost (window resize, driver crash), renderers may hold invalid
  IDs. Current EasyGL does not implement context-loss recovery; the safest approach is
  to dispose all resources and recreate from scratch.

### Vulkan

- Vulkan handles (`VkBuffer`, `VkImage`, `VkDeviceMemory`) are freed during renderer
  destruction or the renderer's frame-fence-gated retirement pass. The logical device
  (`VkDevice`) is still valid at that point.
- The disposal order guaranteed by `GraphicsDevice::Dispose()` (resources first, device
  renderer second) satisfies this requirement automatically.
- Existing XNA textures, buffers, effects, render targets and queries detach deferred wrapper
  pointers and retire referenced native handles after the consuming frame fence. `MOD-2252` proves
  that storage buffers, compute programs, storage images, texture arrays and GPU timers use the same
  mechanism across submit, resize and device-first teardown. Applications must not call
  `vkDeviceWaitIdle` (nor can they access the native device).
- Compute descriptor snapshots do not own shared resource records indefinitely. Pending commands
  retain their exact records through command recording; destruction then evicts every descriptor
  snapshot that names the dying buffer or image view and retires both behind the same frame fence.
  This preserves submitted work and prevents recycled native handles from selecting stale bindings.

### Bgfx

- Bgfx handles (`bgfx::TextureHandle`, `bgfx::VertexBufferHandle`, etc.) are freed
  via `bgfx::destroy(handle)` inside the renderer destructor.
- Bgfx queues destructions internally; the actual GPU deallocation may be deferred to
  the next `bgfx::frame()` call.
- Do not call `bgfx::shutdown()` before all resource renderers are destroyed. The
  `GraphicsDevice::Dispose()` order guarantees this as long as `bgfx::shutdown()` is
  called inside `destroyNativeResources()`.

### SDL_Renderer

- `SDL_Texture` objects are destroyed with `SDL_DestroyTexture`. The `SDL_Renderer`
  must still exist at that point.
- The disposal order in `GraphicsDevice::Dispose()` (textures first, renderer second)
  satisfies this requirement.

---

## 8. Quick Reference

| Question | Answer |
|---|---|
| When does public ownership end? | On `Dispose()`, with destructor fallback |
| When is the native handle freed? | Immediately if unused, otherwise after the renderer's completion token/fence |
| Is double-dispose safe? | Yes — `isDisposed_` guard makes it a no-op |
| What happens when the device is disposed? | All tracked resources are disposed first, then the device renderer |
| Do events fire on destructor path? | `ResourceDestroyed` does; `Disposing` does not |
| Does move transfer the tracking pointer? | No — avoid moving tracked resources to a different address |
| Can I use a resource after `Dispose()`? | No — `isDisposed_` is set; GPU handle is null |
| Must the caller wait for the GPU before disposal? | No — fence/completion-safe retirement is renderer-owned |
