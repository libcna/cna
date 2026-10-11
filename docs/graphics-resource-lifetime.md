# Graphics Resource Lifetime Rules

This document describes how GPU resources are created, tracked, and destroyed in CNA.
It applies to `Texture2D`, `VertexBuffer`, `IndexBuffer`, `RenderTarget2D`, and any
future class that inherits `GraphicsResource` and holds a renderer handle.

Section 1 states the rules every renderer follows for ordering, deferred-command retention,
synchronization and native-handle retirement. Where an older renderer note disagrees, section 1
wins. The remaining sections describe the public wrapper/event mechanics and the current renderer
paths.

---

## 1. Ordering, Synchronization and Native Retirement

1. **One observable device order.** Every accepted GPU operation on a `GraphicsDevice` (clear,
   draw, present, upload, copy, readback, query) belongs to one order: the call order on the
   device's graphics thread. A renderer may batch, merge passes or record work later, but the
   result must equal that sequence:
   - a call snapshots every mutable value and state its operation needs before returning;
   - a deferred record never reads a public wrapper, caller buffer or state object later;
   - an upload or state change issued after a draw cannot change that draw;
   - `Present` commits the preceding work without moving later work before it;
   - a synchronous readback observes all earlier work its result depends on.

   Internal work may overlap only when no observable data, state or lifetime dependency exists.

2. **Graphics-thread ownership.** `GraphicsDevice` and resource calls, including `Dispose`, are
   single-threaded and belong to the thread that owns the device's context or command stream. CNA
   promises no concurrent mutation, binding, submission or disposal. CPU values may be prepared
   elsewhere. A renderer's internal off-thread compilation or retirement does not make the public
   API thread-safe.

3. **Snapshot and retain, never borrow.** Public wrappers keep exclusive ownership. Renderer
   commands retain an internal native record, never the public object's address. A deferred
   renderer copies descriptors, ranges, shader values, state and caller bytes when it accepts a
   call, and keeps every referenced buffer, texture, sampler, pipeline, descriptor and query alive
   until both recording and GPU execution have finished. Descriptor caches evict every entry that
   names a dying native handle before that handle can be recycled. An immediate renderer may consume
   the data before returning, but it still never keeps an unowned pointer.

4. **Disposal is logically immediate and physically fence-safe.** `Dispose()` makes the object
   disposed at once (sections 3–5) but does not cancel accepted work. The native object is
   destroyed at once if nothing uses it, kept while a deferred CPU record references it, and retired
   against the submission's completion token while submitted GPU work references it. Routine
   disposal never waits for the whole device or queue: Vulkan uses fence values, Metal command-buffer
   completion and WebGPU work-done completion, while GL and D3D11 rely on their native ordering and
   reference lifetime and delete on the owning context thread. Device shutdown is the one terminal
   boundary. It may discard unsubmitted records, wait for submitted work and drain retirement before
   destroying the device. A lost device may abandon handles under its API's loss rules.

5. **Readback blocks only for its result.** A synchronous readback submits the work that its
   requested resource, range and mip need, then waits for that copy's completion. It never waits for
   device-wide idle and flushes no unrelated later work:
   - GL: the prior commands, then `glReadPixels` or buffer mapping on the owning context;
   - D3D11: a copy to a staging resource, then `Map`;
   - Vulkan: the copy is recorded, its ordered prefix submitted, and its fence awaited;
   - Metal: a blit to a CPU-visible buffer, then a wait for that command buffer;
   - WebGPU: a copy to a mappable buffer, pumping only until that map completes.

   Frame-slot reuse and presentation may apply bounded back-pressure. Disposal, upload and draw
   must never turn into a global wait.

6. **Validate before touching native state.** A call is rejected before any native allocation,
   descriptor update, recording or submission when it uses a disposed object or another device's
   resource, invalid enums or flags, zero, negative or overflowing sizes and ranges, an undeclared
   usage, an unsupported format, a value beyond a device limit, or a broken alignment, mip or slot
   rule. Calls consult the capability profile cached when the device was created. They never try a
   native call and treat a driver error as feature discovery. A capability stays unreported until
   allocation, use, synchronization, disposal and validation tests pass; a native feature bit alone
   is not enough. If a valid allocation still fails, the renderer cleans up and reports the failure
   without publishing a half-built resource.

7. **Synchronization belongs to the renderer.** The renderer derives barriers, layout transitions,
   pass breaks and cache visibility from declared usage and call order. The public API exposes no
   Vulkan layout or stage mask, Metal hazard mode, GL barrier bit or WebGPU usage object, and the
   application never needs one for correctness.

8. **No native objects in the public API.** Public descriptors and handles hold CNA value types
   only. Native device, queue, command-buffer, buffer, image, view, descriptor, fence and layout
   values stay inside renderer modules. Renderer test introspection may expose counters, but it is
   never needed to use a resource correctly.

---

## 2. Ownership Model

Every GPU-backed resource owns a renderer-side object, held as a
`std::unique_ptr<IXxxRenderer>` (for example `IVertexBufferRenderer` or `ITexture2DRenderer`).

`GraphicsResource` itself does not hold a renderer pointer. The derived class is
responsible for declaring and managing `renderer_`.

The renderer object may hand its native handles to an internal retained record before the public
wrapper releases it. That renderer-internal record is not a second public owner: it exists only so
a command accepted before `Dispose()` cannot dereference a dead wrapper or free a handle still in
flight.

---

## 3. When Public and Native Ownership End

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

---

## 4. GraphicsDevice Tracking List

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

## 5. ResourceCreated / ResourceDestroyed Events

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

## 6. Move Semantics

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

A new `GraphicsResource`-derived class should delete both move operations unless device tracking
updates its registered address.

---

## 7. Resources Without a Device

Some resources (e.g. `BlendState`, `SamplerState`) may be constructed without a
`GraphicsDevice`. In that case:

- `graphicsDevice_` is `nullptr`.
- `AddResourceReference` and `OnResourceCreated` are not called.
- `RemoveResourceReference` and `OnResourceDestroyed` are not called on Dispose.
- No tracking list entry is created.

---

## 8. Renderer-Specific Caveats

### EasyGL

- GL object IDs are freed by the renderer destructor (`glDeleteTextures`,
  `glDeleteBuffers`, `glDeleteFramebuffers`). This must happen while the SDL/OpenGL
  context is current.
- GL calls execute immediately in public order. A resource that needs a CNA-side deferred record
  detaches its current binding and retains that record on disposal itself; callers never have to
  unbind first.
- If the GL context is lost (window resize, driver crash), renderers may hold invalid
  IDs. Current EasyGL does not implement context-loss recovery; the safest approach is
  to dispose all resources and recreate from scratch.

### Vulkan

- Vulkan handles (`VkBuffer`, `VkImage`, `VkDeviceMemory`) are freed during renderer
  destruction or the renderer's frame-fence-gated retirement pass. The logical device
  (`VkDevice`) is still valid at that point.
- The disposal order guaranteed by `GraphicsDevice::Dispose()` (resources first, device
  renderer second) satisfies this requirement automatically.
- Textures, buffers, effects, render targets and queries detach deferred wrapper pointers and
  retire referenced native handles after the consuming frame fence. Applications must not call
  `vkDeviceWaitIdle` (nor can they access the native device).

### SDL_Renderer

- `SDL_Texture` objects are destroyed with `SDL_DestroyTexture`. The `SDL_Renderer`
  must still exist at that point.
- The disposal order in `GraphicsDevice::Dispose()` (textures first, renderer second)
  satisfies this requirement.

---

## 9. Quick Reference

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
