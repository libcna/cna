# ADR 0001: Portable modern-GPU ordering, synchronization and lifetime

- Status: Accepted
- Date: 2026-09-09
- Decision owner: `plans/plan_modern.md` `MOD-2202`
- Scope: CNA's native C++ API and C ABI; no language-specific binding design

## Context

CNA already presents XNA drawing, resource transfers, CNAEXT compute and indirect operations through
one `GraphicsDevice`, but its renderer families execute them differently. EasyGL, OpenGL4 and D3D11
mostly call an immediate context. Vulkan and WebGPU build deferred command streams. D3D12 and Metal
also require explicit submitted-work lifetime tracking. A portable resource API cannot expose one
of those implementation models or require an application to repair backend-specific hazards.

The implementation baseline is recorded in `docs/modern-gpu-baseline.md`. In particular, existing
`StorageBuffer`/`ComputeShader` objects are not `GraphicsResource`-tracked, the first Vulkan compute
slice submits synchronously, and an old lifetime document told Vulkan callers to use
`vkDeviceWaitIdle`. Those are measured transitional gaps, not contracts to preserve.
Subsequent `MOD-2229`/`MOD-2245` work made storage buffers tracked and proved deferred argument
retention through fence retirement; synchronous dispatch and full mixed-command ordering remain
the transitional parts owned by `MOD-2247`–`MOD-2253`.

## Decision

### 1. One observable device order

Every accepted public GPU operation on a `GraphicsDevice` belongs to the same observable order,
whether it is an XNA clear/draw/present, resource upload/copy/readback, compute dispatch, indirect
draw, timer/debug operation or a modern-resource operation. The order is the call order on the
device's graphics thread.

A renderer may batch, merge passes or record work later, but the result must be equivalent to that
ordered sequence. In particular:

- a call snapshots every mutable value/state needed by its operation before returning;
- a deferred record never reads a public wrapper, caller buffer or mutable state object later;
- an upload or state change issued after a draw cannot retroactively change that draw;
- a compute/copy result consumed by a later draw is visible without an application barrier;
- `Present` commits the preceding observable prefix without moving later work before it; and
- a synchronous readback observes the ordered producer prefix on which its requested result
  depends.

Independent internal work may overlap only when the renderer proves there is no observable data,
state or lifetime dependency. Phase 22 does not expose an asynchronous-compute queue or a second
public ordering domain.

### 2. Graphics-thread ownership

Ordinary `GraphicsDevice` and GPU-resource calls, including explicit `Dispose`, are single-threaded
and belong to the thread that owns the device's graphics context/command stream. CNA does not
promise concurrent resource mutation, binding, submission or disposal. Immutable CPU values may be
prepared elsewhere, but crossing into the device is serialized on its graphics thread.

A backend may internally make off-thread compilation, completion callbacks or retirement queues
safe. That is not permission for portable callers to invoke the public GPU API concurrently. If a
future API deliberately accepts worker-thread uploads, it must own/copy the input and define its
insertion point in this same order; it cannot create an implicit second stream.

### 3. Snapshot and retain, never borrow deferred public state

Public wrappers retain exclusive logical ownership. Renderer commands retain an internal native
resource record/token, not the public C++ object's address. That internal token may use reference
counting even though the public API does not.

At call acceptance a deferred backend must:

1. copy scalar descriptors, ranges, shader values, state and caller-provided bytes that will be
   read after the call returns; and
2. retain the native records for every referenced buffer, image/view, sampler, pipeline,
   descriptor and query until both CPU command recording and submitted GPU execution are finished.

An immediate backend may consume the same data before returning and therefore need no retained CPU
record. It still may not retain an unowned public pointer for later work.

### 4. Disposal is logically immediate and physically fence-safe

`Dispose()` immediately makes the public object disposed: it is removed from device tracking,
fires the established explicit-disposal events, detaches mutable bindings where necessary and all
future use throws the established disposed-object error. It does not cancel already accepted work.

Native destruction is a renderer decision:

- an unused/immediate object may be destroyed at once;
- an object referenced by a deferred CPU record stays alive until that record is consumed or
  discarded; and
- an object referenced by submitted GPU work is retired against that submission's completion token
  and destroyed/recycled only after completion.

Therefore normal resource disposal never calls a device-wide or queue-wide idle wait. Vulkan and
D3D12 use fence/timeline values; Metal retains records through command-buffer completion; WebGPU
retains records through submission/work-done completion; immediate GL/D3D11 paths use their native
ordered/reference-lifetime rules while keeping deletion on the owning context thread.

Device shutdown/recovery is an explicit terminal boundary. It may discard unsubmitted records,
wait for already submitted work when the native API still permits that wait, drain every retirement
bucket, then destroy renderer/device state. A lost device may instead abandon handles according to
that API's loss rules. Public resources must not outlive their `GraphicsDevice`.

### 5. Readback blocks only for its requested result

A synchronous CPU readback may submit/flush the dependency closure needed to produce the requested
resource, range and mip/layer, then wait for the narrow completion token that makes that copy
available. It must not use device-wide idle as a convenience and must not flush unrelated future
work. Earlier work on the same ordered queue may naturally complete first.

Backend mappings are:

- OpenGL: the required prior commands plus `glReadPixels`/buffer mapping on the owning context;
- D3D11: copy the requested subresource to staging and `Map` it;
- Vulkan/D3D12: record the requested copy, submit its ordered prefix and wait for that submission's
  fence value;
- Metal: blit to a CPU-visible buffer and wait for that command buffer; and
- WebGPU: copy to a mapped readback buffer, wait/pump only until that map completes.

Frame-slot reuse and presentation may apply bounded queue back-pressure. They still must not turn
routine disposal, upload, draw or dispatch into a global idle.

### 6. Reject from cached capability facts before native mutation

Portable validation happens before native allocation, descriptor update, command recording or
submission. This includes:

- disposed device/resource and cross-device ownership;
- invalid enum/flag combinations;
- zero, negative and overflowed dimensions/counts/ranges;
- declared resource usage and CPU-access intent;
- feature support, per-format usage support and numeric device limits; and
- compressed-block, mip, layer, alignment and binding-slot rules.

Backend discovery may query native properties when the device is created. Public calls consume the
cached truthful profile; they do not try a native operation and reinterpret a driver error as
feature discovery. If a valid native allocation later fails, the backend cleans up the partial
transaction and reports a backend/device failure without publishing a half-constructed resource.

### 7. Synchronization is renderer-owned

Public descriptors declare stable logical usage (for example sampled, storage read/write,
transfer source/destination, indirect or vertex/index use), and each operation declares its actual
logical access. The renderer maps the ordered previous/next use to its own barriers, transitions,
pass breaks and cache visibility.

The portable modern API exposes no Vulkan image layout, pipeline stage/access mask, D3D12 resource
state, Metal hazard mode, OpenGL barrier bit or WebGPU native usage object. Application code is not
required to call `ComputeShader::barrier` for ordinary correctness. The existing
`GraphicsMemoryBarrier` API remains a compatibility/explicit-ordering surface until its users are
migrated, but new typed resources must be correct from their declared usage and operation order
without it.

### 8. No native object leakage

Public resource descriptors and handles contain CNA value types only. Native device, queue,
command-buffer, buffer/image/view, descriptor, fence/event and state/layout values stay in renderer
modules. Test-only renderer introspection may expose counters or opaque integer diagnostics inside
renderer-owned test APIs; it is not portable application state and cannot be required to use a
resource correctly.

## Backend mapping

| Backend family | Ordered execution | Automatic synchronization | Safe native retirement |
|---|---|---|---|
| EasyGL / OpenGL4 | One current GL context; calls execute in public order. CNA-side batches snapshot their inputs. | Renderer selects `glMemoryBarrier` bits and pass/FBO boundaries from logical uses. | Delete on the context thread after CNA deferred records release; GL owns completion of already-issued uses. |
| D3D11 | One immediate context in public order; any deferred command list must retain its inputs. | Runtime hazard tracking plus renderer-owned unbind/copy rules; no public D3D state. | Keep COM references through command-list consumption/submission; release on the device thread. |
| Vulkan | One monotonic record stream on the selected graphics/compute queue for Phase 22. | Internal per-resource/subresource intent tracker emits Vulkan barriers/layout transitions and render-pass splits. | Reuse the existing frame-fence retirement queue; target readbacks wait a submission fence, never routine `vkDeviceWaitIdle`. |
| D3D12 | Direct command lists/queue preserve the same sequence. | Existing resource-state tracker expands to the new logical uses and emits transitions/UAV barriers. | Stamp resources/descriptors with the shared submitted fence value and recycle after `GetCompletedValue`. |
| Metal | Encode the sequence into command buffers on one queue. | Renderer chooses encoder boundaries and explicit synchronization where automatic hazard tracking is insufficient. | Hold strong internal records through command-buffer completion handlers/status. |
| WebGPU | Replay one ordered stream into encoders and submit on one queue. | Validate declared WebGPU usages; API transitions remain internal, with pass boundaries selected by the renderer. | Retain handles through command submission and, where required for reuse/destruction, queue work-done completion. |

## Consequences and required evidence

- New `GraphicsResource`-derived objects must be non-movable unless device tracking explicitly
  updates their registered address.
- Descriptor usage/access flags are immutable after construction. Expanding usage requires a new
  resource, avoiding backend-dependent reallocation and invalidating no queued snapshot.
- Backend support stays false/zero/unknown until allocation, use, synchronization, disposal and
  validation tests all pass. Native feature bits alone remain insufficient.
- Tests must dispose each modern resource immediately after enqueue, mix XNA and modern calls in
  one frame, mutate source state after enqueue, read back a dependency closure, and verify bounded
  retirement without routine global waits.
- `docs/graphics-resource-lifetime.md` describes the public wrapper/event rules; this ADR is
  normative when that overview or an older renderer note conflicts with it.

Implementation is intentionally split across the later Phase 22 rows. Accepting this ADR does not
claim that the transitional synchronous Vulkan compute path or every existing renderer resource
already satisfies it.
