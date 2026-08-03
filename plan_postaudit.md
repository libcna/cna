# CNA Post-Audit Development Plan

**Created:** 2026-08-03, on `feature/audit`, immediately after `REMED-GFX-202` closed.
**Owns:** `REMED-GFX-203` … `REMED-GFX-213` and any future finding admitted by the rules below.
**Does not own:** the remediation campaign. `remediation/REMEDIATION_INDEX.md` and
`remediation/REMEDIATION_PROGRESS.md` remain the authoritative status of record for every ticket,
including the ones scheduled here.

---

## 1. Why this file exists

The intensive post-audit remediation campaign is **milestone-bounded, not backlog-bounded.** It ends
at a defined checkpoint, not when the ticket count reaches zero. Nothing in the campaign's design
ever promised that every descendant of every closed ticket would be implemented before the
checkpoint, and treating it that way is what turned `REMED-GFX-201` and `REMED-GFX-202` into an
automatically self-extending chain: two closed tickets produced eleven open descendants
(`REMED-GFX-203` … `REMED-GFX-213`), of which six are separate per-backend native implementations.

`REMED-GFX-201` and `REMED-GFX-202` together delivered the piece that actually had to be shared:

- **one** fixed-capacity, immutable vertex-stream representation
  (`std::array<GpuVertexStreamBinding, 16>` + `vertexStreamCount` + `combinedVertexStride` inside
  `GpuDrawParams`), captured by value, used by `DrawPrimitives`, `DrawIndexedPrimitives` **and**
  `DrawInstancedPrimitives` through the same `FillVertexStreamBindings`;
- complete shared validation — per-stream vertex ranges, per-instance record ranges over every
  per-instance stream, semantic `(usage, usageIndex)` composition across both rates, argument
  validation ordered strictly **before** the capability gate;
- a full ordinary multi-stream reference implementation on **EasyGL** and **Software**;
- a full mixed-frequency instanced reference implementation on **EasyGL**;
- and, on every backend whose native input-layout model is still single-stream, a **deterministic
  capability rejection before native submission** instead of silent truncation.

That last point is the load-bearing one for scheduling. It is the difference between *"this backend
does not do multi-stream yet, and says so"* and *"this backend renders the wrong thing and says
nothing"*. The former can be scheduled. The latter cannot.

CNA now needs capacity for work the remediation campaign has been crowding out:

- integration of **19** existing Git branches;
- stabilization of the integrated tree;
- decomposition / modularization;
- NoXNA graphics extensions;
- continuing backend development.

**Moving a ticket into this plan does not deny the finding, weaken its evidence, or close it.** Every
ticket routed here keeps its original text, its original evidence, its original severity of record
and its original ID in `remediation/`. What changes is only *when it is expected to be done, and by
what trigger.*

### Four states this document must keep distinguishable

| State | Meaning | Example here |
|---|---|---|
| **Known and planned** | Recorded, evidenced, scheduled, not started | `REMED-GFX-203` … `REMED-GFX-208` |
| **Safe declared capability boundary** | The unsupported request is rejected deterministically, before native submission, with a public exception, and a test asserts the boundary in both directions | `MultiStreamVertexInput = false` on Vulkan / Bgfx / WebGPU / SDL_GPU / D3D11 / D3D12 / D3D9 / Headless |
| **Silent wrong result** | A supported public call is accepted, submitted, and renders or reports the wrong thing with no exception and no diagnostic | `REMED-GFX-211` (measured), `REMED-GFX-212` (partly measured) |
| **Checkpoint blocker** | Must be resolved or explicitly accepted before the post-audit checkpoint is taken | see the `Checkpoint blocker` column — `YES` / `NO` / `REVIEW`, never assumed |

A "safe declared capability boundary" is **not** a silent wrong result and must never be recorded as
one. Concretely, on the backends that report `MultiStreamVertexInput = false`:

- `GraphicsDevice::ValidateVertexStreamCapability` rejects a multi-stream ordinary draw, and an
  over-wide binding set on a backend whose `GetMaxVertexStreams()` is below 16, **before** any native
  submission — never by truncating the binding list and never by collapsing streams;
- since `REMED-GFX-202` the same gate covers **both** over-wide shapes: more than one per-vertex
  stream *and* more than one per-instance stream;
- backends additionally call `RejectUnsupportedStreamCombination`, so a hand-built `GpuDrawParams`
  from a test harness cannot silently truncate the array either;
- argument validation runs **before** the capability gate, so an out-of-range request reports the
  same public exception on every backend and a capability limit can never hide an invalid range;
- `UnsupportedBackendRejectsMultiStreamDeterministically` (ordinary) and
  `UnsupportedBackendRejectsMixedStreamInstancingDeterministically` (instanced) assert the boundary
  **in both directions on every backend**, and flip to the positive assertion the moment a backend
  claims the capability — so the declared skips cannot outlive the gap they describe.

---

## 2. Admission rules

### Belongs in this plan

- **Backend capability completion** where the unsupported use already rejects safely, deterministically
  and before native submission.
- **Implementation parity** across additional backends for a mechanism that already has a working,
  tested reference implementation elsewhere in the tree.
- **LOW correctness or reporting differences** that do not corrupt a supported common path.
- **Platform-blocked validation** — work whose verification, not whose implementation, is blocked by
  the dev environment.
- **Extensive feature work surfaced by a stronger conformance fixture**, i.e. scope that only became
  visible because a new oracle could finally express it.
- **Work best combined with a future change to the same subsystem**, where doing it now would mean
  touching the same files twice.

### Does not belong in this plan

- Crash or native fatal reachable through supported ordinary use.
- Use-after-free, memory corruption, buffer overflow or any lifetime defect.
- Silent data loss.
- **Silent wrong output in a supported common path.**
- False public API success — a call that reports success while doing nothing or the wrong thing.
- Anything blocking integration of the 19 branches or the stability of the repository.

A finding matching **any** non-candidate criterion stays in the remediation campaign regardless of its
recorded severity. `REMED-GFX-211` is the reason this rule is written explicitly rather than left
implicit: its severity of record is MEDIUM/P2, but its measured behaviour is a supported classic
instanced draw silently consuming the wrong instance records.

---

## 3. Priority model

This plan uses its own **post-audit scheduling priority**. It is a *different axis* from
`remediation/REMEDIATION_INDEX.md`'s campaign-wave priority and does not overwrite it. Both values
are carried side by side in the ticket table, and where they diverge the divergence is stated and
justified rather than silently resolved.

| Post-audit priority | Meaning |
|---|---|
| **P0** | Release / checkpoint safety |
| **P1** | Supported-path correctness |
| **P2** | Backend capability completion |
| **P3** | Cleanup, parity, reporting, or platform work |

Every entry additionally carries:

- **Checkpoint blocker:** `YES` / `NO` / `REVIEW` — never assumed, never inferred from severity.
- **Integration blocker:** `YES` / `NO` / `CONDITIONAL` — whether it blocks integrating the 19 branches.
- **Suggested trigger:** the event that should cause the work to start.
- **Dependencies:** other tickets or environment prerequisites.
- **Affected subsystem / module:** where the work lands.
- **Scope class:** `SMALL` / `MEDIUM` / `LARGE` / `EPIC`.

**No hour estimates appear anywhere in this document.** None of these tickets has been started, so
there is no measured basis for one, and the scope classes below are structural (how many native
models, caches and prior tickets the work must move through) rather than temporal.

---

## 4. Immediate checkpoint review candidates

A ticket appears here **only** when its present evidence may describe a supported path silently
producing the wrong result. Nothing here is declared a blocker on the strength of this document's
own reasoning; each entry states what evidence is still missing and what a bounded triage must
answer.

### 4.1 `REMED-GFX-211` — Vulkan, Bgfx and WebGPU ignore `VertexBufferBinding.VertexOffset` on the instanced route

**Record of status:** OPEN, MEDIUM, P2, not begun.
**Post-audit priority:** **P1.** **Checkpoint blocker: REVIEW (evidence currently points to YES).**

**Why it is not pure capability completion.** Nothing rejects. The shape involved —
`{VertexBufferBinding(mesh, offset, 0), VertexBufferBinding(instance, offset, frequency)}` at slots
0 and 1 — is the *classic* instanced shape, the one shape that reaches **no capability check at
all** by design, and the one shape 100 % of the pre-existing instancing coverage used. On Vulkan,
Bgfx and WebGPU that draw is accepted, submitted, and renders from the wrong records.

**Evidence already in the record.** `REMED-GFX-202` A/B-restored `acd703af`'s production files and
measured, on Vulkan and Bgfx, byte-identical frame maps pre- and post-fix —
`band 0: col0, col1; band 1: col2; band 3: col5` — i.e. four instances consuming instance records
**0..3 instead of 1..4**, lighting the decoy record's own cell. The per-instance `VertexOffset` of 1
was already being ignored and still is. Mechanism, by source: Vulkan copies from
`instVb.GetMappedPtr()` at offset 0, Bgfx from `instVb.cpuData.data()` at offset 0, WebGPU from
`ShadowData().begin()`. The record states the per-vertex side of the instanced route is equally
affected on those three backends.

`REMED-GFX-122` corrected EasyGL and `REMED-GFX-123` corrected D3D11/D3D12 for exactly this; these
three backends were never covered.

**Should it be resolved before the checkpoint?** On the present record it meets this plan's own
non-candidate criterion — *silent wrong output in a supported common path* — on three backends. The
recommendation is that triage confirm it as a blocker unless triage establishes something the record
does not currently contain.

**Exact evidence still missing:**

1. **A WebGPU pixel measurement.** Vulkan and Bgfx are A/B-proven at the frame-map level; WebGPU is
   identified by source inspection (`ShadowData().begin()`) only. `InstancedDrawMultiStreamTests.cpp`
   already runs on WebGPU (45/45 gate) and its classic-shape leg is the natural home for this.
2. **A per-vertex-side measurement on the instanced route** for all three backends. The record asserts
   "equally affected"; that is inspection, not a frame map.
3. **Whether `DrawUserPrimitives`-style routes are reachable with a nonzero binding offset** on those
   three backends, or whether the classic instanced route is the only exposure.

**Scope class:** MEDIUM. Three backends, each needing the element offset multiplied by that stream's
own stride exactly once at its own copy/bind site. The correct arithmetic and the regression shape
already exist twice in the tree (`REMED-GFX-122` for EasyGL, `REMED-GFX-123` for D3D11/D3D12), and
since `REMED-GFX-202` every stream's whole public `VertexOffset` is already present in
`GpuDrawParams::vertexStreams[k].vertexOffset` — the fold is 0 on this route, so the value the
backends need is already delivered and merely unread.

**Dependencies:** `REMED-GFX-202` (transport, done); pattern precedent `REMED-GFX-122`,
`REMED-GFX-123`. **Integration blocker: NO.**

### 4.2 `REMED-GFX-212` — `VertexColorEnabled` means different things per backend on the instanced route

**Record of status:** OPEN, LOW, P3, not begun.
**Post-audit priority:** **REVIEW — P1 if the divergence is a defect, P3 if it is a contract question.**
**Checkpoint blocker: REVIEW.**

**Why it is not pure capability completion.** Again nothing rejects, and again the shape is the
classic supported instanced draw. The stock instanced shader colours from `DiffuseColor` on
**Vulkan, WebGPU, D3D11 and D3D12**, and from the **per-vertex colour stream** on **EasyGL and
Bgfx**. A caller who sets `VertexColorEnabled` and supplies per-vertex colours therefore gets two
different pictures depending on the backend, with no diagnostic on either side.

**Evidence already in the record.** Measured as `unknown` / white in `REMED-GFX-202`'s
nearest-corner colour classifier on **Vulkan and WebGPU** — which is precisely why that task's
cross-backend preservation leg asserts *positions only* and does not assert colour. D3D11 and D3D12
are identified by inspection.

**Why it is REVIEW rather than an outright blocker.** The record establishes a **divergence**; it
does not establish **which side is correct**. Until the authoritative behaviour is settled this is
either a four-backend rendering defect (P1) or a two-backend one, or a genuine XNA contract question
about what `VertexColorEnabled` means when the vertex colour arrives through an instanced binding
set. The distinction changes both the priority and the fix.

**Exact evidence still missing:**

1. **The authoritative FNA/XNA answer.** What does the stock effect do with `VertexColorEnabled` on
   `DrawInstancedPrimitives`, per the FNA reference tree at
   `/rv/data/library/github.com/FNA-XNA/FNA`? Neither record contains this. It is a source read, not
   an experiment, and it is the single cheapest thing that would resolve the ticket's class.
2. **A pixel measurement on D3D11 and D3D12** rather than inspection.
3. **Which stock effect families are affected** — the record names the stock instanced shader; it does
   not enumerate the families (`BasicEffect`, `SkinnedEffect`, `EnvironmentMapEffect`, `PbrEffect`, …)
   against which the same question holds.
4. **Whether the ordinary route agrees with itself** across the same six backends, which would separate
   "instanced route only" from "a general `VertexColorEnabled` divergence".

**Scope class:** MEDIUM, with a stated risk of LARGE: if the correction lands in shader sources rather
than in parameter binding, the D3D bytecode regeneration path is on the critical path for D3D9,
D3D11 and D3D12.

**Dependencies:** an FNA reference determination, which nothing else blocks. **Integration blocker: NO.**

### 4.3 `REMED-GFX-213` — Bgfx implements no per-instance divisor at all

**Record of status:** OPEN, MEDIUM, P2, not begun. Distinct from `REMED-GFX-121` (transposed
per-instance matrix on non-GLSL renderers).
**Post-audit priority:** **P2, with an explicit P1 escalation condition.** **Checkpoint blocker: REVIEW.**

**Why it is here and not simply under capability completion.** The record documents **two** outcomes
from one mechanism, and they land on opposite sides of this plan's admission rules:

- `bgfx::setInstanceDataBuffer` advances exactly **one record per instance**, so `InstanceFrequency > 1`
  does not advance correctly; **and**
- `DrawInstancedPrimitivesEx` sizes its copy as `instanceCount * instStride`, so the backend's own
  capacity check **rejects with `ArgumentOutOfRangeException` a range the shared layer correctly
  accepted.**

The second outcome is *loud but wrong*: a valid public request is refused with an exception claiming
the caller's range is out of range when it is not. That is a false failure, not a silent wrong
result — bad, but visible, and it cannot corrupt a frame.

**The escalation condition — must be settled by triage, not assumed.** The mechanism as recorded
copies `instanceCount` records regardless of frequency. It therefore *implies* that an instance
buffer holding at least `instanceCount` records passes the capacity check, reaches the draw, and
renders with an effective divisor of 1 — a silent wrong result on a supported path. **This is an
inference from the recorded mechanism, not a measured result.** No such measurement exists in either
record.

- If triage confirms it: **P1, checkpoint blocker YES**, same class as `REMED-GFX-211`.
- If triage refutes it — every `InstanceFrequency > 1` request on Bgfx rejects, without exception —
  it is a dishonest-but-loud rejection: **P2, checkpoint blocker NO**, and the correct minimum is to
  make Bgfx's refusal honest (a capability rejection naming the divisor, not a range error).

**Exact evidence still missing:** one measurement — an oversized instance buffer at
`InstanceFrequency = 2` on Bgfx, asserting whether it throws or renders, and if it renders, which
records each instance consumed. `InstancedDrawMultiStreamTests.cpp`'s frequency axis (slot 3, stride
16, frequency 2) is already built for exactly this classification and already runs on Bgfx.

**Scope class:** MEDIUM. bgfx exposes no divisor concept at the API level, so the options to evaluate
are (a) an honest declared capability boundary — SMALL, (b) CPU-side record replication into the
transient instance buffer, which changes the per-draw copy size and must be checked against
`BgfxInstancedRangesAllocateNoPerDrawNativeResources` and
`BgfxInstancedBindingsAreTheExactPublicRanges` — MEDIUM. **These are options to evaluate, not a
decided approach.**

**Dependencies:** `REMED-GFX-202` (per-stream frequency arithmetic, done). **Integration blocker: NO.**

### 4.4 `REMED-GFX-209` — carried here for checkpoint hygiene only

Detailed in §7. It is a **test-contract defect, not a production defect**, and it cannot hide a
production bug — it fails loudly rather than passing falsely. It appears in the checkpoint discussion
for one reason only: it is a standing, known-cause red in **six** backends' principal suites, so if
the checkpoint's criteria include "principal suites green" rather than "principal suite failures all
classified", it blocks. **Checkpoint blocker: REVIEW**, scope SMALL — almost certainly cheaper to fix
than to argue about.

---

## 5. Deferred backend capability completion

Six tickets, one per native input-layout model, plus `REMED-GFX-213` where its triage lands on the
capability side. **All six were narrowed, not widened, by `REMED-GFX-202`**: they no longer need to
invent a transport, only to teach their backend to consume the array the shared layer already
delivers. Their eventual implementation must additionally handle several **per-instance** streams,
because `MultiStreamVertexInput` now gates both rates.

**Common to all six:**

- **Present safe boundary:** reports `GraphicsCapability::MultiStreamVertexInput = false`. A
  multi-stream ordinary draw, and since `REMED-GFX-202` an over-wide instanced binding set, is
  rejected deterministically with a public exception **before native submission** — never truncated,
  never collapsed to stream 0. `RejectUnsupportedStreamCombination` closes the harness-built path.
- **Completed common prerequisite:** `REMED-GFX-201` (the transport, the capability, the per-stream
  range validation, `MapCombinedOffsetToStream`, the `combinedByteBase` bridge) and `REMED-GFX-202`
  (the same representation on the instanced route, per-instance range validation, semantic
  composition across both rates).
- **Reference implementations to copy from:** EasyGL (both routes) and Software (ordinary route).
- **Conformance fixtures that already exist and already run on every backend:**
  `tests/Microsoft/Xna/Framework/Graphics/OrdinaryDrawMultiStreamTests.cpp` and
  `tests/Microsoft/Xna/Framework/Graphics/InstancedDrawMultiStreamTests.cpp`. Each backend's declared
  boundary legs **flip to positive assertions automatically** the moment it claims the capability — so
  none of these six tickets needs a new fixture, and none can be closed without the existing one going
  green.
- **Checkpoint blocker: NO** for all six. **Integration blocker: NO** for all six.
- **Suggested phase:** after the 19 branches are integrated and the tree is stabilized, and preferably
  while that backend's module is being touched during modularization.

| Ticket | Backend | Remaining native work | Scope |
|---|---|---|---|
| `REMED-GFX-203` | **Vulkan** | 16 separate pipeline builders each bake one `VkVertexInputBindingDescription{0, stride, VERTEX}` with combined-layout `VkVertexInputAttributeDescription.offset`s, and replay issues a single `vkCmdBindVertexBuffers(cb, 0, 1, …)`. Needs per-stream binding descriptions, `attribute.binding` re-slotted via `MapCombinedOffsetToStream`, the pipeline cache keyed on the combined layout, and the deferred command capturing every stream's byte range **by value**. | LARGE |
| `REMED-GFX-204` | **Bgfx** | One `bgfx::VertexLayout` per byte stride, submitted through `setVertexBuffer(stream 0, …)`. Needs one layout and one stream index per public binding, bgfx's own maximum stream count surfaced through `GetMaxVertexStreams()`, and `REMED-GFX-065`/`-084` view segmentation plus `REMED-GFX-155`/`-157`/`-181` ordering preserved. | MEDIUM |
| `REMED-GFX-205` | **WebGPU** | One `WGPUVertexBufferLayout` per pipeline with a single `arrayStride` and combined `shaderLocation` offsets. Needs a layout array, one `wgpuRenderPassEncoderSetVertexBuffer` slot per stream with its own byte offset and size, the pipeline key extended to the combined layout, and `REMED-GFX-116`/`-146`/`-159`/`-167`/`-172` deferred capture and ordering preserved. | MEDIUM |
| `REMED-GFX-206` | **SDL_GPU** | One vertex buffer description and one `SDL_GPUVertexBufferBinding` per pipeline. Needs per-slot descriptions and bindings with each stream's own pitch and offset, the pipeline cache keyed on the combined layout, and `REMED-GFX-143`/`-145`/`-152`/`-156`/`-173`/`-176` pass segmentation and lifetime preserved **with no extra pass or submit**. | MEDIUM |
| `REMED-GFX-207` | **D3D11 + D3D12** | `D3D11InputLayoutCache` is keyed on `(variant, stride)`; `D3DCommon::InputElementsForStride` / `InputElementsForStrideD3D12` emit every element with `InputSlot = 0`; D3D12 additionally bakes the element array into the PSO. Needs each element re-slotted with `MapCombinedOffsetToStream`, an `IASetVertexBuffers` / vertex-buffer-view array with per-slot strides and offsets, **stale slots cleared when a later draw binds fewer streams**, and the layout/PSO keys extended — without disturbing `REMED-GFX-123`'s instanced offsets. | LARGE |
| `REMED-GFX-208` | **D3D9** | `GetOrCreateVertexDeclarationEXT(stride)` emits every `D3DVERTEXELEMENT9` with `Stream = 0` and the draw issues one `SetStreamSource(0, …)`. Needs per-stream `Stream` indices in the combined declaration, one `SetStreamSource` per binding with its own byte offset and stride, and stream-frequency state left clean after `REMED-GFX-060`/`-117`. | MEDIUM |

**Platform-blocked leg.** `REMED-GFX-207`'s **D3D12 runtime verification** additionally depends on
`REMED-BUILD-012` — no D3D12 test constructing a real window via `Game` + `GraphicsDeviceManager`
can run in this dev environment at all. The D3D12 *implementation* is not blocked; only its
windowed-runtime proof is. `REMED-GFX-201` and `REMED-GFX-202` both A/B-proved that block is
pre-existing, by two untouched windowed fixtures dying at the identical point in the same run.
D3D11's own leg is unaffected and runs under Wine.

**Suggested implementation order within this group** — advisory, and explicitly **not** an instruction
to start any of them now:

1. **Vulkan** (`REMED-GFX-203`) — the explicit-API reference. Its binding/attribute split is the
   closest native model to the representation `GpuVertexStreamBinding` already describes, so it sets
   the pattern the rest reuse.
2. **WebGPU** and **SDL_GPU** (`-205`, `-206`) — modern explicit APIs with the same
   layout-array-per-pipeline shape; they should follow Vulkan directly and can go in parallel with
   each other.
3. **Bgfx** (`-204`) — the abstraction path; its ceiling has to be discovered and surfaced rather than
   assumed, and it interacts with the most prior ordering tickets.
4. **D3D11 / D3D12** (`-207`) — two backends, two cache models, and the stale-slot rule; D3D12's proof
   waits on `REMED-BUILD-012`.
5. **D3D9** (`-208`) — the historical path, last, because its declaration model is the least like the
   others and it benefits most from the four preceding conversions being settled.

---

## 6. Capability and reporting work

### `REMED-GFX-210` — no queryable capability for hardware instancing

**Record of status:** OPEN, LOW, P3, not begun.
**Post-audit priority: P3. Checkpoint blocker: NO. Integration blocker: CONDITIONAL.**

FNA gates `DrawInstancedPrimitives` on `FNA3D_SupportsHardwareInstancing` and throws
`NoSuitableGraphicsDeviceException` when it is 0. CNA has no equivalent: **Software** and **SDL_GPU**
override no `DrawInstancedPrimitivesEx`, so the only signal is a `std::runtime_error` from the
interface default — and Software simultaneously reports `MultiStreamVertexInput = true`, so **no
combination of existing capabilities predicts the outcome.**

**Why this is not a checkpoint blocker.** The API does not promise a false success. The call throws.
What is wrong is that it throws the *wrong exception type*, from the *wrong layer*, with *no advance
query* — a reporting and public-API-parity gap, not a silent wrong result.

**Why integration is CONDITIONAL.** Several of the 19 branches add backends unlikely to implement
hardware instancing at all. The existing precedent in `REMED-GFX-201`'s capability table is that
2D-only backends (`SDL_Renderer`, `Canvas`, `Ascii`, `DX3`) report false for every capability; the
integrated set will contain more of that shape. The cost of *not* having this capability therefore
scales with integration: each newly integrated non-instancing backend adds another public surface
whose only instancing signal is an interface-default `std::runtime_error`. It does not block any
merge; it makes each merge slightly worse until it is done.

**Scope class:** MEDIUM — a new capability enumerant, a truthful report from every backend, and the
XNA-correct public exception replacing the interface-default `std::runtime_error`.
**Suggested trigger:** the first branch-integration group that adds a backend with no instanced path;
naturally combined with `REMED-GFX-213`'s triage, which asks a neighbouring question.

### `REMED-GFX-185` — precedent, not a transfer

`REMED-GFX-185` is **DONE (2026-08-02)** and is **not moved here.** It is listed because it is the
repository's established precedent for exactly the class of work `REMED-GFX-210` represents: Bgfx was
reporting a render-target MSAA count of 8x/16x that the active renderer never delivered (bgfx queried
`GL_MAX_SAMPLES = 4`), and the resolution was to obtain the authoritative native ceiling, clamp
against it, and **report the count actually applied** — not to keep reporting the request. The rule it
established is the one `REMED-GFX-210` should follow: *a capability query must report what the backend
will actually do.*

### No broad backlog sweep was performed

Only tickets whose current documentation explicitly connects them to `REMED-GFX-201`/`-202` are
admitted. `REMED-GFX-126` ("Software reports a `MultiSampleCount` it never implements", OPEN) is the
nearest unrelated capability-reporting neighbour and is **deliberately left where it is** — it belongs
to the remediation index, not to this plan, and nothing in the current records ties it to this work.

---

## 7. Test and infrastructure work

### `REMED-GFX-209` — `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` asserts one backend's gap unconditionally

**Record of status:** OPEN, LOW, P3, not begun.
**Post-audit priority: P3. Checkpoint blocker: REVIEW. Integration blocker: NO. Scope: SMALL.**

The test asserts `SupportsCapability(GraphicsCapability::WireFrame) == false`, which is EasyGL's
documented GLES3 limitation, in a file that is **not backend-gated** — so it fails on every backend
that *does* support wireframe.

**Does it hide production behaviour?** **No.** It encodes an incorrect backend assumption and fails
loudly; it cannot mask a production defect the way a falsely-passing test can. It is pre-existing and
A/B-proven unrelated to `REMED-GFX-201`: that task confirmed both the test file and Software's own
wireframe report byte-identical to `75b61fa4`.

**Measured impact.** Failing on **six** backends across the two principal-suite sweeps recorded by
`REMED-GFX-201` and `REMED-GFX-202`: Software, Headless, Bgfx, WebGPU, Vulkan and SDL_GPU.

**Fix shape, already recorded:** either gate the assertion on `CNA_BACKEND_EASYGL`, or assert that the
query does not throw — which is what the neighbouring MSAA and anisotropy cases in the same file
already do.

**Suggested trigger:** before the post-integration checkpoint, as part of establishing a clean
principal-suite baseline. It is the cheapest item in this entire plan.

### Test-only descendants recorded by `REMED-GFX-201`/`-202`

There are none beyond `REMED-GFX-209`. This is worth stating rather than leaving implicit, because
both tasks ran explicit false-positive audits and neither produced a further test-only ticket:

- `REMED-GFX-201` found exactly one false positive, and it was a **real design error caught by an
  existing test** (`MultipleStreamsUseOnlyTheGeometryStreamsOwnOffset` failing on Vulkan until
  semantic composition was corrected) — the test was right and needed no change.
- `REMED-GFX-202` examined **22** tests in `InstancedDrawRangeTests.cpp` plus five backend harnesses
  and found **100 %** of the pre-existing instancing coverage used the one-per-vertex +
  one-per-instance shape at contiguous slots. None were strengthened in place: they are correct as the
  classic-shape preservation gate and are now used as exactly that, with the blind spots covered by
  the new fixture instead.

---

## 8. Existing unrelated boundaries — mentioned, not transferred

**This plan is not a replacement for `remediation/REMEDIATION_INDEX.md`.** It owns eleven tickets. The
index owns everything else, and nothing below is moved here by this document.

- **`REMED-GFX-199`** — D3D12's instanced PSO sets `desc.RTVFormats[0] = boundColorFormat_` and, after
  `REMED-GFX-123`, caches by `InstanceDataStepRate` alone, while every *ordinary* D3D12 path keys its
  pipeline on the format. LOW, **OPEN, untouched**, inspection-only: its pixel reproduction is blocked
  by `REMED-BUILD-012`. `REMED-GFX-201` and `REMED-GFX-202` both explicitly recorded leaving it
  untouched. **This document does not change its state and does not schedule it.** It stays a
  remediation-index ticket.
- **`REMED-BUILD-012`** — HIGH, P1, `NOT STARTED`, a Wine/vkd3d-proton **dev-environment limitation,
  not a CNA code defect**: any D3D12 test constructing a real window via `Game` +
  `GraphicsDeviceManager` faults inside vanilla Wine's own `dxgi.dll`
  (`vkd3d_instance_get_vk_instance(instance=nullptr)` under `d3d12_swapchain_init`). It blocks
  `REMED-BUILD-008`, `REMED-GFX-014`, `REMED-GFX-015` and `REMED-GFX-199` from public-API-style
  verification, and it gates `REMED-GFX-207`'s D3D12 runtime proof. It stays where it is; it is
  recorded here only because two entries in this plan depend on it.
- **Older unrelated graphics tickets** still OPEN in the index — `REMED-GFX-056`, `-114`, `-115`,
  `-120`, `-121`, `-126`, `-132`, `-133`, `-137`, `-139`, `-171`, `-178` — are **not** transferred, not
  re-prioritized and not re-classified by this document. `REMED-GFX-121` is called out only because
  `REMED-GFX-213` is explicitly distinct from it (transposed per-instance matrix on non-GLSL renderers
  vs. no divisor support at all).

---

## 9. Ticket-by-ticket table

`Rec.` = severity / priority **of record** in `remediation/`, preserved unchanged.
`PA` = this plan's post-audit priority. `CB` = checkpoint blocker. `IB` = integration blocker.
Every ticket has its own row; `REMED-GFX-203` … `-208` are **not** collapsed, because they remain
separately traceable per-backend tasks with different native models, different prior-ticket
constraints and different scope.

| Ticket | Backend / subsystem | Finding class | Current safe behavior | Rec. | PA | CB | IB | Dependencies | Suggested trigger / phase | Scope | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `REMED-GFX-203` | Vulkan — vertex input | Backend capability completion | **Safe declared boundary.** `MultiStreamVertexInput = false`; wider stream arrays rejected deterministically before native submission | MEDIUM / P2 | P2 | NO | NO | GFX-201, GFX-202 | Modularization, while touching the Vulkan module; first of the six | LARGE | 16 pipeline builders + single `vkCmdBindVertexBuffers`; sets the pattern for `-205`/`-206` |
| `REMED-GFX-204` | Bgfx — vertex input | Backend capability completion | **Safe declared boundary**, same gate | MEDIUM / P2 | P2 | NO | NO | GFX-201, GFX-202 | Modularization, while touching the Bgfx module | MEDIUM | Must surface bgfx's real stream ceiling via `GetMaxVertexStreams()`; preserve GFX-065/084/155/157/181 |
| `REMED-GFX-205` | WebGPU — vertex input | Backend capability completion | **Safe declared boundary**, same gate | MEDIUM / P2 | P2 | NO | NO | GFX-201, GFX-202; pattern from GFX-203 | Modularization, after Vulkan | MEDIUM | Preserve GFX-116/146/159/167/172 deferred capture and ordering |
| `REMED-GFX-206` | SDL_GPU — vertex input | Backend capability completion | **Safe declared boundary**, same gate | MEDIUM / P2 | P2 | NO | NO | GFX-201, GFX-202; pattern from GFX-203 | Modularization, after Vulkan; parallel with `-205` | MEDIUM | No extra pass or submit; preserve GFX-143/145/152/156/173/176 |
| `REMED-GFX-207` | D3D11 + D3D12 — vertex input | Backend capability completion + platform-blocked verification | **Safe declared boundary**, same gate on both | MEDIUM / P2 | P2 | NO | NO | GFX-201, GFX-202, GFX-123 (must not disturb); **D3D12 runtime proof needs BUILD-012** | Modularization, while touching the D3D common module | LARGE | Two backends, two cache models; stale slots must be cleared when a later draw binds fewer streams |
| `REMED-GFX-208` | D3D9 — vertex input | Backend capability completion | **Safe declared boundary**, same gate | MEDIUM / P2 | P2 | NO | NO | GFX-201, GFX-202, GFX-060, GFX-117 | Modularization, last of the six | MEDIUM | Per-stream `D3DVERTEXELEMENT9.Stream` + one `SetStreamSource` per binding; leave stream-frequency state clean |
| `REMED-GFX-209` | Test contract — `GraphicsDeviceCapabilityTest` | Test-contract defect (encodes an incorrect backend assumption; **hides nothing**) | **Fails loudly** on six backends; cannot mask a production defect | LOW / P3 | P3 | **REVIEW** | NO | none | Before the post-integration checkpoint, with the clean-baseline work | SMALL | Gate on `CNA_BACKEND_EASYGL` or assert the query does not throw, as the neighbouring cases do |
| `REMED-GFX-210` | Capability reporting — instancing | Capability-query / reporting + public-exception parity | **Throws**, but a `std::runtime_error` from the interface default; no advance query, no false success | LOW / P3 | P3 | NO | **CONDITIONAL** | none technical; adjacent to GFX-213 triage | First integration group adding a backend with no instanced path | MEDIUM | FNA has `FNA3D_SupportsHardwareInstancing` + `NoSuitableGraphicsDeviceException`; follow `REMED-GFX-185`'s report-what-you-do precedent |
| `REMED-GFX-211` | Vulkan, Bgfx, WebGPU — instanced route | **Supported-path silent wrong result** (measured on Vulkan + Bgfx) | **None.** Classic 1+1 draw is accepted and submitted; instance records 0..3 consumed instead of 1..4 | MEDIUM / P2 | **P1** | **REVIEW** *(evidence points to YES)* | NO | GFX-202 (value already delivered); precedent GFX-122, GFX-123 | **Checkpoint triage, before integration** | MEDIUM | Missing: a WebGPU pixel measurement; a per-vertex-side measurement on all three |
| `REMED-GFX-212` | Vulkan, WebGPU, D3D11, D3D12 vs EasyGL, Bgfx — stock instanced shader | **Supported-path cross-backend divergence**; defect vs contract question **not yet settled** | **None.** Both behaviours render silently; no diagnostic on either side | LOW / P3 | **REVIEW** (P1 or P3) | **REVIEW** | NO | an FNA reference determination | **Checkpoint triage, before integration** | MEDIUM *(LARGE if shader sources change)* | Missing: the authoritative FNA answer; pixel measurement on D3D11/D3D12; family and ordinary-route coverage |
| `REMED-GFX-213` | Bgfx — per-instance divisor | Capability gap **with a possible silent-wrong-divisor path** | **Rejects** `InstanceFrequency > 1` with `ArgumentOutOfRangeException` — loud, but the wrong exception for a valid range | MEDIUM / P2 | **P2** *(P1 if escalation confirms)* | **REVIEW** | NO | GFX-202; distinct from GFX-121 | **Checkpoint triage** (one measurement), then Bgfx module work | MEDIUM | Escalates to P1/YES **iff** an oversized instance buffer passes the capacity check and renders at divisor 1 — inferred from the recorded mechanism, **not measured** |

---

## 10. The nineteen integration branches

CNA currently carries **19** unintegrated branches (excluding `develop`, `master` and this
remediation branch `feature/audit`). Verified against the repository, not assumed:

| # | Branch | # | Branch |
|---|---|---|---|
| 1 | `feature/depthcrt` | 11 | `feature/opengl4` |
| 2 | `feature/direct2d` | 12 | `feature/opengles1` |
| 3 | `feature/dxold` | 13 | `feature/skia` |
| 4 | `feature/gdi` | 14 | `feature/stub` |
| 5 | `feature/gl` | 15 | `origin/claude/diligent-engine-backend-cna-01cs23` |
| 6 | `feature/glide` | 16 | `origin/claude/html-dom-cna-backend-xefzwf` |
| 7 | `feature/gltf` | 17 | `origin/claude/llgl-gr-backend-cna-3orpwo` |
| 8 | `feature/metal` | 18 | `origin/claude/noxna-graphics-api-extension-lihfjk` |
| 9 | `feature/opengl1` | 19 | `origin/claude/sokol-gfx-cna-backend-8s3oo8` |
| 10 | `feature/opengl2` | | |

### The integration touchpoint is adaptation, not any ticket in this plan

**None of the 19 branches is a Vulkan, Bgfx, WebGPU, SDL_GPU, D3D11, D3D12 or D3D9 branch.** Those
seven backends live on the mainline, so **no ticket in §5 has a branch-adaptation touchpoint at
all** — `REMED-GFX-203` … `-208` are strictly post-integration or modularization work.

What the 19 branches *do* touch is the common layer those tickets were built on. Measured against
each branch's merge base with `develop`:

- **0 of 19** contain `fc0dd2a2` (`refactor(graphics): unify ordinary and instanced stream
  descriptions`) — every one of them predates the unified representation;
- **16 of 19** modify `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`;
- **13 of 19** modify `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`;
- **10 of 19** modify `include/CNA/GraphicsCapability.hpp`;
- only `feature/depthcrt`, `feature/dxold` and
  `origin/claude/noxna-graphics-api-extension-lihfjk` touch none of the three.

Every one of them therefore still carries the **old** `GpuDrawParams` shape, with the four fields
`REMED-GFX-202` removed — `instanceVb`, `instanceVertexOffset`, `instanceFrequency` and
`vertexBufferOffset`. (`feature/skia`'s header, for example, still declares
`const IVertexBufferBackend* instanceVb = nullptr;`.) That is a **mechanical adaptation cost on
every branch that implements a draw path**, and it is the real integration work — not a defect, and
not a ticket.

### Per-branch adaptation checklist (derived from `REMED-GFX-201`/`-202`, not new policy)

For each integrated backend:

1. Replace the removed `GpuDrawParams` scalars with the `vertexStreams` / `vertexStreamCount`
   representation. `FirstPerVertexStream`, `FirstInstanceStream`, `NthPerVertexStream`,
   `NthInstanceStream`, `PerVertexStreamCount`, `InstanceStreamCount` and `HasMultipleInstanceStreams`
   exist precisely to make this a substitution rather than a redesign.
2. Declare `GraphicsCapability::MultiStreamVertexInput` **honestly**. Its default is `false` and a
   backend must opt in **by name**. Defaulting to true would let a backend that silently renders from
   stream 0 alone claim otherwise — which looks like a correct draw of the wrong data.
3. Call `RejectUnsupportedStreamCombination` so a hand-built `GpuDrawParams` cannot truncate.
4. Run `OrdinaryDrawMultiStreamTests.cpp` and `InstancedDrawMultiStreamTests.cpp`. A backend that
   declares `false` must pass the boundary legs; a backend that declares `true` gets the positive
   assertions automatically.
5. If the branch's backend has no instanced path, note it against `REMED-GFX-210` rather than
   inventing a per-branch ticket.

### Where each post-audit task sits relative to integration

| When | Tasks |
|---|---|
| **Before branch integration** | `REMED-GFX-211`, `REMED-GFX-212`, `REMED-GFX-213` triage (§4) — they are mainline-backend behaviour and are cheaper to settle on a tree that is not mid-merge |
| **While adapting a particular branch** | None of §5. Only the adaptation checklist above, plus `REMED-GFX-210` evidence-gathering when a branch adds a backend with no instanced path |
| **After all 19 branches are integrated** | `REMED-GFX-209` (clean principal-suite baseline before the checkpoint); `REMED-GFX-210` |
| **During modularization** | `REMED-GFX-203` … `REMED-GFX-208`, and `REMED-GFX-213`'s implementation if triage lands it on the capability side |
| **During NoXNA graphics-extension work** | Nothing from this plan is a prerequisite; see §12 |

---

## 11. Modularization relationship

**No modularization work is started, designed or implied by this document.** What follows is a
recorded observation, to be used when modularization is actually planned.

`REMED-GFX-201`/`-202` produced a clean seam that did not exist before, because the same
representation now serves all three draw routes and every backend. The layers it separated are a
natural candidate module boundary:

| Conceptual layer | What lives there today |
|---|---|
| Common `GraphicsDevice` / input validation | `ValidateVertexStreamRanges`, `ValidateInstanceStreamRanges`, `ValidateVertexStreamCapability`, ordered index → declared range → per-stream vertex → per-instance → capability |
| Immutable draw-stream description | `GpuVertexStreamBinding`, `GpuDrawParams::vertexStreams` / `vertexStreamCount` / `combinedVertexStride` — fixed capacity 16, by value, no per-draw heap allocation |
| Backend input-layout translation | `MapCombinedOffsetToStream`, `combinedByteBase`, `CombinedVertexStrideOr`, `HasMultipleVertexStreams`, `VertexStreamByteOffset` — all degenerating to the identity for a single stream |
| Backend-specific native binding | each backend's own `vkCmdBindVertexBuffers` / `IASetVertexBuffers` / `SetStreamSource` / `setVertexBuffer` / `wgpuRenderPassEncoderSetVertexBuffer` site |
| Capability declaration / query | `GraphicsCapability::MultiStreamVertexInput`, `GetMaxVertexStreams()`, `RejectUnsupportedStreamCombination` |
| Conformance fixtures | `OrdinaryDrawMultiStreamTests.cpp`, `InstancedDrawMultiStreamTests.cpp`, `OrdinaryDrawBindingOffsetTests.cpp`, `InstancedDrawRangeTests.cpp` |

Two properties make this seam worth preserving through modularization:

- **The single-stream identity.** Every helper degenerates to the identity when one stream is bound, so
  a module boundary drawn here costs nothing on the overwhelmingly common path — `REMED-GFX-201`
  measured single-stream native bindings byte-identical to their `REMED-GFX-200` form.
- **The capability gate is the module's public contract.** `REMED-GFX-203` … `-208` are, in module
  terms, six independent implementations of one already-specified interface with one already-written
  conformance suite. That is the cheapest possible shape for that work — which is the main argument
  for doing it *during* modularization rather than before it.

---

## 12. NoXNA extension relationship

**No NoXNA API is designed or implemented here.** `NOXNA.md` remains the owner of that surface;
`origin/claude/noxna-graphics-api-extension-lihfjk` is branch 18 of the 19.

`NOXNA.md` already lists the tasks this plan is adjacent to — `N50` (`GraphicsDevice::DrawInstancedPrimitives`
NOXNA overload), `N51` (instance-data `VertexBuffer` streaming helper) and `N52` (LOD selection helper),
under §4.4 *Geometry & Instancing*, all `⬜ Not started`.

Eventual NoXNA graphics extensions are likely to want capabilities beyond the XNA 4.0 contract:

- **more vertex streams** than XNA's 16;
- **explicit, queryable capabilities** rather than XNA's implicit "it works or it throws";
- **modern input rates and divisors** beyond `InstanceFrequency`'s integer step rate;
- **expanded vertex formats** beyond `VertexElementFormat`.

Each of those is a *superset* of something this plan's tickets complete, which is exactly why they
should be built on the modular architecture rather than folded into the immediate XNA-remediation
checkpoint:

- more streams presumes `REMED-GFX-203` … `-208` (the 16-slot XNA case working everywhere first);
- explicit capabilities presume `REMED-GFX-210` (a truthful instancing report) and the
  `MultiStreamVertexInput` / `GetMaxVertexStreams()` pattern already established;
- modern divisors presume `REMED-GFX-213` (Bgfx having any divisor at all).

**Forcing those capabilities into the checkpoint would invert the dependency**: the XNA contract would
be finished on top of an extension layer instead of the extension layer being built on a finished XNA
contract. The checkpoint's job is the XNA 4.0 contract. NoXNA comes after modularization.

---

## 13. Suggested future execution order

Dependency-aware and **advisory**. This is not an instruction to begin any ticket now.

1. **Complete checkpoint triage** — `REMED-GFX-211`, `REMED-GFX-212`, `REMED-GFX-213`, and decide
   `REMED-GFX-209` against the checkpoint's actual green-suite criterion. Bounded: three measurements
   and one FNA source read (§4). Resolve anything triage classifies as a supported-path silent wrong
   result **inside the remediation campaign**, not here.
2. **Integrate the 19 branches**, in groups, using the §10 adaptation checklist.
3. **Resolve branch-specific blockers during adaptation** — including the mechanical `GpuDrawParams`
   conversion every branch needs.
4. **Stabilize the integrated tree** — one principal-suite sweep per backend, every failure classified.
5. **Begin modularization**, drawing the seam described in §11.
6. **Implement the backend capability tickets while touching their backend modules** —
   `REMED-GFX-203` (Vulkan reference) → `REMED-GFX-205` / `REMED-GFX-206` (modern explicit APIs) →
   `REMED-GFX-204` (abstraction path) → `REMED-GFX-207` (D3D11/D3D12) → `REMED-GFX-208` (D3D9
   historical path), plus `REMED-GFX-210` and `REMED-GFX-213`.
7. **Begin NoXNA extensions on the modular architecture** (§12).

---

## 14. Touch-it-fix-it policy

- **When integrating or modifying a backend, review that backend's assigned post-audit tickets first.**
  The §9 table is indexed by backend for exactly this.
- **Resolve related SMALL / P1 work when it is economically bounded** — if the file is already open and
  the fix is contained, do it, and record it in `remediation/` as usual.
- **Do not expand an unrelated branch adaptation into an unlimited remediation campaign.** Adapting
  `feature/skia` to the new `GpuDrawParams` is not permission to implement `REMED-GFX-204`.
- **A new P0 finding interrupts feature work.** Crash, UAF, memory corruption, silent data loss, silent
  wrong output on a supported path, or false public API success: stop, record, fix. These are the §2
  non-candidates and they never enter this plan.
- **New P2 / P3 findings enter this plan** with a full §9-shaped row, and get a ticket in
  `remediation/` in the same ID scheme so they stay traceable from both directions.
- **Never mark a ticket DONE for moving.** Scheduling is not resolution.

---

## 15. Review cadence

- **After each major branch-integration group** — re-check whether any adaptation revealed a new
  supported-path defect, and whether any deferred ticket became cheaper or more urgent.
- **Before the post-integration checkpoint** — re-run §4 in full. Every `REVIEW` must become an
  explicit `YES` or `NO` with evidence; none may stay `REVIEW` at the checkpoint itself.
- **At each modularization milestone** — the §5 tickets are the natural acceptance tests for the module
  boundary in §11; a milestone that makes one of them harder has drawn the seam wrong.
- **A targeted second audit after substantial real-world use.** `REMED-GFX-201`/`-202` are the argument
  for this: both were invisible until a fixture existed that could express a split-stream declaration
  end to end, and 100 % of the pre-existing instancing coverage was blind to everything
  `REMED-GFX-202` found. Coverage shaped by real use finds a different class of defect than coverage
  shaped by the existing implementation.

---

## 16. Relationship to the remediation campaign

Recorded here and mirrored in `remediation/REMEDIATION_PROGRESS.md`:

- `REMED-GFX-201` and `REMED-GFX-202` **completed the shared architecture.** There is one vertex-stream
  representation, one validation path and one capability gate for all three draw routes.
- `REMED-GFX-203` … `REMED-GFX-213` **no longer form an automatic immediate execution chain.** They are
  DEFERRED with disposition POST-AUDIT PLAN, targeting this file.
- The intensive campaign **may proceed toward reconciliation and the checkpoint without implementing
  every backend capability descendant.**
- **Checkpoint review must still inspect the supported-path silent-wrong-result candidates** — §4, at
  minimum `REMED-GFX-211`, `REMED-GFX-212` and `REMED-GFX-213`'s escalation condition.
- **Deferred work stays visible** through this file, and through the `DEFERRED` status and
  `Target plan: plan_postaudit.md` pointer on every affected row in
  `remediation/REMEDIATION_INDEX.md` and `remediation/REMEDIATION_PROGRESS.md`.

**The remediation campaign is not declared complete by this document.** No checkpoint has been taken
and no tag has been created.
