# Graphics resource lifetime rules

This document describes how GPU resources are created, tracked, and destroyed in CNA.
It applies to `Texture2D`, `VertexBuffer`, `IndexBuffer`, `RenderTarget2D`, and any
future class that inherits `GraphicsResource` and holds a renderer handle.

This is the current source of truth, reconciled against the implementation and regressions on
2026-10-31. The earlier per-class FNA audit was retired because its pre-tracking gap analysis and
FNA-derived disposal ordering no longer described CNA or measured Microsoft XNA 4.0 behavior.

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
   disposal never waits for the whole device or queue: WebGPU uses work-done completion, while GL
   relies on its native ordering and reference lifetime and deletes on the owning context thread. Device shutdown is the one terminal
   boundary. It may discard unsubmitted records, wait for submitted work and drain retirement before
   destroying the device. A lost device may abandon handles under its API's loss rules.

5. **Readback blocks only for its result.** A synchronous readback submits the work that its
   requested resource, range and mip need, then waits for that copy's completion. It never waits for
   device-wide idle and flushes no unrelated later work:
   - GL: the prior commands, then `glReadPixels` or buffer mapping on the owning context;
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
   native layout, stage mask, hazard mode, GL barrier bit or WebGPU usage object, and the
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

### Public metadata and base behavior

- `GraphicsDevice` is a non-owning pointer to the device that tracks the resource.
- `Name` is an ordinary string. `ToString()` returns `Name` when non-empty and otherwise the
  resource's type name.
- `Tag` is a non-owning `System::Object*`; the caller retains the pointed-to object's lifetime.
- `IsDisposed` is false initially and becomes true exactly once. A second `Dispose()` is a no-op.

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

The base disposal order follows measured Microsoft XNA 4.0 behavior, which differs from FNA:

1. publish `IsDisposed == true`;
2. on explicit disposal, raise `Disposing` with the resource as sender;
3. raise the owning device's `ResourceDestroyed` event;
4. remove the resource from device tracking.

Publishing the state first makes recursive disposal harmless. If either callback throws, CNA still
removes the tracking entry before propagating the exception, and a later `Dispose()` does not raise
the event again. The permanent XNA-over-FNA decision and regression are recorded in
`xna-fna-behavior-decisions.md`.

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

### What `GraphicsDevice::Dispose()` does to the resources it tracks

It releases them, it does not dispose them (HND-TQ-47 R2, owner decision 2026-11-09). Refresh
`'!GraphicsDevice'` IL_0012-0020 calls `DeviceResourceManager::ReleaseAllDeviceResources`, whose
IL_009e-00a7 call each tracked child's `ReleaseNativeObject(false)`; FNA disposes every child instead.
CNA follows XNA:

```cpp
isDisposed_ = true;
ReleaseTrackedResourcesForTeardownInternal();   // each child: ReleaseNativeResourcesForDeviceTeardown()
if (disposing) Disposing.Raise(...);            // '~GraphicsDevice' IL_0008-0021: after the release
destroyNativeResources();                       // SDL context, GPU device, etc.
```

This means:

1. Every tracked resource's renderer objects are released **before** the device renderer is torn
   down, under the device's own context lease. The list is taken first, so a later
   `RemoveResourceReference` is a no-op.
2. The resource itself is **not** disposed: `IsDisposed` stays false, and neither its `Disposing`
   nor the device's `ResourceDestroyed` is raised (`ReleaseAllReferences(handle, false)` IL_0040-0041
   skips `FireDestroyedEvent`). `GraphicsDevice` still returns the device.
3. A later use refuses the way XNA's `Helpers.CheckDisposed(this, pComPtr)` refuses a null native
   pointer: `ObjectDisposedException` from every texture/buffer `SetData`/`GetData`, `EffectPass.Apply`,
   the `CurrentTechnique` setter, `Effect::Clone`, `EffectParameter::SetValue(Texture)` and the
   texture-collection setter (`GraphicsResource::IsNativeObjectReleasedInternal()`). OcclusionQuery
   behaves as after its own disposal (TQ-035: `Begin`/`End` throw `NullReferenceException`).
4. A later `Dispose()` is safe: there is nothing native left to release, `ResourceDestroyed` is not
   raised, `IsDisposed` becomes true and `Disposing` is raised once (never for Effect/OcclusionQuery).
5. The device's `Disposing` handlers observe that released state. The renderer itself is destroyed
   after the event, so CNA-internal subscribers can still release renderer-side state; a handler that
   throws does not stop the teardown (its exception is rethrown afterwards).

A resource may therefore outlive the `GraphicsDevice` object: its own `Dispose(bool)` and destructor
consult the non-owning device lifetime token before touching the device. Escaping device tracking
(including the move caveat below) still means its renderer objects are not released with the device.

---

## 5. ResourceCreated / ResourceDestroyed Events

`GraphicsDevice` exposes two events:

```cpp
System::EventHandler<ResourceCreatedEventArgs>   ResourceCreated;
System::EventHandler<ResourceDestroyedEventArgs> ResourceDestroyed;
```

- `ResourceCreated` is raised in the `GraphicsResource` constructor, **after**
  `AddResourceReference`.
- `ResourceDestroyed` is raised in `GraphicsResource::Dispose(bool)`, after the disposed state is
  published and **before** `RemoveResourceReference`.

Both events are skipped if no handlers are subscribed (`EventHandler::Empty()` check).

`ResourceDestroyedEventArgs` carries the resource's `Name` string and `Tag` pointer at the time of
disposal. Publishing `IsDisposed` does not erase either value.

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

`GraphicsResource` transfers the device's tracking entry from the source address to the destination
address. The moved-from base is detached from its device, clears its non-owning tag and becomes an
inert disposed wrapper, so its destructor cannot unregister or dispose the live destination.

Move assignment across devices first unregisters and unbinds the overwritten destination, then
transfers the source tracking entry and applicable buffer, sampler, texture or target bindings to
the destination address. Overwriting a currently bound render target is refused before mutation,
because replacing that wrapper during an active target binding cannot preserve the public contract.
`GraphicsResourceTest.CrossDeviceMoveAssignmentTransfersTrackingAndDetachesOldBindings` and the
shared renderer lifetime tests hold these rules.

A new `GraphicsResource`-derived class must implement equivalent binding transfer or delete its move
operations; default address-based moves are unsafe.

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
- EasyGL's resource registry discards stale native names on context loss and recreates registered
  objects after restoration. Textures, including cube and volume mip chains, restore retained CPU
  uploads; render-target allocation recovery does not promise preservation of rendered pixels.
  A normal window resize is separate from context loss. Custom `ShaderEffect` programs remain
  outside this registry; their recovery boundary is recorded in
  [the effect audit](ieffect-renderer-audit.md).

---

## 9. Quick Reference

| Question | Answer |
|---|---|
| When does public ownership end? | On `Dispose()`, with destructor fallback |
| When is the native handle freed? | Immediately if unused, otherwise after the renderer's completion token/fence |
| Is double-dispose safe? | Yes — `isDisposed_` guard makes it a no-op |
| What happens when the device is disposed? | Every tracked resource's native object is released, then `Disposing` is raised, then the device renderer goes; the resources stay undisposed (XNA `ReleaseAllDeviceResources`) |
| Do events fire on destructor path? | `ResourceDestroyed` does; `Disposing` does not |
| Does move transfer the tracking pointer? | Yes — tracking and applicable bindings move to the destination address |
| Can I use a resource after `Dispose()`? | No — `isDisposed_` is set; GPU handle is null |
| Can I use a resource after its device's `Dispose()`? | No — transfers, effect application and cloning throw `ObjectDisposedException`; `Dispose()` itself is safe |
| Must the caller wait for the GPU before disposal? | No — fence/completion-safe retirement is renderer-owned |
