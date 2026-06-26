# GraphicsResource — FNA Audit

**FNA source**: `FNA/src/Graphics/GraphicsResource.cs`  
**CNA source**: `include/Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp` + `src/…/GraphicsResource.cpp`

---

## API Coverage

| FNA member | CNA equivalent | Status | Notes |
|---|---|---|---|
| `GraphicsDevice GraphicsDevice { get; internal set; }` | `getGraphicsDeviceProperty()` | ⚠️ Partial | Getter present; no setter; no device resource-list registration (see gap 1) |
| `bool IsDisposed { get; }` | `getIsDisposedProperty()` | ✅ | |
| `string Name { get; set; }` (virtual) | `getNameProperty()` / `setNameProperty()` (virtual) | ✅ | FNA uses `protected string _Name`; CNA uses `protected name_` — functionally identical |
| `object Tag { get; set; }` | `getTagProperty()` / `setTagProperty()` | ✅ | CNA Tag is `System::Object*` (raw pointer); caller retains ownership |
| `event EventHandler<EventArgs> Disposing` | `System::EventHandler<EventArgs> Disposing` | ✅ | |
| `void Dispose()` | `void Dispose()` | ✅ | |
| `protected virtual void Dispose(bool)` | `protected virtual void Dispose(bool)` | ✅ | Event-before-flag ordering fixed in Task 211 (see fix 2) |
| `string ToString()` | `std::string ToString() const override` | ✅ | Added in Task 211 (see fix 1) |
| `internal protected virtual void GraphicsDeviceResetting()` | — | ❌ | Not implemented; blocked by gap 1 |
| `internal protected virtual bool IsHarmlessToLeakInstance` | — | — | Debug/GC hint; not needed in C++ |
| `~GraphicsResource()` (finalizer) | `~GraphicsResource()` | ⚠️ | CNA destructor always calls `Dispose(false)`; FNA only does so when device is non-null and non-disposed |

---

## Intentional C++ Deviations

| Deviation | Reason |
|---|---|
| Copy constructor / copy-assignment added | C# resources are reference types; in C++ we may copy-construct resource metadata while resetting disposal state and event handlers |
| No `GC.SuppressFinalize` call in `Dispose()` | No GC in C++; destructor lifetime is deterministic |
| `Tag` type is `System::Object*` instead of `System.Object` (reference) | C++ has no unified managed reference type; raw pointer with caller-owned lifetime is the closest equivalent |
| FNA finalizer guard (`!IsDisposed && graphicsDevice != null && !graphicsDevice.IsDisposed`) not replicated | C++ destructors always run; the `isDisposed_` guard inside `Dispose(bool)` is sufficient |

---

## Gaps (not yet implemented)

### Gap 1 — Device resource tracking

FNA's `GraphicsDevice` property setter (`internal set`) registers and unregisters resources
in a per-device list (`AddResourceReference` / `RemoveResourceReference`). This list is used by:

- `GraphicsDevice.Dispose()` — to dispose every tracked resource when the device is destroyed.
- `GraphicsDevice.Reset()` — to call `GraphicsDeviceResetting()` on every tracked resource,
  allowing them to release GPU handles before the device is recreated.

**CNA status**: No resource list exists on `GraphicsDevice`. Resources are not notified on
device reset or device disposal.

**Impact**: Low in current test-driven usage where the device outlives all resources.
Will matter once device-reset / multi-device scenarios are exercised.

**Tracking**: Task 211 documents; Task 212 is the correct milestone to address (it adds
correct `GraphicsResource` inheritance to `VertexBuffer` / `IndexBuffer`, which would also
need to integrate with the resource list).

### Gap 2 — `GraphicsDeviceResetting()` callback

FNA exposes `internal protected virtual void GraphicsDeviceResetting()` as the hook for
resources to invalidate GPU handles before a device reset. CNA has no equivalent.

**Tracking**: Depends on Gap 1 (no resource list → no way to invoke the hook).

---

## Fixes Applied in Task 211

### Fix 1 — `ToString()` override

FNA: `return string.IsNullOrEmpty(Name) ? base.ToString() : Name;`

CNA before: `System::Object::ToString()` always returns `GetTypeName()`, ignoring the name.

CNA after: `GraphicsResource::ToString()` returns `name_` if non-empty, else `Object::ToString()`.

### Fix 2 — `Dispose(bool)` event-before-flag ordering

FNA raises the `Disposing` event *before* setting `IsDisposed = true`. This allows event
handlers to inspect the still-live resource state.

CNA before: set `isDisposed_ = true` first, then raised the event. An event handler that
checked `getIsDisposedProperty()` on the same resource would see `true` prematurely.

CNA after: event is raised first, `isDisposed_ = true` is set after — matching FNA.
