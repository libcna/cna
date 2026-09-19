# CNA diagnostics and profiler

Status: current for DIAG-0001 (2026-09-17).

## Purpose and boundary

`modules/diagnostics` is CNA's renderer-independent observation layer. Engine code writes cheap
statistics or bounded events into the diagnostics core; tools pull snapshots and event batches
through `CNA::Diagnostics::IDiagnosticsProvider`. This keeps producers independent from any
transport or user interface.

The module uses only the C++ standard library. It does not start a thread and has no dependency on
a renderer, profiler SDK, HTTP/WebSocket stack, GUI toolkit, or browser technology. An Inspector
can be added later as a consumer without changing engine instrumentation.

```text
Game / GraphicsDevice / GraphicsResource / audio / application instrumentation
                              |
                              v
                CNA::Diagnostics process core
                    |          |          |
                    v          v          v
                snapshots   events    bounded trace
                    \          |          /
                     future consumer or Inspector
```

Diagnostics are a CNA extension in `CNA::Diagnostics`; no XNA-compatible behavior or public
Microsoft namespace semantics change. DIAG-0001 deliberately does not add a C ABI. The existing
C ABI therefore remains unchanged. A future out-of-process consumer should put a versioned wire
format above the provider rather than exposing C++ objects across a process boundary.

## Build and runtime modes

Configure with `-DCNA_DIAGNOSTICS=OFF`, `STATS`, or `FULL`. The default is `OFF`. The selected
value becomes the public integer compile definition `CNA_DIAGNOSTICS_LEVEL` (0, 1, or 2), so the
engine and an application that links CNA compile the same instrumentation surface.

| Mode | Retained functionality | Cost model |
|---|---|---|
| `OFF` | No metrics, resources, histories, zones, events, or recordings | Engine macros expand to do-nothing statements without evaluating arguments. Conditional graphics resource state is absent. No state object, thread-local buffer, thread, lock, allocation, traversal, polling, or serialization occurs. |
| `STATS` | Metrics, frame timing/FPS, 240-frame history, resource metadata, frame-boundary sources | Metric registration allocates once. Updates are relaxed atomic operations. Frames scan at most 512 registered metric slots; resource lifetime operations use the resource-registry lock. |
| `FULL` | Everything in STATS plus hierarchical zones, markers, resource events, per-thread streams, and recording/export | The first event on a thread allocates and registers one fixed-capacity context. Later producer events use thread-local state without a global hot-path lock or allocation. Merging happens at a frame boundary or explicit consumer read. |

`SetRuntimeMode` can lower a STATS or FULL build to any compiled mode. Raising it above the build
mode fails. A runtime mode change invalidates open zones, so re-enabling FULL cannot accidentally
close a zone across a disabled interval. Build-time OFF is the zero-cost guarantee; runtime OFF in
an enabled build still leaves previously initialized diagnostics storage in the process. Resources
created while runtime mode is OFF are intentionally not discovered retroactively when statistics
are re-enabled; newly created resources resume normal registration.

There is no background polling in any mode. Optional sources are invoked synchronously only from
`EndFrame`, and only while statistics are active.

## Instrumentation API

Include `CNA/Diagnostics/Instrumentation.hpp` for compile-time instrumentation:

```cpp
void Simulate()
{
    CNA_PROFILE_SCOPE_CATEGORY("Physics/Simulate", CNA::Diagnostics::Category::Update);
    CNA_DIAGNOSTICS_FRAME_COUNTER_ADD("Physics/Contacts", contacts.size());
    CNA_DIAGNOSTICS_GAUGE_SET("Physics/ActiveBodies", activeBodyCount);
}
```

Available macros are:

- `CNA_PROFILE_SCOPE(name)` and `CNA_PROFILE_SCOPE_CATEGORY(name, category)` in FULL;
- `CNA_DIAGNOSTICS_EVENT(name)` and `CNA_DIAGNOSTICS_EVENT_CATEGORY(name, category)` in FULL;
- `CNA_DIAGNOSTICS_COUNTER_ADD`, `CNA_DIAGNOSTICS_GAUGE_SET`,
  `CNA_DIAGNOSTICS_GAUGE_ADD`, and `CNA_DIAGNOSTICS_FRAME_COUNTER_ADD` in STATS and FULL;
- `CNA_DIAGNOSTICS_FRAME_SCOPE()` in STATS and FULL.

Names at macro sites are registered once through function-local static handles; no dynamic string
allocation occurs on subsequent hot-path calls. Direct handles (`CounterHandle`, `GaugeHandle`,
`FrameCounterHandle`, and `NameHandle`) are available for code that needs explicit ownership.
`BeginZone`/`EndZone` provide manual timing. A token must end on its producer thread. Out-of-order,
duplicate, wrong-thread, and depth-overflow uses are contained and reported rather than corrupting
the stack. The recommended API remains `ZoneScope` or the scope macro.

Counters are cumulative, gauges retain the latest value, and frame counters are exchanged to zero
when a frame ends. Metric names should be stable, globally unique slash-separated literals.
Repeating an identical registration reuses its ID; reusing a name with a conflicting kind, unit,
or accuracy returns an inert handle. The registry supports 512 metrics, of which up to 64 frame
counters are copied into each historical frame. The current value of every registered metric
remains available in a snapshot.

## Frames and built-in engine metrics

Native `Game::Tick` and the Emscripten main-loop callback publish one frame. A completed sample
contains its monotonically increasing frame number, monotonic start timestamp, elapsed duration,
FPS,
and frame counters. FULL additionally records `Game/Tick`, `Game/Update`, and `Game/Draw` zones.
The top-level tick measurement includes fixed-step pacing waits; the Update and Draw zones isolate
the corresponding application/engine work.

The common engine seams currently publish these renderer-independent metrics:

| Name | Kind and accuracy | Meaning |
|---|---|---|
| `Runtime/UpdateCount` | frame counter, exact | `Game::Update` calls in the tick; may exceed one for fixed-step catch-up. |
| `Runtime/DrawCount` | frame counter, exact | Successful `BeginDraw` paths that reached `Draw` and `EndDraw`. |
| `Graphics/DrawCalls` | frame counter, exact | Successful common `GraphicsDevice` draw submissions. |
| `Graphics/IndexedDrawCalls` / `NonIndexedDrawCalls` | frame counter, exact | Draw-call split at the common device layer. |
| `Graphics/IndirectDrawCalls` | frame counter, exact | Indirect submissions; their GPU-resolved primitive count is not claimed. |
| `Graphics/SubmittedPrimitives` | frame counter, exact for direct calls | Direct primitive count, multiplied by instance count for instanced draws. Indirect calls contribute zero because CNA does not read their GPU argument buffer. |
| `Graphics/RenderTargetChanges` | frame counter, exact | Actual binding-set transitions after validation and renderer acceptance; redundant sets are excluded. |
| `Graphics/EffectChanges` | frame counter, exact | Changes to the common device's current effect pointer. |
| `Graphics/TextureBindingChanges` | frame counter, exact | Texture-slot pointer changes in the common collection. |
| `Graphics/SpriteSubmissions` | frame counter, exact at the SpriteBatch layer | Sprites accepted into SpriteBatch, not native draw calls. |
| `Audio/AllocatedVoices` | gauge, exact at the mixer facade | Mixer tracks CNA has allocated and not logically destroyed. This is not the number currently audible. |
| `Audio/VoiceCreations` | counter, exact | Successful mixer-track creations. |

Draw metrics are updated after the renderer call, so a rejected or throwing submission is not
reported as completed. Instrumentation is in `GraphicsDevice`, not duplicated across renderer
families.

## Resource metadata and memory scope

In STATS/FULL, graphics resources receive stable, process-local, non-reused diagnostic IDs.
Snapshots expose kind, label, numeric format identifier, dimensions, mip count, byte value, and an
`Accuracy` classification. Registration and unregistration follow RAII, including shared
GraphicsResource identities, and never trigger a GPU readback.

- Vertex and index buffer bytes are exact logical capacities requested through CNA.
- Texture and render-target bytes are estimated declared pixel/block payloads across dimensions,
  faces, and mip levels. They exclude native row alignment, depth/stencil companions, multisample
  storage, resolve images, sparse residency, compression chosen by a driver, and driver-private
  allocations.
- `registeredResourceBytes` is therefore a mixed aggregate of exact logical buffer bytes and
  estimated texture payload. It is not total GPU memory and is not total process memory.
- `profilerOwnedBytes` is a conservative lower-bound estimate covering fixed core storage and live
  per-thread event contexts; allocator and container overhead is not claimed.

CNA does not replace global `new`/`delete`, inspect a platform heap, or claim total application
allocation statistics. A custom system can register metadata through `ResourceHandle` without
giving the diagnostics core ownership of the underlying resource.

## Provider and optional sources

`GetProvider()` returns a process-lifetime, pull-only `IDiagnosticsProvider`.
`IDiagnosticsProvider::InterfaceVersion` is 1. `CaptureSnapshot()` returns owned metrics, recent
frames, resources, drop counts, malformed-usage counts, and diagnostics-memory metadata.
`ReadEvents(afterSequence, maximumEvents)` implements a cursor over the bounded FULL history and
reports both history gaps and producer drops. `ResolveName` resolves interned event names.

Consumers allocate their own returned vectors and should poll at a rate appropriate for a debug
tool. No consumer callback runs on an instrumentation path. A future Inspector should use one
consumer thread, keep the latest event sequence as its cursor, and serialize only in that thread.

`IDiagnosticsSource` is the extension point for renderer- or subsystem-specific statistics.
Sources pre-register handles and publish already-available data through `FrameStatisticsSink`.
Collection copies source ownership before invoking callbacks, so a source may unregister itself
without deadlocking the registry. A GPU source must poll asynchronous queries and report an
unavailable/stale result; it must never wait for the device or queue in `Collect`.

No renderer-neutral GPU timer is forced by DIAG-0001. Native timestamp APIs have different query,
frequency, reset, and availability rules, and synchronously normalizing them would risk a GPU
stall. The source contract is the supported future integration seam.

## Thread and buffer model

FULL uses one single-producer/single-consumer event buffer per producing thread. Its capacity is
1,024 records. The producer owns zone nesting and the write index; a consumer advances the atomic
read index. Once initialized, a producer does not acquire the global history mutex, allocate, or
intern a name. New events are dropped when that thread's buffer is full, preserving already
published records. Drops are observable.

Frame boundaries and explicit provider reads merge thread buffers into a 32,768-record process
ring. The oldest process event is overwritten when full, with an overwrite counter and sequence
gap available to consumers. There is no giant global profiler lock on zone entry/exit.

Metric updates use relaxed atomics so counters are correct across worker threads without imposing
frame-order synchronization on callers. This costs more than a plain integer but avoids the much
larger correctness and lifecycle surface of unsynchronized per-thread statistic shards. FULL
zones and markers use thread-local accumulation because that is the materially hotter event path.

## Recording and trace formats

`StartRecording(maximumEvents)` creates a cursor, not a background writer. `StopRecording` drains
available thread buffers and copies at most the requested number of newest events, clamped to the
32,768-event history. Overflow drops older records explicitly and increments the trace's dropped
count. Recording cannot grow without bound.

`Trace::WriteBinary` writes version 1 of `CNATRACE`, always little-endian:

```text
8 bytes  magic "CNATRACE"
u16      version (1)
u16      reserved (0)
u32      name count
u64      event count
u64      dropped event count
repeat names:  u32 id, u32 UTF-8 byte length, bytes
repeat events: seven u64 fields, i64 value, u32 name, u8 kind, u8 category, u16 reserved
```

The seven event integers are sequence, timestamp ns, duration ns, frame number, thread ID,
correlation ID, and parent correlation ID. The reader rejects bad magic/version, truncation,
invalid enums, more than 65,536 names, a name longer than 1 MiB, more than 16 MiB of name data, or
more than 32,768 events.

`WriteChromeTrace` is an offline streaming converter. It writes directly to the destination stream
and never constructs a frame-sized or trace-sized JSON document in memory. CNA does not serialize
JSON every frame.

## Inspector integration

The optional Inspector implemented in `modules/inspector` remains a version-1 consumer:

1. Check provider interface version 1.
2. Pull `Snapshot` at a modest UI refresh rate and `EventBatch` with a retained sequence cursor.
3. Surface accuracy and unavailable states verbatim; do not relabel texture estimates as GPU
   allocation or allocated mixer tracks as audible voices.
4. Detect `eventsDroppedBeforeStart`, `producerEventsDropped`, and
   `eventHistoryOverwrites`, then show a discontinuity instead of inventing continuity.
5. Transport, authentication, resource previews, and browser assets remain outside the diagnostics
   core. Resource previews use a separately authorized readback path and are not metadata.
6. Keep GPU query implementations renderer-local behind `IDiagnosticsSource`; never wait in the
   core or the Inspector transport.

See [`inspector.md`](inspector.md) for activation, protocol, UI, security, limits, platform status,
and its explicit preview source. The Inspector did not require a diagnostics provider change.

## Known limitations

- There is no total process-memory, heap-allocation, or native-driver-memory measurement.
- GPU timings are an extension seam only; no renderer publishes them yet.
- Indirect primitive counts are unavailable without reading the argument buffer.
- Texture format metadata is currently the stable numeric enum value, not a display name.
- Metrics have fixed process limits (512 total and 64 frame-history values); registrations beyond
  the total limit return an inert handle.
- Resources created while an enabled build is lowered to runtime OFF are not retroactively added
  when diagnostics are re-enabled.
- Timestamps use `steady_clock` and are comparable only within one process run.
- The provider is an in-process C++ contract. No C ABI or out-of-process transport is included.

See [`diagnostics-benchmark.md`](diagnostics-benchmark.md) for the reproducible performance gate
and measured results.
