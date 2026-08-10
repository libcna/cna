# Graphics Resource Lifetime Rules

This document describes how GPU resources are created, tracked, and destroyed in CNA.
It applies to `Texture2D`, `VertexBuffer`, `IndexBuffer`, `RenderTarget2D`, and any
future class that inherits `GraphicsResource` and holds a renderer handle.

---

## 1. Ownership Model

Every GPU-backed resource holds a `std::unique_ptr<IXxxRenderer>` (e.g.
`IVertexBufferRenderer`, `ITexture2DRenderer`). Ownership is exclusive: only one
`GraphicsResource` object at a time owns the underlying GPU handle.

`GraphicsResource` itself does not hold a renderer pointer. The derived class is
responsible for declaring and managing `renderer_`.

---

## 2. When GPU Handles Are Released

GPU handles are released **when `Dispose()` is called**, not when the C++ object is
destroyed. Derived classes achieve this by overriding `Dispose(bool)`:

```cpp
void VertexBuffer::Dispose(bool disposing)
{
    renderer_.reset();                       // frees the GL/Vulkan/bgfx object
    GraphicsResource::Dispose(disposing);   // sets isDisposed_, fires events, unregisters
}
```

The destructor calls `Dispose(false)`, so if the user forgets to call `Dispose()` the
renderer is still freed eventually — but the `Disposing` event is **not** fired and
`ResourceDestroyed` is **not** raised in that path. Always call `Dispose()` explicitly.

### Override chain

| Class              | Calls                              |
|--------------------|------------------------------------|
| `VertexBuffer`     | `renderer_.reset()` → `GraphicsResource::Dispose(bool)` |
| `IndexBuffer`      | `renderer_.reset()` → `GraphicsResource::Dispose(bool)` |
| `Texture2D`        | `renderer_.reset()` → `Texture::Dispose(bool)` → `GraphicsResource::Dispose(bool)` |
| `RenderTarget2D`   | `renderer_.reset()` → `Texture2D::Dispose(bool)` → … |

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

1. All resource renderers are released **before** the device renderer is torn down.
   No GL/Vulkan/bgfx call is made against a destroyed context.
2. `RemoveResourceReference` is a no-op during device disposal (the list is already
   empty), so re-entrancy is safe.
3. The device renderer (`destroyNativeResources()`) is destroyed last.

If you destroy resources after their `GraphicsDevice` has been disposed, the GPU handles
are already gone; the C++ `Dispose()` call is still valid (it resets a null `unique_ptr`)
but no events are raised and `RemoveResourceReference` is not called (the device is gone).

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
- Disposing a `RenderTarget2D` that is currently bound as the active render target
  produces a framebuffer with a dangling attachment. Always unbind (call
  `SetRenderTarget(nullptr)`) before disposing.
- If the GL context is lost (window resize, driver crash), renderers may hold invalid
  IDs. Current EasyGL does not implement context-loss recovery; the safest approach is
  to dispose all resources and recreate from scratch.

### Vulkan

- Vulkan handles (`VkBuffer`, `VkImage`, `VkDeviceMemory`) are freed during renderer
  destruction. The logical device (`VkDevice`) must still be valid at that point.
- The disposal order guaranteed by `GraphicsDevice::Dispose()` (resources first, device
  renderer second) satisfies this requirement automatically.
- Destroying a `RenderTarget2D` whose image is referenced by an in-flight command buffer
  is undefined behavior. Ensure GPU work is complete (e.g. `vkDeviceWaitIdle`) before
  disposing render targets that were used in the previous frame.

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
| When is the GPU handle freed? | On `Dispose()`, not on C++ destructor |
| Is double-dispose safe? | Yes — `isDisposed_` guard makes it a no-op |
| What happens when the device is disposed? | All tracked resources are disposed first, then the device renderer |
| Do events fire on destructor path? | No — only on the `Dispose()` path |
| Does move transfer the tracking pointer? | No — avoid moving tracked resources to a different address |
| Can I use a resource after `Dispose()`? | No — `isDisposed_` is set; GPU handle is null |
