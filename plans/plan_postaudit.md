# CNA Post-Audit Development Plan

**Created:** 2026-08-03, on `feature/audit`, immediately after `REMED-GFX-202` closed.
**Owns:** `REMED-GFX-203` … `REMED-GFX-240` and any future finding admitted by the rules below.
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
| **Silent wrong result** | A supported public call is accepted, submitted, and renders or reports the wrong thing with no exception and no diagnostic | `REMED-GFX-211`, `-212`, `-213`, `-215` and `-216` — all five measured at pixel level on 2026-08-03, and **all five DONE**. `REMED-GFX-215` showed the class's sharpest lesson: a white-`DiffuseColor` oracle cannot see a `DiffuseColor` defect, so a defective backend was certified a correct control. `REMED-GFX-216` showed the next one: an oracle pointed only at the backend a ticket names cannot see that **every other backend has the same defect** — running it everywhere spawned `REMED-GFX-217` and `REMED-GFX-218`, both OPEN members of this class |
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

> **That triage has now run (2026-08-03).** Each entry below keeps its original text unchanged and
> carries a **TRIAGE RESULT** block stating what was measured and how the ticket is finally
> classified. All three of `REMED-GFX-211`, `-212` and `-213` resolved to **checkpoint blocker YES**;
> none of them moved to deferred capability work. The measurements live in
> `tests/Microsoft/Xna/Framework/Graphics/InstancedDrawMultiStreamTests.cpp`'s CHECKPOINT TRIAGE
> group, which prints every reading it takes on every backend it runs on and whose per-backend arms
> fail the moment their backend is corrected. No production file was changed by the triage.

### 4.1 `REMED-GFX-211` — Vulkan, Bgfx and WebGPU ignore `VertexBufferBinding.VertexOffset` on the instanced route — **DONE (2026-08-03)**

> **BACKEND-SCOPE PROGRESS, 2026-08-03 — VULKAN DONE, BGFX DONE, WEBGPU OPEN.** The ticket is
> **still OPEN** and still **checkpoint blocker YES**; two of its three backends are now corrected.
>
> **BGFX (2026-08-03).** The same two-loss-point shape, at bgfx's own expressions in
> `BgfxGraphicsBackend::DrawInstancedPrimitivesEx`. Per-instance:
> `std::memcpy(idb.data, instVb.cpuData.data(), copyBytes)` took its source from record zero and now
> takes it from `cpuData.data() + vertexOffset * instStride` — this stream's own stride, never
> binding 0's. Per-vertex: `bgfx::setVertexBuffer(0, vb.handle, range.vertexStart, …)`, whose
> `vertexStart` carried `params.baseVertex` alone. bgfx has **no draw-time base-vertex argument at
> all**, so `_startVertex` is the only term that reaches a decoded index — which is precisely why
> the ORDINARY bgfx indexed route was already correct (`params.baseVertex` there already carries the
> folded binding offset from REMED-GFX-200/201) and the instanced route was not (REMED-GFX-202 folds
> nothing into it, deliberately, so a base-vertex term cannot advance a per-instance stream). The
> offset now joins `baseVertex` in that start element, the bound remainder shrinks by exactly as
> much, and the wireframe sibling takes the same addend through `ExpandWireframeIndices` so its
> zero-based binding assertion stays green. Readings on `cb1d2398`, pre → post: leg A
> `instance-offset-ignored` → `instance-offset-honoured` (pre-fix cells (0,0) (1,0) (2,1) (5,3) —
> the decoy's own cell lit, live record 4's lost), leg B `vertex-offset-ignored` →
> `vertex-offset-honoured` (pre-fix all four cells `prefix(green)`), leg C `both-offsets-ignored` →
> `both-offsets-honoured`. Active renderer **OpenGL 2.1**; the Bgfx Vulkan renderer is UNTESTED
> here. bgfx diagnostics byte-identical to a device-creation-only run; ASan/UBSan clean with the
> leak total A/B-classified as Mesa/bgfx-shutdown. Only bgfx production changed.
> **WebGPU is unchanged and its triage arms still assert its own measured defect.**
> The two Vulkan loss points were separate expressions in `DrawInstancedPrimitivesEx`, and only one
> of them was the copy site the triage predicted: the per-instance side was indeed
> `std::memcpy(d.instVbData.data(), instVb.GetMappedPtr(), …)`, but the per-vertex side was **not**
> the neighbouring `vb.GetMappedPtr()` copy — it was `d.baseVertex = params.baseVertex`, which
> dropped the geometry binding's offset term entirely. That mattered: the per-vertex copy takes the
> WHOLE buffer into the deferred arena and binds it at the draw's own packed arena offset, so
> binding 0 has no per-binding native offset channel to put an offset in, while
> `vkCmdDrawIndexed`'s own `vertexOffset` argument is added to every decoded index and the route
> binds exactly one per-vertex stream. Folding there applies it once, to its own stream only.
> Readings on `d750c10a`, pre → post: leg A `instance-offset-ignored` → `instance-offset-honoured`,
> leg B `vertex-offset-ignored` → `vertex-offset-honoured`, leg C `both-offsets-ignored` →
> `both-offsets-honoured`. Vulkan moved from `CNA_INSTANCED_OFFSET_DEFECT_MEASURED` into
> `CNA_INSTANCED_BINDING_OFFSET_ORACLE` in both instanced test files — reusing REMED-GFX-122/123's
> complete oracle rather than inventing a parallel one — plus seven new permanent legs.
> **WebGPU is unchanged and its triage arms still assert its own measured defect**;
> the four legs printed `instance-offset-ignored`, `vertex-offset-ignored`, `both-offsets-ignored`
> and `divisor-1-silently` on it, verbatim, after this work.
>
> **WEBGPU (2026-08-03) — the ticket is now DONE on every backend it names.** The same two-loss-point
> shape a third time, and the per-vertex loss point was again NOT the neighbouring copy. In
> `WebGPUGraphicsBackend::DrawInstancedPrimitivesEx`, `command.vertexData` takes the WHOLE per-vertex
> buffer into a command-owned CPU vector and `IssueInstancedDraw` uploads it to a fresh buffer bound
> at `wgpuRenderPassEncoderSetVertexBuffer(pass, 0, …, /*offset=*/0, …)` — so binding 0 again has no
> per-binding native offset channel, and the only term reaching a decoded index is
> `wgpuRenderPassEncoderDrawIndexed`'s own `baseVertex`. The loss point was
> `command.baseVertex = params.baseVertex`, structurally identical to Vulkan's `d.baseVertex` rather
> than to bgfx's start-element. It is now `params.baseVertex + perVertexOffset`; the term is a signed
> `int32_t` on both sides, so negative-`baseVertex` behaviour is unchanged. The per-instance side was
> where the ticket said: `command.instVbData.assign(instShadow.begin(), instShadow.begin() +
> instanceCount * stride)` took `instanceCount` consecutive records from record zero. Its source base
> is now `vertexOffset * instVbStride` — this stream's own stride, never binding 0's, never advanced
> by `baseVertex`. Readings on `9df8770d`, pre → post: leg A `instance-offset-ignored` (cells (0,0)
> (1,0) (2,1) (5,3) — records 0..3, the decoy's own cell lit and live record 4's lost) →
> `instance-offset-honoured`; leg B `vertex-offset-ignored` ((4,2) (5,2) (6,3) (7,3) — every instance
> renders the mesh decoy) → `vertex-offset-honoured`; leg C `both-offsets-ignored` ((4,2) (5,2) (6,3),
> 768 lit) → `both-offsets-honoured`. WebGPU moved out of `CNA_INSTANCED_OFFSET_DEFECT_MEASURED` into
> `CNA_INSTANCED_BINDING_OFFSET_ORACLE` in both instanced test files, which turns REMED-GFX-122/123's
> existing matrix on for it; that emptied the measured-defect set, so the macro and its four arms were
> deleted and the triage legs keep only their `#else` UNCLASSIFIED arm for the still-unmeasured D3D9.
> Only WebGPU production changed. **REMED-GFX-212's WebGPU boundary is preserved and untouched** —
> `ordinary-route` still reads `liveA(red)` and `instanced-route` still reads `unknown`.

**Record of status:** ~~OPEN, MEDIUM, P2 — Vulkan and Bgfx scopes DONE 2026-08-03, WebGPU not
begun.~~ → **DONE 2026-08-03 on all three named backends (Vulkan, Bgfx, WebGPU). Checkpoint blocker
RESOLVED.** D3D9 remains unmeasured for want of a D3D display and is asserted neither way.
**Post-audit priority:** **P1.** **Checkpoint blocker: ~~REVIEW (evidence currently points to YES)~~
→ YES, confirmed by measurement 2026-08-03 — see the TRIAGE RESULT at the end of this subsection.**

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

#### TRIAGE RESULT (2026-08-03) — **P1, checkpoint blocker YES. Confirmed, and wider than recorded.**

All three missing pieces of evidence were taken at pixel level on the classic 1+1 shape, with the
per-vertex and per-instance sides isolated from each other for the first time. Legs A/B/C of
`InstancedDrawMultiStreamTests.cpp`'s CHECKPOINT TRIAGE group; EasyGL is the known-correct control
and is green on all three, which is what calibrates the oracle.

| backend | A: instance offset | B: per-vertex offset | C: both nonzero |
|---|---|---|---|
| EasyGL (control) | honoured | honoured | honoured |
| Vulkan | **ignored** | **ignored** | **both ignored** |
| Bgfx | **ignored** | **ignored** | **both ignored** |
| WebGPU | **ignored** | **ignored** | **both ignored** |

1. **The WebGPU pixel measurement now exists** and matches Vulkan and Bgfx exactly — WebGPU is no
   longer source-identified only.
2. **The per-vertex side is confirmed, not inferred.** Leg B holds the instance offset at zero and
   puts the decoy in the geometry stream: all three backends render the decoy triangle. The record's
   "equally affected" was right, and it is now measured.
3. Leg C separates the failure modes the single-sided legs cannot see. The reading is
   `both-offsets-ignored` — **not** one binding's offset cross-applied to both streams, **not** an
   offset applied twice, and not a partial loss. **The two streams fail independently, so a
   correction must carry both**; fixing only the instance side would leave leg B red.

Every draw was **accepted** — no exception, no capability error, no diagnostic on any of the three.
This is a supported-path silent wrong result on the one shape that reaches no capability check by
design, which is exactly this plan's §2 non-candidate criterion for deferral.

**Confirmed by source:** `InstanceFrequency`/`vertexOffset` never reach the copy sites —
`VulkanGraphicsBackend.cpp:10180` copies the per-vertex stream from `vb.GetMappedPtr()` at offset 0
and `:10192` the per-instance stream from `instVb.GetMappedPtr()` at offset 0.

**Not measured, and deliberately left open:** D3D9 is outside `CNA_INSTANCED_BINDING_OFFSET_ORACLE`
too, but no D3D display was reachable from the triage session (SDL reports `x11 not available` under
Wine on the Xvfb displays this environment permits), so the triage asserts nothing about it in either
direction. The legs print their reading on any host that can run that backend.

**Smallest implementation task if taken now:** at each of the three backends' own copy/bind sites,
multiply that stream's `GpuDrawParams::vertexStreams[k].vertexOffset` by that stream's own stride
exactly once, for the per-vertex and per-instance stream alike — the value is already delivered and
merely unread, and legs A/B/C are the acceptance gate.

### 4.2 `REMED-GFX-212` — `VertexColorEnabled` means different things per backend on the instanced route

**Record of status:** ~~OPEN, LOW, P3, not begun.~~ → **DONE 2026-08-03. Vulkan and WebGPU scopes
both DONE; checkpoint blocker RESOLVED and the ticket has left the blocker set.**
**Post-audit priority:** ~~**REVIEW — P1 if the divergence is a defect, P3 if it is a contract
question.**~~ → **P1: the reference settles it as a defect (case A).**
**Checkpoint blocker: ~~REVIEW~~ → ~~YES, 2026-08-03~~ → NO — RESOLVED 2026-08-03.**

> **CLOSED (2026-08-03).** Both backends were reproduced first on unfixed `199c9b7f` — the new
> permanent oracle `tests/Microsoft/Xna/Framework/Graphics/InstancedVertexColorTests.cpp` ran
> **1/9 on Vulkan and 1/9 on WebGPU**, the one passing leg on each being the
> `VertexColorEnabled = false` control — then classified independently, then corrected. Both loss
> points are **case C** with case A as its consequence: neither backend lost a colour attribute it
> had, each selects a dedicated instanced shader family that never declared one
> (`kInstanced3dVertSpv`'s `fragColor = pc.diffuseColor;` with a one-attribute vertex input, and
> WebGPU's inline WGSL `output.color = u.diffuseColor;` with a one-element `vertexAttrs`). Both
> already *received* `vertexColorEnabled` (`FillInstancedPushConst`'s `pc[31]`, `FillExtUniforms`'
> `out[31]`) and simply never read it. The fix is the colored3d/textured3d split the ordinary route
> already makes, applied to the instanced family and selected from each backend's own stride table
> (16 and 24, colour at offset 12, shader location 1). `VertexColorEnabled` is deliberately not a
> pipeline-key dimension on either backend; Vulkan additionally folds the raw per-vertex stride into
> its Instanced3D key so a position-only and a position+colour declaration cannot share a pipeline.
> **9/9 on Vulkan, WebGPU, EasyGL and bgfx; zero Vulkan VUIDs with the layer proven loaded; zero
> WebGPU uncaptured errors; ASan and UBSan clean; 2 pipeline variants total and no extra draw, pass,
> submit, frame, wait or readback.** Only Vulkan and WebGPU production changed. Full record in
> `remediation/REMEDIATION_PROGRESS.md`. Two independent findings were spawned and given IDs
> without being fixed: **`REMED-GFX-214`** (WebGPU's ordinary route throws on a stride-24 draw with
> `TextureEnabled = false`) and **`REMED-GFX-215`** (bgfx's instanced shader emits the raw COLOR0,
> ignoring DiffuseColor *and* VertexColorEnabled — the mirror image of this ticket, invisible to the
> triage oracle because its DiffuseColor was white). **`REMED-GFX-215` was triaged as a checkpoint
> blocker and is now DONE (2026-08-03)**; it spawned `REMED-GFX-216`, which was reclassified P1,
> fixed the same day, and in turn spawned `REMED-GFX-217` and `REMED-GFX-218` — both OPEN silent
> wrong results on the remaining backends. `REMED-GFX-214` is still OPEN and uninvestigated.

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

#### TRIAGE RESULT (2026-08-03) — **case A: P1, checkpoint blocker YES.**

**The authoritative FNA/XNA answer, by source read of `/rv/data/library/github.com/FNA-XNA/FNA`.**
The reference makes `VertexColorEnabled` **draw-call-independent**, on three independent points:

1. `GraphicsDevice.DrawInstancedPrimitives` (`src/Graphics/GraphicsDevice.cs:1257`) is a hardware-
   instancing support check, `ApplyState()`, `PrepareVertexBindingArray(baseVertex)` and the
   `FNA3D_DrawInstancedPrimitives` call. **It touches no effect state whatsoever** — the same
   `ApplyState()` the ordinary route calls.
2. `BasicEffect.OnApply` (`src/Graphics/Effect/StockEffects/BasicEffect.cs:488-511`) derives
   `shaderIndex` from `fogEnabled`, `vertexColorEnabled`, `textureEnabled` and `lightingEnabled`
   **only**. There is **no instancing term**, so the identical vertex shader permutation is selected
   for an instanced draw and an ordinary one.
3. Every vertex-colour permutation in `BasicEffect.fx` multiplies `vout.Diffuse *= vin.Color` (ten
   sites), reading `COLOR` from `VSInputVc`.

So the reference contract is unambiguous: **`VertexColorEnabled = true` with a bound `COLOR0` stream
must multiply in the per-vertex colour on the instanced route exactly as on the ordinary one.** This
is case A of the triage brief.

A related fact worth recording, because it bounds the *shape* of any correction but not the verdict:
FNA's `BasicEffect.fx` contains **zero** instancing constructs and takes `World` from a constant
buffer (`float4x4 World _vs(c19) _cb(c15)`), never from a vertex stream. CNA's stock instanced shader
— a per-instance world matrix in `TEXCOORD1..4` — is therefore a **CNA extension with no reference
counterpart**. The *colour* half is not an extension: `VertexColorEnabled` is XNA public API on an
XNA effect, and CNA's own ordinary route already implements it.

**Measured, and it needs no reference at all.** A new leg draws the same triangle from the same
buffer under identical effect state (`VertexColorEnabled = true`, white `DiffuseColor`, lighting and
texturing off) through both routes and reads the colour of the same cell:

| backend | ordinary route | instanced route | agree? |
|---|---|---|---|
| EasyGL | red (stream colour) | red | yes |
| Bgfx | red (stream colour) | red | yes |
| Vulkan | red (stream colour) | **white — `DiffuseColor`** | **no** |
| WebGPU | red (stream colour) | **white — `DiffuseColor`** | **no** |

**Vulkan and WebGPU disagree with the reference *and with themselves*.** Their own ordinary route
honours the bound `COLOR0` stream; their instanced permutation substitutes `DiffuseColor` for it,
under identical public state, with no diagnostic. Question 4 of the missing-evidence list is answered
in passing: the ordinary route agrees with itself across every backend measured, so this is an
instanced-route-only divergence, not a general `VertexColorEnabled` problem.

**Still unmeasured:** D3D11 and D3D12 remain source-identified only — the Wine control could not
start (`SDL_InitSubSystem(SDL_INIT_VIDEO) failed: x11 not available`; a native binary runs fine on
the same display, so this is Wine-specific and not a broken display). The new leg asserts nothing
about them in either direction and prints its reading on any host that can run them. This does not
hold the ticket at REVIEW: **Vulkan and WebGPU alone are measured, silent and wrong**, which settles
the classification. Missing item 3 — which stock effect families beyond `BasicEffect` are affected —
is scope for the fix, not for the verdict.

**Priority resolves to P1**, the upper of the ticket's two stated arms, and the LOW severity of
record understates it. **Scope stays MEDIUM with the recorded LARGE risk**: if the correction lands
in shader sources rather than parameter binding, D3D bytecode regeneration is on the critical path.

**Smallest implementation task if taken now:** make the Vulkan and WebGPU instanced shader
permutations consume the bound `COLOR0` stream when `VertexColorEnabled` is set, exactly as their own
ordinary permutations already do; `OrdinaryAndInstancedRoutesAgreeOnVertexColorEnabled` is the
acceptance gate and needs no reference oracle to run.

### 4.3 `REMED-GFX-213` — Bgfx, Vulkan and WebGPU implement no per-instance divisor at all — **DONE (2026-08-03)**

> **BACKEND-SCOPE PROGRESS, 2026-08-03 — VULKAN DONE, BGFX DONE, WEBGPU OPEN.** The ticket is
> **still OPEN** and still **checkpoint blocker YES**.
>
> **BGFX (2026-08-03).** Its native capability was classified from the installed bgfx header, not
> from a grep count. bgfx's entire public instancing surface is `allocInstanceDataBuffer` /
> `getAvailInstanceDataBuffer` / the four `setInstanceDataBuffer` overloads / `setInstanceCount`,
> with `struct InstanceDataBuffer { data, size, offset, num, stride, handle }` — there is **no
> divisor, step-rate or frequency parameter anywhere on the path**, no renderer-specific step-rate
> mechanism is exposed through it, and every supplied record advances exactly once per drawn
> instance. Category **C** again: destination slot `i` takes source record
> `vertexOffset + i / frequency` in the instance-data buffer the route already allocates; frequency
> 1 keeps its single bulk `memcpy`; frequency > 1 costs `instanceCount` one-record `memcpy`s and no
> allocation beyond the same single `allocInstanceDataBuffer`. Destination cardinality is unchanged,
> so no program, layout or state word moves and no pipeline-key term was needed. bgfx's over-long
> instance-range gate was generalised at the same time, from `instanceCount * stride >
> cpuData.size()` — the wrong count once a frequency groups instances and the wrong base once an
> offset moves them — to the highest source record `vertexOffset + (instanceCount - 1) / frequency`,
> which is `ValidateInstanceStreamRanges`' own arithmetic. Reading on `cb1d2398`:
> `divisor-1-silently` → `divisor-2-honoured` at frequency 2, before and after an intervening
> frequency-1 draw, frequency-1 control unchanged. Cardinality **measured**, identical at both
> frequencies: 4 public draws → 5 bgfx submissions, 16 TriList prims, 1024 transient VB bytes, 0
> transient IB, 0 extra views; 8 draws → 9 and 2048; per-draw delta exactly one submission at
> either frequency. Two further permanent legs cover both offsets nonzero at frequency 3, and
> frequency 2 → 1 → 2 within a single frame. **WebGPU is unchanged.**
>
> **VULKAN (2026-08-03).** The native capability was CLASSIFIED before a
> mechanism was chosen, because the triage's "all three CAN emulate a divisor" is a claim about what
> is possible, not about what is already enabled: the Vulkan backend requests
> `VK_API_VERSION_1_1` and its device-extension list is literally
> `kDeviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME }` — one entry — so neither
> `VK_EXT_vertex_attribute_divisor` nor its KHR promotion is enabled, no
> `VkPhysicalDeviceVertexAttributeDivisorFeatures` is chained, and divisors are core only from
> Vulkan 1.4. Category **C** (replication into the staging path the route already builds), not A or
> B. `binds[1]` therefore keeps `VK_VERTEX_INPUT_RATE_INSTANCE` at the implicit divisor of 1, and
> the copy writes source record `vertexOffset + i / frequency` into destination slot `i`.
> **Destination cardinality is unchanged** — one record per instance, exactly as before — so nothing
> about the native binding, the pipeline, or its cache key moves, and the pipeline-identity question
> §4.3 raised resolves to *data-copy concern only* rather than *needs a divisor in the key*.
> `MakeExt3DKey` takes stride, topology, depth, blend, cull, attachment count, wireframe and MSAA;
> no offset and no frequency reaches it, and none was added. Frequency 1 keeps its single bulk
> `memcpy`; frequency > 1 costs `instanceCount` one-record `memcpy`s and no allocation beyond the
> same single `resize`. Reading on `d750c10a`: `divisor-1-silently` → `divisor-2-honoured`, both
> before and after an intervening frequency-1 draw, with the frequency-1 control still
> `divisor-1-honoured`. A new permanent leg then covers frequency 3, frequency 3 with a nonzero
> instance offset, a frequency larger than the instance count at two offsets, and a return to
> frequency 1 — so no special case for two could pass. **WebGPU is unchanged and still prints
> `divisor-1-silently`.**
>
> **WEBGPU (2026-08-03) — the ticket is now DONE on every backend it names.** The native capability
> was classified from the INSTALLED headers before a mechanism was chosen, as it was for Vulkan and
> bgfx. In wgpu-native **v29.0.1.1** (`cmake-build-webgpu/_deps/wgpu-native-v29.0.1.1/include/webgpu`),
> `WGPUVertexBufferLayout` is exactly `{ nextInChain, stepMode, arrayStride, attributeCount,
> attributes }` — no divisor and no step-rate field; `WGPUVertexStepMode` is
> `{ Undefined, Vertex, Instance, Force32 }` — no rate; the strings `divisor`, `stepRate`,
> `step_rate` and `instance_rate` occur **zero** times in `webgpu.h` *and* in wgpu-native's own
> extension header `wgpu.h`; the eleven `WGPUNativeSType` extension chains cover Device, NativeLimits,
> ShaderSourceGLSL, Instance, BindGroupEntry, BindGroupLayoutEntry, QuerySetDescriptor,
> SurfaceConfiguration, SurfaceSourceSwapChainPanel, PrimitiveState and SamplerDescriptor —
> **none extends a vertex buffer layout or vertex state**; and no `WGPUNativeFeature` adds a step
> rate. Category **C**, on the same evidential footing as the other two, and no device feature or
> extension was added to avoid a bounded copy that already exists. Binding 1 therefore keeps
> `WGPUVertexStepMode_Instance`'s implicit divisor of one and destination slot `i` takes source
> record `vertexOffset + i / frequency` in the command's own `instVbData` vector. **Destination
> cardinality is unchanged** — one record per instance — so `GetOrCreatePipelineInstanced3D`'s key
> (`Make3DPipelineKey` over topology, strip index format, depth, blend, cull, wireframe, depth bias
> and a `pvStride`/`instVbStride` salt) needed no frequency term and got none: the divisor is
> data-preparation state only. Frequency 1 keeps its single bulk `memcpy`; frequency > 1 costs
> `instanceCount` one-record `memcpy`s and no allocation beyond the same single `resize`. Readings on
> `9df8770d`: `divisor-1-silently` → `divisor-2-honoured` at frequency 2, before and after an
> intervening frequency-1 draw, with the frequency-1 control still `divisor-1-honoured`. Cardinality
> **measured** by a new permanent WebGPU regression
> (`examples/webgpu_instanced_offset_frequency_cardinality_test.cpp`, 22/22) against the backend's own
> counters and wgpu-native's `wgpuGenerateReport()` live-object registry: a 4-draw cycle costs
> 1 render pass, 1 queue submit, 0 new Instanced3D pipeline variants and 0 new native render pipelines
> at frequency 1, 2 and 3 and at every offset pair tried; doubling the instance count at frequency 2
> adds no native buffer; and repeating a sequence leaves the live native buffer, pipeline and command
> buffer counts unchanged. WebGPU's over-long instance-range gate was generalised the same way bgfx's
> was, to the highest SOURCE record `vertexOffset + (instanceCount - 1) / frequency`.

**Record of status:** ~~OPEN, MEDIUM, P2 — Vulkan and Bgfx scopes DONE 2026-08-03, WebGPU not
begun.~~ → **DONE 2026-08-03 on all three named backends (Bgfx, Vulkan, WebGPU). Checkpoint blocker
RESOLVED.**
Distinct from `REMED-GFX-121` (transposed per-instance matrix on non-GLSL renderers).
**Post-audit priority:** ~~**P2, with an explicit P1 escalation condition.**~~ → **P1: the escalation
condition was measured and confirmed.** **Checkpoint blocker: ~~REVIEW~~ → YES, 2026-08-03 — see the
TRIAGE RESULT at the end of this subsection. Backend scope widens to Bgfx, Vulkan and WebGPU.**

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

#### TRIAGE RESULT (2026-08-03) — **escalation condition CONFIRMED: P1, checkpoint blocker YES. And it is not only Bgfx.**

The one missing measurement was taken: six instances at `InstanceFrequency = 2` against an instance
buffer holding **six** records — twice the three a correct divisor needs, so neither the shared range
gate nor a backend sizing its own copy by `instanceCount` can refuse it. Records are authored one per
cell, so the consumed sequence is readable directly from the frame.

| backend | frequency = 2 | frequency = 1 (control) | frequency = 2 again |
|---|---|---|---|
| EasyGL (control) | `divisor-2-honoured` — records 0,0,1,1,2,2 | `divisor-1-honoured` | `divisor-2-honoured` |
| Bgfx | **`divisor-1-silently`** — records 0,1,2,3,4,5 | `divisor-1-honoured` | **`divisor-1-silently`** |
| Vulkan | **`divisor-1-silently`** | `divisor-1-honoured` | **`divisor-1-silently`** |
| WebGPU | **`divisor-1-silently`** | `divisor-1-honoured` | **`divisor-1-silently`** |

**The ticket's inference was right and its "loud but wrong" arm is not what happens here.** Nothing
threw. The draw was accepted, reached the native call, and `InstanceFrequency = 2` produced *exactly
the frame `InstanceFrequency = 1` produces*, with no diagnostic. The `ArgumentOutOfRangeException`
the record describes is real but is a *second-order* consequence of an under-sized buffer; with a
buffer large enough to clear the capacity check — the ordinary case for any caller who sizes a buffer
for its instance count — the failure is silent. The frequency-1 control proves the leg can see a
divisor-1 sequence, and the repeat draw proves the reading is not stale state.

**Two corrections to the ticket as recorded:**

1. **Backend scope is Bgfx, Vulkan *and* WebGPU**, not Bgfx alone. Per the triage brief, divisor
   evidence belongs to this ticket, so the scope is widened here rather than split into a new ID.
   **No new ticket was created.**
2. **This is not a native-capability limit on any of the three.** Confirmed by source: the string
   `frequency` appears **0 times** in `VulkanGraphicsBackend.cpp`, `WebGPUGraphicsBackend.cpp` and
   `BgfxGraphicsBackend.cpp`, and once in `EasyGLGraphicsBackend.cpp` — the public
   `InstanceFrequency` value never reaches three of the four backends at all. `bgfx.h` has no
   `divisor` or step-rate concept anywhere in its public API (0 matches) and
   `bgfx::setInstanceDataBuffer` advances one record per instance; WebGPU's `WGPUVertexStepMode` is a
   binary `Vertex`/`Instance` enum with no divisor field. But **all three can express a divisor by
   replicating records into the buffer they already build** — Bgfx at
   `BgfxGraphicsBackend.cpp:4620`'s `memcpy` into the transient instance buffer, Vulkan at
   `VulkanGraphicsBackend.cpp:10192`'s equivalent. So the "unavailable hardware capability, truthfully
   rejected" arm of the brief does not apply: it is unimplemented emulation, not an absent capability,
   and today it is neither emulated nor declared.

**Smallest implementation task if taken now:** where each of the three backends copies `instanceCount`
consecutive instance records, write record `vertexOffset + i / frequency` into slot `i` instead, and
size the copy by the records actually consumed. `ClassicInstanceFrequencyDivisorIsReadOnEveryInstancingBackend`
is the acceptance gate, and it already asserts the frequency-1 control and the no-stale-state repeat.
This shares its copy site with `REMED-GFX-211`'s correction on all three backends, so the two are
naturally one piece of work per backend.

**Not measured:** D3D9, for the same display reason as `REMED-GFX-211`. D3D11/D3D12 are excluded by
`REMED-GFX-123`, which keyed their input layout/PSO cache on the step rate.

### 4.4 `REMED-GFX-217`, `REMED-GFX-218` and `REMED-GFX-214` — exit triage (2026-08-04)

`REMED-GFX-216`'s declaration oracle, run on every backend rather than only on bgfx, spawned
`REMED-GFX-217` and `REMED-GFX-218`. This section is their checkpoint triage, together with
`REMED-GFX-214`'s, which had never been investigated. **No production code was changed by this
triage and none of the three tickets was implemented.** The evidence is the committed pixel
measurements in `remediation/REMEDIATION_INDEX.md` and `REMEDIATION_PROGRESS.md`, plus source
classification of every named backend; no build and no test run was required to reach the
classifications below, and none was performed.

#### 4.4.1 The mechanism, stated once

Nine backends select a native vertex layout from **one canonical stride table** that is transcribed
independently in at least four places — `D3DCommon::InputElementsForStride` /
`InputElementsForStrideD3D12` (`D3DVertexFormatHelper.cpp`), `MakeBgfxLayout(stride)`,
`EasyGLVertexBufferBackend::ApplyLayout`'s switch, and Vulkan's / SDL_GPU's / WebGPU's per-family
`attrs[]` builders. All four agree byte for byte:

| stride | elements |
|---|---|
| 16 | Position@0, Color@12 |
| 20 | Position@0, TexCoord@12 |
| 24 | Position@0, Color@12, TexCoord@16 |
| 32 | Position@0, Normal@12, TexCoord@24 |
| 48 | Position@0, Normal@12, Tangent@24, TexCoord@40 |
| 52 | Position@0, Normal@12, TexCoord@24, BlendWeight@32, BlendIndices@48 |
| 56 | stride 52 + Color@52 |
| 68 | stride 48 + BlendWeight@48, BlendIndices@64 |

The public `VertexDeclaration` is **not** consulted. `IVertexBufferBackend::SetVertexDeclaration` is
pure virtual precisely so that dropping a declaration must be an explicit decision, and
`VertexBuffer::UploadValidatedData` calls it immediately before every real upload — so the
authoritative description arrives at all nine and eight of them discard it.

**The defect is therefore not "custom declarations do not work".** It is exactly this: *a
declaration whose elements disagree with the canonical table entry for its own stride is accepted,
submitted, and rendered from the table's bytes instead of the declaration's.* Two declarations that
share a stride are indistinguishable to these backends.

**The native binding stride, however, comes from the buffer, not from the table** on every backend
except bgfx. That is why bgfx desynchronized whole records on `positionOnly12` and the others did
not: bgfx derived record spacing from the layout it guessed, the others derive it from the real
stride and only the *attribute offsets* are wrong. This distinction is what separates the two
sub-classes below.

#### 4.4.2 `REMED-GFX-217` — backend-by-backend classification

The ticket names eight backends. They do **not** share one observable failure, and the umbrella
is retained only because the *loss point* is identical (an empty `SetVertexDeclaration` plus a
stride-keyed table). Three distinct outcomes, established from the committed measurements and from
each backend's own accepted-stride set:

| backend | rasterizes | accepted strides on a stock no-texture `BasicEffect` draw | silent-wrong surface | safe-rejection surface |
|---|---|---|---|---|
| **Vulkan** | yes | **every stride** — `MakeExt3DKey`'s `default: s = 0` buckets any unrecognised stride with 16, and there is **no stride guard anywhere in the backend** | **widest of the eight.** Any declaration disagreeing with the table at an in-table stride, plus every out-of-table stride silently treated as 16. Measured: `colorPosition16` → nothing rasterized; `positionTextureColor24` → geometry exact, colour `(0,0,0,63)`; `positionColorPadded32` → colour attribute absent, plain DiffuseColor `(204,89,140)` | **none** |
| **Software** | yes (CPU) | 16/20/24/32/52 only | same three cases, measured identically to Vulkan | stride 12 and every other out-of-table stride: *"unsupported vertex stride (only 16/20/24/32/52 supported in v1)"* — an honest declared v1 boundary, **not** this defect |
| **WebGPU** | yes | **16 only** without a bound texture; 20/24/32 need `params.texture0 != nullptr` | **`colorPosition16` alone** — five of the oracle's seven declarations never reach native submission | the other five, via `REMED-GFX-214`'s stride-16 message. Loud, deterministic, pre-native |
| **SDL_GPU** | yes | same dispatch shape as WebGPU (`stride == 16`; 20/24/32 gated on `texture0`) | **`colorPosition16` alone**, by the same reasoning | the rest, by fall-through to the same loud stride-16 requirement |
| **D3D11** | yes | 16/20/24/32/48/52/56/68 | the three measured cases, by shared `InputElementsForStride` | out-of-table strides throw *"unsupported vertex stride"* |
| **D3D12** | yes | same, via `InputElementsForStrideD3D12` | same | same |
| **D3D9** | yes | same, via `GetOrCreateVertexDeclarationEXT(stride)` | same | same |
| **Headless** | **no** | n/a | **NONE — this backend has no native vertex layout of any kind.** `DrawPrimitivesEx` validates its arguments and calls `DrawColoredPrimitives`, which records a trace; no pixel is ever produced, so no pixel can be wrong. It already reports `MultiStreamVertexInput = false` on exactly this reasoning | n/a |

**Triage correction: Headless does not belong in `REMED-GFX-217`.** Its empty
`SetVertexDeclaration` cannot produce a wrong result because there is nothing downstream of it. The
ticket's scope is **seven** rasterizing backends, not eight. This is recorded rather than silently
dropped.

**D3D9/D3D11/D3D12 runtime-untested.** No D3D display is reachable here, so their rows above are
source-derived from the shared `D3DCommon` table rather than measured. That is *not* a reason to
leave the classification at REVIEW: the loss point is a **shared** helper whose public consequence
is already measured at pixel level on Vulkan and Software, and the D3D backends consume it
unchanged.

**The declaration classes, separated:**

- **Built-in declarations** (`VertexPositionColor`, `…Texture`, `…ColorTexture`, `…NormalTexture`,
  the PBR and skinned types) — match the table by construction. **Correct on all ten backends.**
  Every CNA example, `SpriteBatch`, and every stock effect path uses only these, which is why this
  has never been visible.
- **Same stride, different element content** — `colorPosition16`, `positionTextureColor24`.
  **Silently wrong.** The shader family already has the right *semantics*; only the byte offsets are
  taken from the wrong source. This sub-class needs no new shader and no new family selection.
- **Same semantics, unusual padding** — `positionColorPadded32`. **Silently wrong**, and *worse*
  than the previous class: at stride 32 the table's semantic set is Position+Normal+TexCoord, so the
  Color the caller declared has no attribute to land in at all. Fixing this needs the shader family
  to be chosen from the **declaration** rather than the stride.
- **Semantic order changes** — indistinguishable from the same-stride class above on these eight;
  the declaration is sorted by offset before it reaches them and is then discarded.
- **Out-of-table strides** — Software/D3D/WebGPU/SDL_GPU reject loudly. **Vulkan does not**, and
  silently reuses the stride-16 attribute set. Note that Vulkan renders `positionOnly12` *correctly*
  today only because `VertexColorEnabled` is false there and the out-of-record Color@12 attribute is
  never read; the same declaration with vertex colour enabled reads past every record.
- **Unsupported formats** — outside the measured set; not a new class, since the format never
  reaches the backend either.

**Public capability reporting is currently silent on all of this.** No `GraphicsCapability` entry
describes declaration fidelity, and none of the eight claims or denies it. That is the honest gap.

#### 4.4.3 Does CNA permit a backend to reject a declaration it cannot represent?

**Yes, and the mechanism is already established.** Two precedents settle it:

1. `GraphicsCapability::MultiStreamVertexInput` — default **false**, queried by
   `GraphicsDevice::ValidateVertexStreamCapability`, and an unsupported binding set throws
   `System::NotSupportedException` **before native submission**, never truncated. Its own doc
   comment names the analogy explicitly: *"a newly added backend must make an explicit decision to
   claim this, exactly like `IVertexBufferBackend::SetVertexDeclaration` being a required
   override."*
2. `REMED-GFX-216`'s bgfx translator raises `System::NotSupportedException` for an overlapping
   element, a repeated semantic, an unmappable usage or format, an element outside its own stride,
   and a renderer/declaration size disagreement — **before anything is created or submitted**, never
   a substituted guess.

So a truthful, deterministic, pre-native rejection is in-contract and needs **no new public API**.
`IGraphicsBackend.hpp` already documents the stride-derived model at line 696.

#### 4.4.4 The four strategies, evaluated

| | Strategy | Files | Scope | Checkpoint-safe? | Verdict |
|---|---|---|---|---|---|
| **A** | Full immediate implementation — declaration-driven layout on all seven rasterizing backends | 7 backends × (layout translator + family selection + pipeline/PSO/input-layout cache key) | **LARGE** | no — this *is* `REMED-GFX-203` … `-208` under another name | **Rejected.** The preferred strategy must not require implementing `-203` … `-208`, and this would |
| **B** | Shared pre-native guard in `GraphicsDevice` | `GraphicsDevice.cpp` (16/19 branches touch it), `IGraphicsBackend.hpp` (13/19), `GraphicsCapability.hpp` (10/19) | MEDIUM | yes, but | **Rejected on integration grounds.** A new capability enumerator plus a new `IGraphicsBackend` query lands on the three highest-contention files in the whole tree, immediately before a 19-branch integration |
| **C** | **Backend-local guard, one shared predicate** — the seven rasterizing stride-table backends turn their empty `SetVertexDeclaration` into a call to one shared helper that compares the declaration against the canonical table entry for its stride; EasyGL gets the equivalent inside `SelectProgram` for the **stock** families only | 1 new header/source pair under `Backends/Common`, 7 one-line override bodies, 1 EasyGL function + its 4 `SelectProgram` call sites, plus test updates | **SMALL–MEDIUM** | yes | **Recommended for `REMED-GFX-217`.** No public API change, no `GraphicsDevice`/`IGraphicsBackend`/`GraphicsCapability` edit, therefore **zero contention with the 19 branches** |
| **D** | Direct deferral | — | — | only where behaviour is already truthful | **Correct for Headless** (does not rasterize) and **for `REMED-GFX-214`** (already a loud deterministic rejection). Not defensible for Vulkan/Software/D3D, which silently render the wrong thing |

**The predicate Strategy C must use, and why the obvious one is wrong.** A strict
"declaration must equal the table entry" test is **not safe**: it would reject declarations that
render correctly today, such as `positionOnly12` on Vulkan (Position@0 matches; the table's Color@12
is simply never read). The sound rule is narrower and asymmetric:

> reject when the declaration names a semantic the table entry for its stride does **not** carry, or
> places a shared semantic at a **different offset or format** than the table entry does.

Checked against the whole oracle matrix: `positionColor16`, `positionTexture20`,
`positionColorTexture24` and `positionOnly12` pass unchanged; `colorPosition16` (Position@4 vs @0),
`positionTextureColor24` (Color@20 vs @12) and `positionColorPadded32` (Color absent from the
stride-32 entry) reject. Every currently-correct case survives and every measured silent-wrong case
becomes a legible exception. `VertexDeclarationLayoutTest` **already accommodates this**: its
`deviatesElsewhere` arm is guarded on `r.rendered`, and a non-rendering result falls through to a
branch that only requires the refusal to be legible.

**Known consequence, stated rather than discovered later.** The two production paths that can build
a non-built-in declaration are `Xnb::VertexDeclarationReader` (`ModelContentTypeReaders.cpp:118`,
which reads offsets/formats/usages straight out of the content file) and the public
`DrawUserPrimitives` / custom-`VertexBuffer` surface. Under Strategy C those go from *silently wrong
pixels* to *a deterministic exception naming the declaration and the backend* on seven backends.
That is the correct direction, but it is a public behaviour change and needs each affected backend's
principal suite green before the checkpoint — which is `REMED-GFX-209`'s clean-baseline work
anyway, already scheduled for the same slot.

#### 4.4.5 `REMED-GFX-218` — EasyGL, and why it is **not** the small fix it looks like

EasyGL is the one backend that consumes the declaration
(`EasyGLVertexBufferBackend::SetVertexDeclaration` stores `GetVertexElements()`), and `ApplyLayout`
then assigns `const auto location = static_cast<unsigned int>(i)` —
`EasyGLGraphicsBackend.cpp:3477`. Measured: `colorPosition16` renders nothing;
`positionTextureColor24` renders exact geometry with colour `(102,45,0,255)` — a *different* wrong
colour from Vulkan's and Software's, which is the evidence that the mechanism differs;
`positionColorPadded32` loses the colour attribute.

The tempting one-line fix — replace `location = i` with `location = SemanticSlot(usage)` — **is
wrong**, for three independently sufficient reasons found in the source:

1. **There is no global semantic→location function to write.** EasyGL's stock shaders bind
   location 1 to `aColor` at stride 16, to `aUV` at stride 20, and to `aNormal` at stride 32;
   location 5 is `aColor` in the skinned families. The correct location depends on the **bound
   program family**, not on the semantic alone.
2. **`ApplyLayout` runs at upload time**, from `SetData` (lines 3644/3683/3701), where no program is
   bound and no `GpuDrawParams` exists. The family is only known at draw time, in
   `SelectProgram(stride, params)` — four call sites (5877, 5949, 6083, 6116).
3. **The index convention is load-bearing and, for one path, intentional.**
   `ConfigureDeclarationAttributes`, `FirstLocationForStream` (`REMED-GFX-201`) and
   `PerVertexLocationCount` (`REMED-GFX-202`) all concatenate locations by element **count**, and
   the `ShaderEffect` custom-program path documents *"location N == Nth field of the ported HLSL
   input struct"* as its contract with the caller — a custom GLSL shader may declare inputs CNA has
   no semantic for at all. Mapping by semantic there would **break** custom shaders.

**Classification: MEDIUM checkpoint fix, not SMALL.** The correct correction is per-family semantic
placement applied to the **stock** path only, at draw time, leaving the custom-`ShaderEffect` index
contract untouched — and it must not disturb `REMED-GFX-201`/`-202`'s location arithmetic. This is
recorded explicitly because the instruction for this triage was not to recommend rejection merely to
avoid a small semantic-mapping fix: the evidence says it is not a small semantic-mapping fix, and
the reason is structural rather than a matter of effort.

**Consequence for the checkpoint.** Strategy C's guard applied to EasyGL's stock path (its
`SelectProgram` variant, above) converts EasyGL's three silent-wrong cases into the same legible
rejection as the other seven, without touching the custom-shader contract and without the per-family
placement work. That keeps the ten backends *consistent* at the checkpoint — which matters, because
a guard applied to seven backends and not to EasyGL would leave EasyGL the **only** backend silently
rendering the wrong thing, a worse divergence than today.

#### 4.4.6 `REMED-GFX-214` — WebGPU stride-24 with `TextureEnabled = false`

Established from source, no run required.
`WebGPUGraphicsBackend::DrawPrimitivesEx` / `DrawIndexedPrimitivesEx` dispatch stride 20/24 to
`QueueTexturedDraw` **only when `params.texture0 != nullptr`**, and stride 32 likewise. A stride-24
draw with the texture off matches no branch, falls through the documented
*"unmatched stride/texture combination"* tail to `DrawColoredPrimitives`, and
`QueueColoredDraw`'s **first statement** throws
`std::invalid_argument("CNA WebGPU: DrawColoredPrimitives requires a stride-16 (VertexPositionColor) vertex buffer")`.

- **Before native submission:** yes — the throw precedes `ColoredDrawCommand command;` and every
  encoder call. No command is queued, no buffer written, no pass opened.
- **Partial submission / device loss / memory corruption:** none possible; nothing was started.
- **Caller receives a deterministic exception:** yes, and its text is recorded verbatim by
  `InstancedVertexColorTest.PackedColorTextureStrideConsumesGeometryColorOnBothRoutes`, which prints
  the boundary rather than skipping it.
- **Same input silently misrendering in another permutation:** no. Every other route for this stride
  is gated on the same `texture0 != nullptr` condition and reaches the same tail.
- **Capability reporting truthful:** no capability claims this either way — the same gap as
  `REMED-GFX-217`, but here it costs nothing, because the failure is loud.

**Checkpoint blocker: NO. DEFERRED.** A loud deterministic unsupported-path rejection is exactly the
class this plan defers. Its smallest fix is unchanged and still recorded: route stride 20/24 with a
null `texture0` to the textured pipeline against the default white texture, as Vulkan's
`GetOrCreatePipelineFogTex3D` stride-24 arm already does.


#### 4.4.7 `REMED-GFX-DECL-GUARD` — the bounded checkpoint action, **DONE (2026-08-04)**

Strategy C, implemented. **This is not a new finding and has no ticket of its own:** it is the
bounded remediation action recorded against `REMED-GFX-217` and `REMED-GFX-218`, and it resolves
both of their checkpoint blockers without implementing either translator.

**The rule that shipped, and how it differs from §4.4.4's sketch.** §4.4.4 proposed *"reject when
the declaration names a semantic the table entry for its stride does not carry, or places a shared
semantic at a different offset or format"*. That is right as far as it goes, and the implemented
rule keeps it as R2, but three further clauses were required once the predicate had to survive the
whole matrix rather than the seven-entry oracle:

- **R0** — the native record advance must equal the declared stride. A declaration whose stride
  disagrees with the stride the buffer was uploaded with describes a different record, whatever its
  elements say, and every record after the first would be read from the wrong address.
- **R1** — no native fetch may read bytes a declared element owns under another semantic, usage
  index, offset or format. R2 covers every semantic the declaration *names*; R1 closes the
  remaining shape, a native attribute the declaration does **not** name reaching into bytes it
  does. This is the direct statement of the safety goal.
- **R2** — every declared element has a native attribute with the same usage **and usage index**, at
  the same offset, in the same format. The usage index was missing from the sketch: `Color1` is not
  `Color0`, and a table entry that binds only `Color0` cannot supply it.
- **R3** — every declared element lies wholly inside the declared stride.
- **R4** — declared elements do not overlap one another.

**The asymmetry is load-bearing and is now permanently tested.** A native attribute the declaration
does not name is **not** a violation — nothing declared is being reinterpreted — which is exactly
why Vulkan's `positionOnly12` still renders: its fallback binds `Color0@12`, the declaration names
no colour, and the two never meet. `VertexDeclarationFidelityTest` asserts the accepted cases as
hard as the rejected ones, so tightening the rule into equality fails the suite rather than quietly
removing working layouts.

**Where the unlisted-stride behaviour comes from.** The predicate takes a per-route
`UnlistedStrideLayout` describing what that backend actually does with a stride the canonical table
does not list, and every value is measured, not assumed:

| Backend / route | Unlisted stride | Evidence |
|---|---|---|
| Vulkan, ordinary | `PositionColorFallback` | `positionOnly12` renders the correct flat staircase today |
| Vulkan, instanced | `PositionOnlyFallback` | `PackedColorOffsetForStride` lists 16 and 24 only |
| WebGPU, ordinary | `BackendRefusesIt` | `QueueColoredDraw` throws for any stride but 16 |
| WebGPU, instanced | `PositionOnlyFallback` | `positionOnly12/instanced` renders correctly today |
| SDL_GPU, both | `BackendRefusesIt` | the unmatched-shape tail reaches the same stride-16 refusal |
| Software | `BackendRefusesIt` | *"unsupported vertex stride (only 16/20/24/32/52 supported in v1)"* |
| D3D9 / D3D11 / D3D12 | `BackendRefusesIt` | `InputElementsForStride*` returns null and the caller throws |

`BackendRefusesIt` means the guard **abstains**: the backend's own rejection is already loud,
deterministic and pre-native, and replacing it would change an established boundary for no safety
gain. Every one of those messages is unchanged after this task.

**Where the guard runs.** At the top of each backend's `DrawPrimitivesEx` /
`DrawIndexedPrimitivesEx` / `DrawInstancedPrimitivesEx` — before any pipeline, input layout, PSO or
`IDirect3DVertexDeclaration9` is created, before any command is queued and before any submission.
D3D9/D3D11/D3D12 share one call site each through their own `DrawPrimitivesExImpl`. The seven
backends' `SetVertexDeclaration` overrides stopped being empty: they now *remember* the declaration
so there is something to compare against. **Nothing is translated, repacked or padded, and no
fallback layout was added.**

**Why the guard is not at `SetVertexDeclaration` itself.** It was the obvious site and it is wrong.
Per-instance streams carry declarations the per-vertex stride table has never described — the
instancing fixture's own stream is stride 64 with `TextureCoordinate1..4` — and a buffer does not
know at upload time which role it will play. Guarding at draw time inspects only the geometry
stream, which is the declaration these backends actually infer a layout from.

**EasyGL is a different rule, because it is a different mechanism.** Its stock programs bind by
attribute location and the declaration's element **order** chooses those locations, so the truthful
comparison is against the selected program's own ordered input list, transcribed from the
`layout(location=N) in ...` lines of the shaders themselves. `SelectProgram`'s cascade was factored
into one `SelectStockProgramShape` that both the program selection and the guard read, so the
program a draw is bound to and the shape it is checked against cannot drift apart. The guard runs
only when `params.customEffectBackend == nullptr`: **a custom `ShaderEffect` keeps its documented
element-index convention untouched**, proven by a dedicated control that renders `colorPosition16`
through a `ShaderEffect` declaring `aColorIn` at location 0 and `aPosIn` at location 1 and reads
each column's own unmultiplied record colour back.

**Measured outcome, all ten backends.** The three colliding declarations moved from *accepted and
silently wrong* to *deterministic `System::NotSupportedException` before any native work* on
Vulkan, Software, WebGPU, SDL_GPU, EasyGL, D3D9, D3D11 and D3D12; bgfx, which has a real
translator, still renders every one of them correctly and is the control proving the refusals are a
**per-backend capability boundary**, not a claim that these declarations are invalid. Every built-in
declaration, `positionOnly12` on Vulkan, `positionTexture20`, `positionColorTexture16/24` and every
pre-existing loud rejection are unchanged.

**Checkpoint blockers of `REMED-GFX-217` and `REMED-GFX-218` are RESOLVED. Both tickets stay OPEN**
for the real translators, which remain in this plan alongside `REMED-GFX-203` … `-208`.

### 4.5 `REMED-GFX-209` — **DONE 2026-08-04**

Detailed in §7, which now records what was measured rather than what was expected. It was a
**test-contract defect, not a production defect**, and it could not hide a production bug — it failed
loudly rather than passing falsely. It appeared in the checkpoint discussion for one reason only: it
was a standing, known-cause red in **six** backends' principal suites, so it blocked under a
"principal suites green" criterion and not under a "failures all classified" one. That question is
now moot: the red is gone from all six, **no production file changed**, and the remaining failures on
every backend are the pre-existing ones this plan already names.

It also produced the campaign's one new finding, **`REMED-GFX-219`** — EasyGL reports
`GraphicsCapability::WireFrame` as `false` while its own `GL_LINES` emulation renders a correct
wireframe. That is a production defect, it is OPEN, it is **not** a checkpoint blocker (the renderer
is right; only the report under-states it), and it is deliberately not fixed here.

### 4.6 `WEBGPU-115` — **RESOLVED 2026-08-04.** Was the last checkpoint blocker, P1

`REMED-GFX-209` measured WebGPU's WireFrame output as byte-identical to Solid and classified it as a
"documented deviation, `WEBGPU-115`, already recorded and accepted." **Exit reconciliation checked
that claim and it does not hold.**

**Re-measured live at `099b03c0`:**

```
[ GFX-209 ] WebGPU solid:     total=18176 interior=1089/1089 AB=298 BC=310 CA=329
[ GFX-209 ] WebGPU wireframe: total=18176 interior=1089/1089 AB=298 BC=310 CA=329
```

**Every term of the blocker rule is satisfied:**

- **The capability reports support.** `WebGPUGraphicsBackend` does **not override**
  `SupportsCapability` at all, so `GraphicsDevice.SupportsCapability(WireFrame)` returns the
  `IGraphicsBackend` default — `true`. The claim is inherited, not deliberate, which is precisely why
  nobody noticed it contradicts the backend's own comments.
- **The request is accepted.** `ApplyRasterizerState` stores `fillModeWireframe_` and returns. No
  throw, no warning, **no log**.
- **A command is queued and natively submitted.** `command.wireframe` is captured at 11 queue sites
  and folded into `Make3DPipelineKey`, so a **distinct `WGPURenderPipeline` is built and submitted**
  — and then the flag reaches no `WGPUPrimitiveState` field, because WebGPU has no polygon mode.
- **No truthful boundary exists.** `plan_webgpu.md:504`'s `WEBGPU-115` row is **`⬜` — NOT DONE**; its
  entire content is *"document as unsupported … add to deviations doc"*, never performed.
  `docs/webgpu-backend.md:488` mentions wireframe in prose under *Important limitations*, which is
  unreachable from the public API and is **contradicted** by the capability query. Internal C++
  comments are not public contract.

**This is the exact inverse of `REMED-GFX-214`**, which is safely deferred *because* it rejects
loudly before native submission. And it is the exact inverse of **`REMED-GFX-219`**: GFX-219
under-reports a capability EasyGL genuinely has (conservative, no wrong pixels); `WEBGPU-115`
over-reports one WebGPU does not have (silently wrong geometry). **Do not bundle them** — their
safety directions are opposite.

**Smallest safe correction — preferred:** override `SupportsCapability` to return `false` for
`WireFrame` on WebGPU, and reject a WireFrame draw deterministically **at draw time** (not at
`ApplyRasterizerState`, per `REMED-GFX-DECL-GUARD`'s precedent that a state setter cannot know what
the draw will be). This matches the repository's established pattern for an unrepresentable request.
**Alternative, large:** implement real wireframe by index-expanding triangles to line topology.

**Two dependent test contracts must be updated in the same task** —
`GraphicsDeviceCapabilityTest.WireFrameSilentlyRendersSolidGeometryOnThisBackend` is written to fail
the day this is fixed (its own failure message says so), and `examples/webgpu_graphicsstate_test.cpp`
Check G asserts the draw "does not crash".

#### RESOLUTION — 2026-08-04

The preferred correction above was implemented, backend-local, in `636b43de` + `0be30127`.

| Term | Before | After |
|---|---|---|
| `SupportsCapability(WireFrame)` | `true`, inherited | **`false`**, asserted by `WebGPUGraphicsBackend` |
| `ApplyRasterizerState(WireFrame)` | accepted silently | still accepted — a state operation stays one |
| A polygon draw consuming it | queued, keyed, pipelined, submitted | **`System::NotSupportedException`**, before any of it |
| Queued draw commands | +1 | **0** |
| Pipeline caches | +1 | **0** |
| Native draw issues | +1 | **0** |
| The target | solid triangle, 18176 px | **unchanged**, 0 px |
| The next Solid draw | — | renders exactly; 1 draw, 1 submit, 0 new pipelines |

**Where the refusal lives.** `RequireSupportedFillModeEXT(primitive, route)` at the top of the five
public 3D draw entry points — `DrawColoredPrimitives`, `DrawIndexedColoredPrimitives`,
`DrawPrimitivesEx`, `DrawIndexedPrimitivesEx`, `DrawInstancedPrimitivesEx`. That is the narrowest
boundary all eleven `Queue*Draw()` command families pass through, so one guard covers every route
rather than ten that would drift apart.

**One correction the principal suite forced.** The first guard refused *every* topology.
`PointListPrimitiveTest.PointListIsNotAffectedByTriangleCulling` failed on it — and was right to. A
fill mode selects how a **polygon's interior** is rasterized; a line or point list has no interior,
both fill modes were measured **byte-identical** there, and this backend substitutes nothing. An
over-wide guard deletes a correct draw instead of preventing a wrong one, so the refusal is now
scoped to `TriangleList` and `TriangleStrip`. This is the same asymmetry rule
`REMED-GFX-DECL-GUARD` arrived at: check only what the caller's request actually reaches.

**Verification.** 12-route matrix (ordinary non-indexed/indexed 16- and 32-bit, nonzero
`vertexStart`/`startIndex`/`baseVertex`, both `DrawUser*` families in typed and raw-`void*` form, a
second effect family, instanced), each run under both fill modes; alternation and repeated-refusal
sequences; resource-replacement and teardown lifetime; wgpu-native validation and out-of-memory error
scopes clean; ASan+UBSan clean with the residual leaks A/B-classified as per-device wgpu-native
allocations that do not scale with refusals. WebGPU principal suite **5909 / 5876 passed / 28
skipped / 5 failed** — exactly the recorded baseline residuals. Positive controls unchanged on
Software, Vulkan, bgfx, SDL_GPU, EasyGL, D3D9, D3D11; Headless keeps its honest skip.

**`REMED-GFX-219` is untouched and still deferred** (§4.5) — opposite safety direction, as stated
above.

Full record: **`remediation/REMEDIATION_EXIT.md` §3.**

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

### `REMED-GFX-209` — **DONE 2026-08-04**. The WireFrame contract is per backend, and it was measured

**Record of status:** DONE. **Checkpoint blocker: RESOLVED. Integration blocker: NO. Scope: SMALL.**
**No production source file changed. No new public capability API. `audit/` untouched.**

#### The exact old assumption

```cpp
TEST(GraphicsDeviceCapabilityTest, DoesNotSupportWireFrame)
{
    GraphicsDevice gd;
    EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::WireFrame));
}
```

One test, in a file compiled once per backend and gated on nothing, encoding EasyGL's documented
`ℹ️ EasyGL N/A (GLES3)` row from `plan_graphics.md`'s XNA 4.0 coverage table as though every backend
shared it.

#### Reproduced first, on the unmodified test

Run before anything was changed: **PASSES on EasyGL**; fails `Actual: true / Expected: false` on
**Software, Headless, Vulkan, bgfx, WebGPU and SDL_GPU** — exactly the six of record, no more and no
fewer. A/B-proven a test assumption rather than a production regression on two independent axes: the
test file is byte-identical to `75b61fa4`, and every backend's `SupportsCapability` WireFrame arm is
unchanged between `75b61fa4` and HEAD.

#### The contract was then MEASURED on all ten backends, not read out of the source

A new asymmetric-triangle pixel oracle: a 256×256 `RenderTarget2D`, one non-indexed `DrawPrimitives`,
`CullMode::None`, a clear colour that is deliberately not black, one interior probe at least 40 px
from every edge, and one disjoint 25×25 probe per edge. Solid and WireFrame differ in nothing but the
fill mode, which is what makes the two frames comparable.

| Backend | `SupportsCapability(WireFrame)` | Measured rendering | Classification |
|---|---|---|---|
| Software | true | wireframe — total 556, interior **0**/1089, edges 25/23/25 | positive pixel oracle |
| Vulkan | true | wireframe — total 607, interior **0**/1089, edges 25/31/26 | positive pixel oracle |
| bgfx | true | wireframe — total 559, interior **0**/1089, edges 25/25/25 | positive pixel oracle |
| SDL_GPU | true | wireframe — total 607, interior **0**/1089, edges 25/31/26 | positive pixel oracle |
| D3D9 | true | wireframe — total 560, interior **0**/1089 (Wine/DXVK on `:99`) | positive pixel oracle |
| D3D11 | true | wireframe — total 559, interior **0**/1089 (Wine/DXVK on `:99`) | positive pixel oracle |
| **EasyGL** | **false** | **wireframe — total 559, interior 0/1089, edges 25/25/25** | positive oracle + **`REMED-GFX-219`** |
| **WebGPU** | **false** *(was `true`)* | **REFUSED — `System::NotSupportedException`, target total 0** *(was SOLID, total 18176 interior 1089/1089, byte-identical to Solid)* | **negative oracle — `WEBGPU-115` RESOLVED 2026-08-04** |
| Headless | true | rasterizes nothing at all, by design | honest skip + exact draw cardinality |
| D3D12 | (cross-built) | **not measured** — no D3D12 runtime under Wine | compiles; deliberately not called clean |

Solid rendered **18176** lit pixels on every measured backend but D3D9 (18160) — the triangle's exact
area, `|AB × AC| / 2`.

Three results in that table are worth stating in words rather than leaving in a cell:

- **No backend rejected a WireFrame request** *(when this table was measured, at `099b03c0`)*. A
  "deterministic rejection" arm therefore had an **empty registration set**, and manufacturing one
  would have required a production change that task was forbidden to make. It was not manufactured.
  **`WEBGPU-115` filled that arm on 2026-08-04**, with the production change its own ticket
  authorised — WebGPU is now its single member, and the table row above records the new reading.
- **WebGPU accepted the request and rendered solid.** `REMED-GFX-209` recorded that as an
  already-accepted deviation; exit reconciliation checked the claim and found nothing had ever
  accepted it (§4.6). The full-strength byte-identical assertion that arm carried is what made the
  correction provable, and it is preserved as red-first A/B evidence in `679cbed2`. WebGPU now
  reports the capability as `false` and refuses a polygon draw that would consume it, before
  anything is queued.
- **EasyGL's report contradicts its own renderer.** Its `GL_LINES` emulation landed in `a55397f7`
  (2026-06-30); the capability query claiming GLES3 has no wireframe was written in `33d6540b`
  (2026-07-17), two and a half weeks later. The renderer is right and the report is stale. New
  ticket **`REMED-GFX-219`**, OPEN, LOW, not a checkpoint blocker.

#### The corrected test architecture

Five arms replace one universal assertion, registered by backend rather than branched on at runtime:

1. `WireFrameCapabilityReportIsThisBackendsOwn` — one `EXPECT` per backend, stating only what that
   backend answers. EasyGL's arm asserts the current `false` so the baseline stays truthful, and
   **fails deliberately** when `REMED-GFX-219` lands.
2. `WireFrameLightsEveryEdgeAndLeavesTheInteriorUnfilled` — the positive oracle. Interior probe
   exactly 0; each of three disjoint edge probes ≥ 8 lit pixels in the exact ink colour; WireFrame
   total × 4 < Solid total; every lit pixel in the frame is ink or clear.
3. `WireFrameAndSolidAlternateWithoutStaleRasterizerState` — WireFrame → Solid → WireFrame, third
   frame **byte-identical** to the first and different from the Solid one.
4. `SolidRendersExactlyAfterAWireFrameDraw` — the recovery contract, run on every measured backend
   including WebGPU: no device loss, no stale state, no poisoned frame.
5. `WireFrameSilentlyRendersSolidGeometryOnThisBackend` — WebGPU's documented deviation, asserted.

Plus, on Headless: an **honest skip** stating that the route genuinely does not exist there, and
`WireFrameReachesTheBackendAsExactlyOneDraw`, which is the one place native draw cardinality can be
counted exactly — one public `DrawPrimitives`, one recorded draw, no retry and no second pass.

#### The oracle's teeth were proven by injection, not asserted

- Forcing WebGPU into the positive arm fails on *"filled 1089 interior pixels … that is a solid fill,
  not a wireframe"* and *"WireFrame covered 18176 pixels against Solid's 18176"*.
- Culling the geometry away fails the Solid control at total 0 — and revealed that EasyGL's
  `GL_LINES` emulation **survives triangle culling**, which is why `CullMode::None` in the oracle is
  load-bearing rather than decorative.
- Moving one triangle vertex zeroes exactly the two edge probes that touch it and names them
  individually.

#### False-positive audit

Every `FillMode`/`CullMode` mention in `tests/` was inspected: **7 files, 138 tests.**
`RasterizerStateTests` (24) and `GraphicsDeviceDefaultStateTests` (10) are state-object round-trips
making no rendering claim and are correct as they stand. The three pre-existing wireframe *pixel*
tests — `PointListPrimitiveTests`' point case and the bgfx range cases in `NonIndexedDrawRangeTests`
and `InstancedDrawRangeTests` — are real oracles and were not weakened. **1 defective (this one),
1 strengthened (this one), 0 new tickets from the audit.**

The blind spot, named concretely rather than implied: all three pre-existing wireframe pixel tests
are scoped to bgfx or to point topology, and **none of them compares WireFrame output against a Solid
control** — so WebGPU's silent solid fill passed every one of them. That is the gap the new positive
oracle closes.

#### The principal-suite baseline this task exists to produce

Full suite, every backend, 2026-08-04, on `:101`:

| Backend | Ran | Passed | Skipped | Failed | The failures |
|---|---|---|---|---|---|
| EasyGL | 5894 | 5887 | 6 | **1** | `XnbContainerFuzzTest` |
| Software | 5822 | 5775 | 45 | **2** | `XnbContainerFuzzTest`, `SetRenderTargets_FourTargets_DoesNotThrow` |
| Vulkan | 5877 | 5847 | 27 | **3** | `XnbContainerFuzzTest`, `CnjEffectTest`, `CnjStockEffectTest` |
| bgfx | 5921 | 5891 | 27 | **3** | `XnbContainerFuzzTest`, `CnjEffectTest`, `CnjStockEffectTest` |
| WebGPU | 5900 | 5867 | 28 | **5** | the above three, plus `SetRenderTargets_FourTargets_DoesNotThrow` and the 30 s `TwoProcessLoopbackTest` |
| SDL_GPU | 5808 | 5784 | 20 | **4** | `XnbContainerFuzzTest`, `CnjEffectTest`, `CnjStockEffectTest`, `TwoProcessLoopbackTest` |
| Headless | 5735 | 5688 | 44 | **3** | `XnbContainerFuzzTest`, `SetRenderTargets_FourTargets_DoesNotThrow`, `TwoProcessLoopbackTest` |

**`DoesNotSupportWireFrame` appears in none of them.** Every remaining failure is drawn from the
pre-existing set `REMED-GFX-DECL-GUARD` already named and classified — the XNB container fuzzer, the
two `Cnj*` cases feeding GLSL to a non-GL backend, `SetRenderTargets_FourTargets_DoesNotThrow`, and
one 30 s two-process networking test that is timing-flaky (it passed on Software in the full run and
failed in the A/B run below).

Two further checks, because "no failures appeared" is weaker than "no failures could have appeared":

- **A/B, Software with the changed suite excluded** (`--gtest_filter=-GraphicsDeviceCapabilityTest.*`):
  5809 ran, 5762 passed, 44 skipped, 3 failed — the same pre-existing set. The new tests neither
  cause nor mask a failure anywhere else in the process.
- **ASan**, `cmake-build-software-asan` with `libasan.so.8` proved linked by `ldd`: the changed suite
  plus `RasterizerStateTest` at 36/37 (1 skip), **zero AddressSanitizer reports**. Run because the
  new oracle reads a native surface back into a heap buffer; no broader sanitizer campaign was run,
  and none was warranted — no production code changed.

D3D9 and D3D11 run the corrected contract under Wine/DXVK on `:99` at **13/13** each (12 passed,
1 skip — the WebGPU-only deviation arm). D3D12 cross-builds; its runtime is unavailable here and is
**not** called clean.

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

`REMED-GFX-209`'s own false-positive audit, run 2026-08-04, likewise produced **no** further
test-only ticket. It did produce one *production* ticket, `REMED-GFX-219`, which is not a descendant
of `-201`/`-202` at all: it exists because the corrected oracle measured EasyGL's renderer for the
first time and found it contradicting EasyGL's own capability report.

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

  **Amended 2026-08-04.** Leaving them un-re-classified here left them **unclassified against the
  checkpoint-blocker rule**, which is a different thing from being deferred. Exit reconciliation
  closed that gap: every one of them is now classified in `remediation/REMEDIATION_EXIT.md` §4.3, and
  **none blocks the checkpoint**. Their severities and priorities of record are still unchanged, and
  they still belong to the remediation index, not to this plan. The one that needed real argument is
  **`REMED-GFX-121`**: it is genuinely the same *shape* as `WEBGPU-115` (accepted public op, silently
  wrong, no runtime boundary), and it does not block only because bgfx's SPIR-V/HLSL renderers are
  **not** the declared bgfx baseline — the principal suite runs the GLSL route, where output is
  correct, and the split is pinned by `BgfxPerInstanceWorldMatrixIsAppliedOnGlslRenderersOnly` with a
  named skip. **It becomes a blocker the moment bgfx's Vulkan renderer enters declared checkpoint
  scope.**

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
| `REMED-GFX-209` | ~~Test contract — `GraphicsDeviceCapabilityTest`~~ **DONE** *(2026-08-04)* | Test-contract defect (encoded one backend's assumption universally; **hid nothing**) | **Resolved.** Replaced by five backend-specific arms — positive pixel oracle, documented-deviation oracle, recovery oracle, honest skip, exact draw cardinality | LOW / P3 | P3 | **RESOLVED** | NO | none | ~~Before the post-integration checkpoint~~ — done | SMALL | Contract MEASURED on all ten backends, not assumed. No production file changed. Spawned `REMED-GFX-219` |
| `REMED-GFX-219` | EasyGL — capability reporting | **Capability under-report on an accepted path.** `SupportsCapability(WireFrame)` returns `false` while the `GL_LINES` emulation renders a correct wireframe | **Under-reports.** The renderer is correct; a caller that gates on the query disables a working feature. No false success, no wrong pixels | LOW / P3 | P3 | NO | NO | found by `REMED-GFX-209`'s oracle | With the EasyGL module, or any time — it is three lines and two stale comments | SMALL | Also correct `plan_graphics.md`'s `ℹ️ EasyGL N/A (GLES3)` coverage row, which records the same non-existent boundary. `WireFrameCapabilityReportIsThisBackendsOwn` fails deliberately when this lands |
| **`WEBGPU-115`** ✅ **DONE 2026-08-04** | **WebGPU — `FillMode::WireFrame`** | **Supported-path silent wrong result on a capability that reported `true`** — was the campaign's last checkpoint blocker; **RESOLVED**, see §4.6 | **NONE. This is the defect.** The capability query returns `true` (inherited `IGraphicsBackend` default; WebGPU never overrides it), `ApplyRasterizerState` accepts the request without throw, warning or log, a distinct `WGPURenderPipeline` is keyed on `wireframe` and **natively submitted**, and the frame comes back **byte-identical to Solid** (`total=18176 interior=1089/1089` for both, re-measured at `099b03c0`) | — / — | **P1** | **YES** | NO | none | **Immediately — it is the only thing between here and the checkpoint** | SMALL *(truthful-capability correction)* / LARGE *(real wireframe)* | Triaged §4.6 by exit reconciliation, 2026-08-04. `plan_webgpu.md:504`'s row is **`⬜` NOT DONE** — its content is the documentation task itself, never performed — so "already recorded and accepted" does not hold. `docs/webgpu-backend.md:488` is prose under *Important limitations*, unreachable from the public API and contradicted by the capability query. Exact inverse of `REMED-GFX-214` (loud pre-native rejection) and of `REMED-GFX-219` (under-report, not over-report) — **do not bundle with either.** Fix must also update `WireFrameSilentlyRendersSolidGeometryOnThisBackend`, which is written to fail when this lands, and `examples/webgpu_graphicsstate_test.cpp` Check G |
| `REMED-GFX-210` | Capability reporting — instancing | Capability-query / reporting + public-exception parity | **Throws**, but a `std::runtime_error` from the interface default; no advance query, no false success | LOW / P3 | P3 | NO | **CONDITIONAL** | none technical; adjacent to GFX-213 triage | First integration group adding a backend with no instanced path | MEDIUM | FNA has `FNA3D_SupportsHardwareInstancing` + `NoSuitableGraphicsDeviceException`; follow `REMED-GFX-185`'s report-what-you-do precedent |
| `REMED-GFX-211` | ~~Vulkan, Bgfx, WebGPU~~ **ALL THREE DONE — TICKET DONE** *(2026-08-03)* — instanced route | **Supported-path silent wrong result** — measured on all three, both stream sides | **None.** Classic 1+1 draw is accepted and submitted; instance records 0..3 consumed instead of 1..4, and the per-vertex stream renders its decoy | MEDIUM / P2 | **P1** | ~~REVIEW~~ **YES** *(triaged 2026-08-03)* | NO | GFX-202 (value already delivered); precedent GFX-122, GFX-123 | ~~Immediate remediation campaign~~ **COMPLETE — checkpoint blocker RESOLVED** | MEDIUM | Triaged: WebGPU pixel measurement taken; per-vertex side confirmed on all three; the two streams fail **independently** (`both-offsets-ignored`), so a fix must carry both. D3D9 unmeasured — no D3D display. **Vulkan corrected §4.1: instance offset into the copy's source base, per-vertex offset folded into `vkCmdDrawIndexed`'s own `vertexOffset` — the per-vertex loss point was `d.baseVertex`, not the neighbouring copy. Bgfx corrected §4.1 the same way at its own two expressions: the instance copy's source base became `cpuData.data() + vertexOffset * instStride`, and the per-vertex offset joined `baseVertex` in `bgfx::setVertexBuffer`'s `_startVertex` — bgfx has no draw-time base-vertex argument, so that start element IS the addend, which is also why the ordinary bgfx route was already correct (it folds into `params.baseVertex` upstream) and the instanced one was not (it folds nothing, by GFX-202 design). Vulkan and Bgfx now both in `CNA_INSTANCED_BINDING_OFFSET_ORACLE`; WebGPU corrected §4.1 at its own two expressions, and the per-vertex loss point was Vulkan's shape rather than bgfx's: `command.vertexData` copies the whole per-vertex buffer and the replay binds it at native offset 0, so `command.baseVertex = params.baseVertex` was the only term that could carry the offset and now carries `params.baseVertex + perVertexOffset`; the instance copy's source base became `vertexOffset * instVbStride`. All three backends are now in `CNA_INSTANCED_BINDING_OFFSET_ORACLE`, which emptied `CNA_INSTANCED_OFFSET_DEFECT_MEASURED` — the macro and its four arms were deleted, leaving the triage legs' `#else` UNCLASSIFIED arm for the still-unmeasured D3D9.** |
| `REMED-GFX-212` | **~~Vulkan, WebGPU~~ BOTH DONE — TICKET DONE** *(2026-08-03)*; D3D11, D3D12 remain *(source-identified, unmeasured)* — stock instanced shader | **Supported-path silent wrong result.** Reference settles it: case A | **None.** The draw renders silently in the wrong colour; no diagnostic | LOW / P3 | ~~REVIEW~~ **P1** | ~~REVIEW~~ ~~**YES**~~ **NO — RESOLVED 2026-08-03** | NO | ~~an FNA reference determination~~ — **answered** | **Immediate remediation campaign** | MEDIUM *(LARGE if shader sources change)* | Triaged: FNA's shader index has no instancing term, so the same VS runs on both routes. Vulkan/WebGPU colour their **own** ordinary route from the stream and the instanced one from `DiffuseColor`. D3D11/D3D12 still unmeasured — no D3D display |
| `REMED-GFX-213` | **~~Bgfx, Vulkan, WebGPU~~ ALL THREE DONE — TICKET DONE** *(2026-08-03)* — per-instance divisor *(scope widened by triage)* | **Supported-path silent wrong result.** Escalation condition confirmed | **None.** An adequately sized instance buffer clears every check and renders `InstanceFrequency = 2` at divisor 1 | MEDIUM / P2 | ~~P2~~ **P1** | ~~REVIEW~~ **YES** *(triaged 2026-08-03)* | NO | GFX-202; distinct from GFX-121; shares its copy site with GFX-211 | ~~Immediate remediation campaign~~ **COMPLETE — checkpoint blocker RESOLVED** | MEDIUM | Triaged: `frequency` occurs **0 times** in all three backends' sources. Not an absent native capability — all three can emulate a divisor in the buffer they already build. **Vulkan corrected §4.3 by replication: no divisor extension is enabled (API 1.1, `VK_KHR_swapchain` only), so the binding keeps divisor 1 and slot `i` takes source record `vertexOffset + i / frequency`. Destination cardinality unchanged, so no pipeline-key term was needed. Bgfx corrected §4.3 the same way, after classifying its native capability from the installed header rather than a grep count: bgfx's whole public instancing surface (`allocInstanceDataBuffer` / `setInstanceDataBuffer` / `setInstanceCount`, `InstanceDataBuffer{data,size,offset,num,stride,handle}`) has no divisor, step-rate or frequency parameter at all, so category C again — slot `i` takes source record `vertexOffset + i / frequency` in the instance-data buffer the route already allocates. Cardinality measured identical at frequency 1 and 2 (5 submissions / 16 prims / 1024 transient B for a 4-draw frame at both). Bgfx's over-long-range gate was generalised from `instanceCount * stride` to the highest source record at the same time. WebGPU corrected §4.3 the same way and classified its native capability from the installed wgpu-native v29.0.1.1 headers rather than a grep count: `WGPUVertexBufferLayout` is only `{nextInChain, stepMode, arrayStride, attributeCount, attributes}`, `WGPUVertexStepMode` is only `{Undefined, Vertex, Instance}`, none of the eleven `WGPUNativeSType` chains extends a vertex layout or vertex state, and no `WGPUNativeFeature` adds a step rate — category C again, so slot `i` takes source record `vertexOffset + i / frequency` in the command's own `instVbData`. Cardinality measured by a new permanent WebGPU regression (22/22) against the backend counters and wgpu-native's `wgpuGenerateReport()`: 1 pass, 1 submit, 0 new pipeline variants at every frequency and offset pair, and no native buffer added by doubling the instance count.** |
| `REMED-GFX-214` | WebGPU — ordinary route, stride 20/24 with `TextureEnabled = false` | **Loud deterministic unsupported-path rejection.** Not a silent wrong result | **Safe.** `QueueColoredDraw`'s first statement throws `std::invalid_argument` **before** any command is queued, buffer written or pass opened. No partial submission, no device loss, no memory corruption. Text recorded verbatim by an existing test | MEDIUM / — | P2 | **NO — DEFERRED** *(triaged 2026-08-04)* | NO | none | Post-audit, with the WebGPU backend's own work | SMALL | Triaged from source, no run needed: stride 20/24 reach `QueueTexturedDraw` only when `params.texture0 != nullptr`; with the texture off the draw matches no branch and falls through the documented "unmatched stride/texture combination" tail. No capability claims it either way, which costs nothing here because the failure is loud. Smallest fix unchanged: route stride 20/24 with a null `texture0` to the textured pipeline against the default white texture, as Vulkan's stride-24 arm already does |
| `REMED-GFX-217` | **Seven rasterizing backends** — Vulkan, WebGPU, Software, SDL_GPU, D3D9, D3D11, D3D12. **Headless removed at triage** | ~~Supported-path silent wrong result~~ **Now a deterministic pre-native rejection** on custom declarations that collide with a built-in stride *(`REMED-GFX-DECL-GUARD`, 2026-08-04)* | **Safe declared boundary since 2026-08-04.** An unrepresentable declaration throws `System::NotSupportedException` before any native layout, command or submission exists. Before the guard it was partial and differed per backend: WebGPU and SDL_GPU accept only stride 16 without a bound texture, so six of the oracle's seven declarations never reach native submission; Software and D3D reject out-of-table strides loudly; **Vulkan has no stride guard at all** and is the widest surface | HIGH / — | **P1** | **NO — RESOLVED 2026-08-04 by `REMED-GFX-DECL-GUARD`** | NO | shares the canonical stride table with GFX-203…208; distinct from them (this is fidelity at ONE stream, those are several) | **Strategy C guard before the checkpoint; full translators during modularization with GFX-203…208** | Guard **SMALL–MEDIUM**; full translation **LARGE** | Triaged §4.4. Built-in declarations are correct on all ten backends — every example, `SpriteBatch` and stock effect uses only these, which is why this was never visible. **Headless does not belong in this ticket:** it rasterizes nothing, so its empty override cannot produce a wrong result. D3D9/11/12 are source-derived, not measured — no D3D display — but the loss point is the **shared** `D3DCommon` table whose public consequence is already measured at pixel level on Vulkan and Software. Guard predicate is the asymmetric one and shipped in §4.4.7: R0 record advance, R1 no native fetch into declared bytes, R2 same usage **and usage index** at the same offset and format, R3 in-stride, R4 no overlap. A strict equality test would wrongly reject `positionOnly12`, which still renders correctly on Vulkan. **Guard DONE 2026-08-04; the translators stay OPEN here.** |
| `REMED-GFX-218` | EasyGL — attribute location from element **index**, not semantic | ~~Supported-path silent wrong result~~ **Now a deterministic pre-native rejection on the stock path** *(`REMED-GFX-DECL-GUARD`, 2026-08-04)*; different mechanism from GFX-217 and a different wrong colour (`(102,45,0,255)` vs Vulkan's/Software's `(0,0,0,63)`) | **Safe declared boundary since 2026-08-04** on the stock path; custom `ShaderEffect` unchanged. Before the guard: **none** on the three colliding declarations | HIGH / — | **P1** | **NO — RESOLVED 2026-08-04 by `REMED-GFX-DECL-GUARD`** | NO | must not disturb GFX-201's `FirstLocationForStream` or GFX-202's `PerVertexLocationCount` | **Guard before the checkpoint; per-family semantic placement post-checkpoint** | Guard SMALL; **full fix MEDIUM, not SMALL** | Triaged §4.4.5. The one-line `location = SemanticSlot(usage)` fix is **wrong** for three independent reasons: EasyGL's stock shaders bind location 1 to `aColor` at stride 16, `aUV` at 20 and `aNormal` at 32, so there is no global semantic→location function; `ApplyLayout` runs at **upload** time with no program bound; and the index convention is the **documented contract** of the custom-`ShaderEffect` path, where a GLSL shader may declare inputs CNA has no semantic for. Correct fix is per-family placement on the stock path only, at draw time in `SelectProgram` (4 call sites). Guarding EasyGL mattered at the checkpoint: guarding the other seven and not EasyGL would have left EasyGL the **only** backend silently rendering the wrong thing. **Guard DONE 2026-08-04** — `SelectProgram`'s cascade was factored into one `SelectStockProgramShape` that both the selection and the guard read, and the custom-`ShaderEffect` path is untouched and proven so by its own control |

---

## 10. The pending integration branches

> **The branch inventory is dynamic and is no longer maintained in this file.**
> **Authoritative source: `remediation/INTEGRATION_BRANCH_INVENTORY.md`**, derived from Git refs.
>
> **Current inventory as of the 2026-08-04 exit-reconciliation fetch (checkpoint candidate on
> `feature/audit`): 21 logical pending integration lanes.** N may change before integration begins.
> The count is a snapshot, not an invariant. Do not quote it without its fetch date and commit.
>
> **Do not restate 19.** That figure was derived without a fetch and is retained in the inventory
> document only as a labelled historical snapshot. Two corrections it hid: the two remote-only
> `claude/*` lanes were invisible, and **Magnum and Wicked are audit-stacked lanes** (13 and 10 own
> commits on `feature/audit` @ `2338b44f7`), not develop-forked backends 768/765 commits ahead.
>
> **`feature/direct2d` is the final actively-developed lane** — freeze it at a known head before
> integration begins.

**Historical snapshot (2026-08-03), preserved as a record of what was true then — not current:**
CNA carried **19** unintegrated branches (excluding `develop`, `master` and this remediation branch
`feature/audit`):

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

**What changed by 2026-08-04.** The count is still 19, but **four of those refs no longer exist** —
`origin/claude/diligent-engine-backend-cna-01cs23`, `.../llgl-gr-backend-cna-3orpwo`,
`.../sokol-gfx-cna-backend-8s3oo8` and `.../noxna-graphics-api-extension-lihfjk` were renamed or
promoted to `feature/diligent`, `feature/llgl`, `feature/sokol` and `origin/feature/ext`. The
coinciding total is exactly the false stability a fixed number produces, which is why the inventory
now lives in its own regenerated document. **`REMED-GFX-201`/`-202`'s overlap figures were
recomputed at `099b03c0` and all still hold** — 0 of 19 contain `fc0dd2a2`, 16 of 19 touch
`GraphicsDevice.cpp`, 13 of 19 touch `IGraphicsBackend.hpp`, 10 of 19 touch `GraphicsCapability.hpp`.

**`feature/gl` is a cross-repository lane** with a mandatory MetaGL → EasyGL → CNA order; EasyGL and
MetaGL are **development-complete**, merely unmerged into their own `develop` branches. See the
inventory document §4. **Magnum and Wicked Engine have no Git evidence anywhere in this repository
or workspace** and are deliberately **not** counted — see the inventory document §5.

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
| **Before branch integration** | ~~`REMED-GFX-211`, `REMED-GFX-212`, `REMED-GFX-213` triage (§4)~~ — triage done 2026-08-03, and **all three tickets are now DONE on every backend they name**. `REMED-GFX-215` and `REMED-GFX-216`, spawned in turn by `REMED-GFX-212` and `REMED-GFX-215`, were each triaged against §2, each confirmed a supported-path silent wrong result, each treated as an immediate checkpoint blocker, and **both are DONE (2026-08-03)**. **The checkpoint-blocker set inherited from this cluster is EMPTY.** `REMED-GFX-216` reclassified correctly on re-reading: bgfx accepted a supported single-stream draw, reached native submission, raised nothing, and rendered 27385 of 50752 pixels — deterministic and silently wrong, so P1 rather than deferred capability work. Its fix spawned **`REMED-GFX-217`** and **`REMED-GFX-218`**, which ARE of the same silent-wrong-result class on the other backends. ~~and must be triaged here before a checkpoint is taken~~ — **that triage ran on 2026-08-04 (§4.4).** Outcome: neither is a blocker in the "must be fully implemented" sense, but the checkpoint is **not honest without a bounded safety guard**, because both silently render supported public draws from the wrong bytes. The pre-checkpoint work is **one** Strategy-C task covering all eight affected rasterizing backends (seven stride-table + EasyGL's stock path) with no public API change; full declaration translation is deferred to modularization alongside `REMED-GFX-203` … `-208`. **Headless was removed from `REMED-GFX-217`'s scope at triage** — it rasterizes nothing. `REMED-GFX-214` (MEDIUM, OPEN) was triaged at the same time and is confirmed **not** a blocker: `QueueColoredDraw` throws before any command is queued, so the failure is loud, deterministic and pre-native — **DEFERRED** |
| **While adapting a particular branch** | None of §5. Only the adaptation checklist above, plus `REMED-GFX-210` evidence-gathering when a branch adds a backend with no instanced path |
| **After all 19 branches are integrated** | ~~`REMED-GFX-209`~~ — **DONE 2026-08-04**, the clean principal-suite baseline is established and the six known-cause reds are gone; `REMED-GFX-210`. `REMED-GFX-219`, spawned by it, is OPEN and blocks nothing |
| **During modularization** | `REMED-GFX-203` … `REMED-GFX-208`, and `REMED-GFX-213`'s implementation if triage lands it on the capability side. **Added 2026-08-04:** `REMED-GFX-217`'s full per-backend declaration translation and `REMED-GFX-218`'s per-family semantic placement, both after their Strategy-C guard has shipped |
| **During NoXNA graphics-extension work** | Nothing from this plan is a prerequisite; see §12 |

### The exit sequence — the shortest honest path to the checkpoint

Decided 2026-08-04 from §4.4's triage. **Four steps, in order.** Nothing else from this plan is a
prerequisite for taking the checkpoint.

1. **`REMED-GFX-DECL-GUARD` — DONE (2026-08-04). See §4.4.7 for what actually shipped.** One
   bounded safety task covering all eight affected rasterizing backends at once. One shared pure
   predicate — `include/CNA/Internal/Graphics/VertexDeclarationFidelity.hpp`, **not**
   `Backends/Common/`, and header-only, because each backend is its own static library linked
   against `cna_backend_graphics_common` and SharpRuntime rather than against the CNA library, so a
   translation unit under `src/` is not visible to them and a shared build target would have been a
   new abstraction this task was forbidden to add. It implements §4.4.4's asymmetric rule as R2 and
   adds R0/R1/R3/R4, raising `System::NotSupportedException` before anything native is created or
   submitted, exactly as `REMED-GFX-216`'s bgfx translator already does. Called at the top of each
   backend's three `Draw*PrimitivesEx` routes — **not** from `SetVertexDeclaration`, which cannot
   tell a geometry stream from a per-instance one — and from EasyGL's stock-program path only,
   never on the custom-`ShaderEffect` path, whose element-index convention is a documented
   contract. **Touched no public API, no `GraphicsDevice.cpp`, no `IGraphicsBackend.hpp`, no
   `GraphicsCapability.hpp`** — therefore zero contention with any of the 19 branches.
2. **`REMED-GFX-209` — DONE (2026-08-04). See §7 for the measured per-backend contract.** The clean
   principal-suite baseline is established: the universal `DoesNotSupportWireFrame` assumption is gone
   from all six backends whose suites it reddened, no production file changed, no new public
   capability API was added, and the remaining failure on every backend is one this plan already
   names. It spawned one new ticket, **`REMED-GFX-219`** (EasyGL under-reports `WireFrame`), which is
   OPEN, LOW, and blocks neither integration nor the checkpoint.
3. **Exit reconciliation** — statuses, provenance and the deferred set. **DONE (2026-08-04).**
   Produced `remediation/REMEDIATION_EXIT.md` and
   `remediation/INTEGRATION_BRANCH_INVENTORY.md`.
4. **Take the checkpoint.** — **NOT YET TAKEN.** Requires a fresh exit reconciliation; see below.

3½. **`WEBGPU-115` — blocker found by exit reconciliation (2026-08-04), and **RESOLVED the same
   day**.** See §4.6.

**Steps 1, 2, 3 and 3½ are complete. Step 4 has not been taken, and this ticket did not take it.**
The blocker `WEBGPU-115` is closed and no other checkpoint blocker is currently recorded, but the
checkpoint decision belongs to a separate exit-reconciliation session: it must re-derive the blocker
inventory and the branch inventory from refs as they stand at that moment, not from this record. The
next single action is **re-run exit reconciliation**; take the signed checkpoint tag only if it
confirms the blocker set is empty.

**Explicitly NOT in the sequence:** `REMED-GFX-203` … `REMED-GFX-210` (deferred, unchanged);
`REMED-GFX-214` (safe loud boundary, deferred); `REMED-GFX-217`'s seven native declaration
translators; `REMED-GFX-218`'s per-family semantic placement. **No backend gains a declaration
translator before the checkpoint.**

**Why not simply defer everything and document the boundary.** The oracle already prints and asserts
every measured deviation and fails the moment a backend is corrected, so the *plan-level* boundary is
already honest. What is not honest is the **runtime**: a caller passing a custom `VertexDeclaration`
that collides with a built-in stride gets wrong pixels or a blank frame with no diagnostic on eight
of ten backends. Custom vertex structs are core XNA, not an exotic corner. Step 1 is the smallest
change that makes the runtime tell the truth, and it is strictly smaller than any one of
`REMED-GFX-203` … `-208`.

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

`NOXNA.md` already lists the tasks this plan is adjacent to — `N50` (`InstancedRendererEXT`, the
instance-stream helper over the existing `DrawInstancedPrimitives`) and `N51` (`LodGroupEXT` distance
selection), under §8 *Geometry helpers*, both `⬜ Not started`. The `DrawInstancedPrimitives` overload
this list previously named alongside them has since shipped and is no longer a backlog item.

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

1. ~~**Complete checkpoint triage**~~ — **DONE.** `REMED-GFX-211`, `REMED-GFX-212` and `REMED-GFX-213`
   are DONE; `REMED-GFX-209` no longer needs deciding against a green-suite criterion because it is
   DONE and its reds are gone. Bounded: three measurements
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
- `REMED-GFX-203` … `REMED-GFX-210` **no longer form an automatic immediate execution chain.** They are
  DEFERRED with disposition POST-AUDIT PLAN, targeting this file.
- The intensive campaign **may proceed toward reconciliation and the checkpoint without implementing
  every backend capability descendant.**
- ~~**Checkpoint review must still inspect the supported-path silent-wrong-result candidates** — §4, at
  minimum `REMED-GFX-211`, `REMED-GFX-212` and `REMED-GFX-213`'s escalation condition.~~ **That
  inspection ran on 2026-08-03.** All three were confirmed supported-path silent wrong results,
  **checkpoint blocker YES**, and stayed in the immediate remediation campaign rather than being
  deferred to this plan. **All three are now DONE on every backend they name (2026-08-03), and this
  cluster's checkpoint-blocker set is EMPTY.** `REMED-GFX-212` spawned `REMED-GFX-214` and
  `REMED-GFX-215`. `REMED-GFX-215` was a supported-path silent wrong result of the same class on
  bgfx — its instanced shader emitted the raw COLOR0, dropping both `DiffuseColor` and
  `VertexColorEnabled` — was triaged against §2, held as an immediate checkpoint blocker, and is
  **DONE (2026-08-03)**. It spawned `REMED-GFX-216` (bgfx derived its native vertex layout from the
  buffer stride rather than the `VertexDeclaration`), which was **reclassified P1 on re-reading** —
  a supported draw accepted, submitted, silently rendering 27385 of 50752 pixels with correct
  colours — held as a checkpoint blocker, and is likewise **DONE (2026-08-03)**. Running its
  declaration oracle on every backend then spawned **`REMED-GFX-217`** (Vulkan, WebGPU, Software,
  SDL_GPU, D3D9/11/12 and Headless leave `SetVertexDeclaration` an empty override and pick a layout
  by stride) and **`REMED-GFX-218`** (EasyGL consumes the declaration but binds attributes by
  element index rather than by semantic). **Both are silent wrong results on supported public draws
  and therefore belong to this same P1 class** — ~~they must be triaged before a checkpoint~~, and
  neither was fixed under `REMED-GFX-216`, whose scope was bgfx production alone. ~~`REMED-GFX-214`
  remains OPEN, uninvestigated, and is not a blocker: it fails loudly and deterministically.~~
- **That triage ran on 2026-08-04 (§4.4), and it is the last one this cluster needs.** All three of
  `REMED-GFX-214`, `-217` and `-218` are classified, none is implemented, and no production file was
  touched by the triage. `REMED-GFX-214` is a **safe loud boundary — DEFERRED**: `QueueColoredDraw`
  throws before any command is queued, buffer written or pass opened. `REMED-GFX-217` and
  `REMED-GFX-218` are genuine silent wrong results, but their **full** fix is per-backend capability
  work of the same shape as `REMED-GFX-203` … `-208` and is deferred with them; what the checkpoint
  requires instead is **one bounded guard task** (§10, *The exit sequence*) that rejects a
  declaration a backend cannot faithfully represent, on all eight affected rasterizing backends, with
  **no public API change**. **Headless was removed from `REMED-GFX-217`'s scope**: it rasterizes
  nothing, so its empty override cannot produce a wrong result. `REMED-GFX-218` was classified
  **MEDIUM, not SMALL** — EasyGL has no global semantic→location function to write, its `ApplyLayout`
  runs at upload time with no program bound, and its element-index convention is the documented
  contract of the custom-`ShaderEffect` path.
- **Deferred work stays visible** through this file, and through the `DEFERRED` status and
  `Target plan: plan_postaudit.md` pointer on every affected row in
  `remediation/REMEDIATION_INDEX.md` and `remediation/REMEDIATION_PROGRESS.md`.

**The remediation campaign is not declared complete by this document.** No checkpoint has been taken
and no tag has been created.

---

## 17. `REMED-GFX-220` — static initialization order fiasco between `BlendState` and `Color`

**Status:** **DONE** (fixed 2026-08-05, see *Resolution* below) · **Severity:** HIGH ·
**Discovered:** 2026-08-05, Batch 1 stabilization ·
**Introduced:** 2026-06-06 by `2345f8fc` — re-verified by pickaxe (`git log -S`), and
`git merge-base --is-ancestor 2345f8fc d79214e7` confirms it predates
`cna-post-audit-remediation-phase1` · **Owner lane:** core Graphics, not a backend lane

### Finding

`src/Microsoft/Xna/Framework/Graphics/BlendState.cpp` defines four namespace-scope `BlendState`
constants (`Additive`, `AlphaBlend`, `NonPremultiplied`, `Opaque`, lines 6-9). Their constructor
chain reaches the default constructor, whose member init list contains:

```cpp
// BlendState.cpp:22
, blendFactor_(Color::White)
```

`Color::White` is a namespace-scope constant defined in a **different translation unit**
(`src/Microsoft/Xna/Framework/Color.cpp:113+`). The relative order of dynamic initialization across
translation units is unspecified, so when `BlendState.cpp`'s initializers run first, `Color::White`
is still zeroed `.bss` and the copy reads an object whose lifetime has not begun.

`Color` is polymorphic (it derives from `IPackedVectorT<UInt32>` and `IEquatable<Color>`), so the
read is detectable, and UBSan's vptr check catches it exactly:

```
include/Microsoft/Xna/Framework/Color.hpp:22:12: runtime error: member access within address
0x... which does not point to an object of type 'Color'
0x...: note: object has invalid vptr        <-- memory is all zeros

#0 Color::Color(Color const&)                     Color.hpp:22
#1 BlendState::BlendState()                       BlendState.cpp:22
#2 BlendState::BlendState(name, ...)              BlendState.cpp:28
#3 __static_initialization_and_destruction_0      BlendState.cpp:6
#4 _GLOBAL__sub_I__ZN...BlendState8AdditiveE      BlendState.cpp:74
```

### Why it was not seen before

The manifestation depends on link order, which differs per backend. In the Batch 1 sanitizer matrix
it fires under **OPENGL1** and not under OPENGL4 or OPENGL2, though all three link the same two
translation units. It is latent in every configuration, not an OPENGL1 defect.

### Consequence

The four static presets receive `blendFactor_ = Color(0,0,0,0)` (transparent black) instead of
`Color::White`. `GraphicsDevice::setBlendStateProperty` consumes that value
(`GraphicsDevice.cpp:2448-2456`) and forwards it to the backend as the blend colour, so applying a
stock preset writes a wrong device blend factor, and a subsequent user `BlendState` selecting
`Blend::BlendFactor` / `Blend::InverseBlendFactor` blends against the wrong colour.

**No test covers this.** `BlendStateTest.DefaultBlendFactorWhite` constructs a `BlendState` at
runtime, after static initialization has completed, so it exercises the constructor and passes even
in an affected binary. Nothing asserts the *static* presets' `BlendFactor`.

### Evidence boundary — what is and is not proven

- **Proven:** the UB itself (UBSan vptr violation), that the source object is all zeros at copy
  time, and that production forwards the value to the backend. Reproduced 2/2.
- **Not proven:** an observably different rendered frame. `gdb` is unavailable in this environment,
  so the stored value was not read back after initialization, and no pixel oracle was run against a
  `Blend::BlendFactor` blend mode. Whether this satisfies exit criterion **E3** (supported-path
  silent wrong result) is therefore **arguable, not demonstrated**.

### Suggested fix, as originally filed (superseded — see *Resolution*)

Remove the cross-translation-unit static dependency rather than reorder anything: construct the
value directly, e.g. `blendFactor_(Color(UInt32{0xFFFFFFFFU}))`, or give `Color::White` a
function-local-static accessor. Then add a test asserting each static preset's `BlendFactor`, since
that is the coverage gap that let a two-month-old defect survive.

Fixing it was out of scope for the Batch 1 stabilization session, which was bounded to stabilization
and forbidden from broad remediation.

---

### Resolution (2026-08-05)

#### Which binary, and why only one

The manifestation was pinned without guesswork by decoding `.init_array` — the execution order of
each translation unit's `_GLOBAL__sub_I_*` — in all 38 OpenGL 1 sanitizer executables and comparing
the index of `BlendState`'s initializer against `Color`'s:

| OpenGL 1 sanitizer binaries | Order | Result |
|---|---|---|
| `cna_test_opengl1_anisotropic_gl_state` | BlendState **#167** before Color **#185** | **hazardous — reproduces** |
| the other 37 | Color before BlendState | unaffected |

That is the whole reason a two-month-old latent defect produced exactly *one* UBSan line in the
Batch 1 matrix. The same decode over the non-OpenGL-1 executables found none hazardous — including
`CnaTests`, where Color is **#1008** and BlendState **#1011**, which matters for the regression test
below.

#### Reproducer (execution proof, not inspection)

- Tree `cmake-build-opengl1-asan` — `OPENGL1`, `Debug`, `CNA_SANITIZE=address,undefined`,
  GCC 14.2.0, `CMAKE_CXX_COMPILER_LAUNCHER=ccache`, `CNA_TEST_DISPLAY=:101`, Unix Makefiles.
- Binary `./cna_test_opengl1_anisotropic_gl_state`; CTest name `OpenGL1_Anisotropic_GlState`.
- Environment `ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=print_stacktrace=1`, `DISPLAY=:101`,
  `SDL_VIDEODRIVER=x11`; working directory the build tree.
- Mechanism is **UBSan's `vptr` check**, not ASan's initialization-order detector. The process
  **exits 0** — the diagnostic never failed a test, which is why only a log scan surfaced it.

Reproduced verbatim, including the all-zero memory dump and `note: object has invalid vptr`, with
frames `#0 Color::Color(Color const&) Color.hpp:22` → `#1 BlendState::BlendState()
BlendState.cpp:22` → `#3 __static_initialization_and_destruction_0` → `#4 _GLOBAL__sub_I_…`.

#### The originally suggested form does not compile

`Color(UInt32)` is **private** (`Color.hpp:557`); only `Color.cpp` may use it. `blendFactor_(Color(
UInt32{0xFFFFFFFFU}))` fails with *"is private within this context"*. The applied fix therefore uses
the XNA-public component constructor:

```cpp
, blendFactor_(255, 255, 255, 255)
```

`Color(intcs,intcs,intcs,intcs)` delegates to `Color(bytecs,…)`, whose body packs
`(A<<24)|(B<<16)|(G<<8)|R`; `ToByte(255) = clamp(255,0,255) = 255` and
`ToPackedComponent(255) = 255 & 0xFF = 255`, so the result is `0xFFFFFFFF` — **byte-identical to
`Color::White`** (`Color.cpp:250`). Public API, ABI and every other `BlendState` default are
untouched; no lazy initialization, no function-local static, and no new ordering dependency is
introduced. A *call* into another translation unit is always safe during static initialization; only
*reading another unit's object* is not, and that is what was removed.

#### The evidence boundary is now closed

§17 originally recorded the wrong rendered/stored value as **not proven**, because `gdb` is
unavailable here. A deterministic probe settles it without a debugger: the same source linked twice
against the real `libCNA.a`, once with `BlendState.cpp.o` ahead of `Color.cpp.o` on the link line and
once behind, which forces the `.init_array` order instead of accepting whatever the build produced.

| | hazardous order | safe order |
|---|---|---|
| **pre-fix** | `Additive` / `AlphaBlend` / `NonPremultiplied` / `Opaque` all `0x00000000` — **PROBE FAIL** | all `0xFFFFFFFF` |
| **post-fix** | all `0xFFFFFFFF` — **PROBE PASS** | all `0xFFFFFFFF` |

So the four presets really did carry **transparent black** instead of opaque white in the hazardous
order, and `GraphicsDevice::setBlendStateProperty` forwards exactly that value to the backend as the
blend colour. The consequence is a **silent wrong value on a supported path**, no longer an
arguable one. (Probe sources live under the shared `build-probe/` tree and its binaries are
disposable; the commands and both outcomes are recorded here, which is the durable record.)

#### Post-fix verification

- The reproducer binary is clean — **0 runtime errors** — while its **hazardous link order is
  unchanged** (`BlendState #167` still precedes `Color #185`). The diagnostic disappeared because the
  dependency was removed, not because the link order shifted; that control matters.
- `BlendState.cpp.o` no longer carries an undefined reference to
  `_ZN9Microsoft3Xna9Framework5Color5WhiteE` at all — the cross-unit read is gone from the object
  code, independently of any link order.
- Full OpenGL 1 sanitizer matrix: **38/38 pass**, **0 UBSan runtime errors** (was 1), **0 ASan
  errors**, 114 leak reports of which **114 name `libGLX_mesa.so.0` at frame `#1`** and **0 name
  `src/`** — the same driver profile §8 recorded, with the finding removed.

#### Regression test — and an honest limit

`BlendStateTest.PredefinedBlendFactorIsOpaqueWhite` asserts all four presets' `BlendFactor`
packed value is `0xFFFFFFFF`, comparing against the packed literal rather than `Color::White` so the
oracle does not itself depend on another translation unit's static object. This closes the coverage
gap that let the defect survive: `DefaultBlendFactorWhite` only ever constructed a `BlendState` at
run time, after static initialization had completed.

**It is not, by itself, a detector for this defect.** In `CnaTests` the link order is Color-first,
so it passes against the pre-fix code too. The deterministic pre-fix/post-fix oracle is the forced
link-order probe above; the gtest is the permanent value contract, and it *will* fail in any future
binary that links hazardously. Recording that distinction rather than claiming a fail-then-pass the
test cannot deliver.

#### Same-pattern scan

Mechanical, not grep-based: for each of the 253 CNA objects, start at
`_GLOBAL__sub_I_*` / `__static_initialization_and_destruction_*`, take the transitive closure of
calls to functions defined in the same object, and report relocations against symbols undefined
there but defined as data elsewhere. (A direct-reference-only scan misses this very defect — the
read sits two frames deep inside `BlendState::BlendState()`, not in the static-init body. A source
grep misses it too.)

| Site | Reads | Verdict |
|---|---|---|
| `BlendState.cpp` | `Color::White` (`nm` type `B`, dynamically initialized) | this defect — **fixed** |
| `GestureDetector.cpp` | `Vector2::Zero` (`nm` type `B`, dynamically initialized) | **second instance — see §18** |
| `GraphicsAdapter.cpp`, `MediaPlayer.cpp`, `BlendState.cpp` | vtable symbols (`nm` type `V`) | **benign**: vtables are `PROGBITS` in `.data.rel.ro`, relocated by the dynamic linker before any static initializer runs |

No other cross-translation-unit static-initialization read exists in the CNA library.

#### Not a Batch 1 regression

Introduced 2026-06-06, an ancestor of the phase-1 checkpoint, latent in every configuration and
gated only by link order. Every Batch 1 lane merge is unrelated to it.

---

## 18. `REMED-GFX-221` — `GestureDetector` statics copy `Vector2::Zero` across translation units

**Status:** **DONE — RESOLVED by `FINAL-STAB-001`, 2026-08-09** · **Severity:** LOW ·
**Discovered:** 2026-08-05, by the `REMED-GFX-220` same-pattern scan · **Owner lane:** core Input
(`src/CNA/Internal/Input/`), not Graphics

> **Numbering note.** Filed in this plan's `REMED-GFX-*` series because it was found by §17's scan
> and shares its exact root cause, but the defect is in Input. Renumber under a CORE/INPUT prefix if
> the campaign prefers subsystem-accurate IDs.

### Historical finding and strict reproduction

`src/CNA/Internal/Input/GestureDetector.cpp` defines namespace-scope state at lines 51-63, five
members of which are copy-initialized from `Vector2::Zero`:

```cpp
Vector2 activeFingerPosition = Vector2::Zero;   // and lastUpdatePosition, pressPosition,
                                                // secondFingerPosition, velocity
```

Before the fix, `Vector2::Zero` was defined at
`src/Microsoft/Xna/Framework/Vector2.cpp:88` as
`const Vector2 Vector2::Zero(0.0f, 0.0f)`, its two-component constructor was not `constexpr`, and
`nm` reported `Zero`, `One`, `UnitX`, and `UnitY` as type `B`. Relative initialization order across
translation units was therefore unspecified. The original LOW assessment correctly noted that
the all-zero bits happened to produce the intended value and that UBSan's vptr mechanism could not
see a non-polymorphic `Vector2`; it did not establish that the lifetime violation was safe.

`FINAL-STAB-001` enabled ASan's dedicated initialization-order detector without a suppression:
`ASAN_OPTIONS=check_initialization_order=1:strict_init_order=1`. It failed before `main()` with an
`initialization-order-fiasco`, an 8-byte read at `GestureDetector.cpp:52`, naming
`Vector2::Zero` and its definition in `Vector2.cpp:88`. The linked binary placed
`GestureDetector`'s translation-unit initializer before `Vector2`'s, so the previously source-only
finding became a reproducible final gate failure.

### Root fix

The two-component `Vector2(float, float)` constructor is now `constexpr` and defined inline where
constant evaluation can see it. All four value-type constants are defined as `constinit const`
objects. `constinit` is the permanent compile-time regression gate: a future change that makes any
initializer dynamic fails compilation instead of recreating an order dependency. Post-fix `nm`
reports all four constants as type `R`, each 8 bytes, and the Vector2 object contains no global
constructor for them.

The earlier suggested `GestureDetector`-local construction would have hidden only these five
reads while leaving the same vulnerable public-constant pattern intact. The implemented fix owns
the root cause at `Vector2` and preserves the public `Vector2::Zero` API.

### Same-pattern audit and evidence

A bounded relocation/call-closure scan covered all 255 CNA objects in the fresh sanitizer graph:
36 translation units had static-initialization seeds and 355 functions were reachable from them.
Only two cross-translation-unit data references remained: `GestureDetector` to
`Vector2::Zero`, now read-only/type `R`, and `BlendState` to the loader-provided `Color` vtable,
type `V`. No `B`/`D` XNA value-type constant remained reachable during another translation unit's
static initialization, so there is no independent ticket.

Under strict initialization order, leak detection, ASan, UBSan, and float-cast-overflow, the four
Vector2 constant/constructor tests pass 4/4 and the exact representative corpus passes
215 = 214 pass + 1 intentional HEADLESS no-pixel-route skip. The affected audio slice passes 8/8,
the dynamic/static/no-hardware harnesses each exit zero, and no sanitizer report remains.

---

## 19. `REMED-GFX-222` — `SetVertexBuffers` rejected FNA-legal null vertex-buffer bindings

**Status:** **CLOSED — DISCOVERED AND RESOLVED in the Batch 2 stabilization, 2026-08-06** ·
**Severity:** MEDIUM · **Introduced:** 2026-07-25 by `8a308f3d` (`REMED-GFX-039`), before the
phase-1 checkpoint

**Defect.** `GraphicsDevice::SetVertexBuffers` threw `System::ArgumentNullException` for any
binding whose vertex buffer is null. FNA performs no per-element null check
(`FNA/src/Graphics/GraphicsDevice.cs:1143`): a null *array* unbinds every slot, more than 16
bindings throws `ArgumentOutOfRangeException`, and a null-buffer *element* is a legal unused
slot — FNA itself assigns `VertexBufferBinding.None` into the same array, and CNA's own draw
dispatch already skips a defaulted binding as unused. GFX-039's master-plan strategy is "Add each
validation per FNA"; every other validation in its progress record has an FNA counterpart — this
one contradicts FNA and documented no deviation. One unit test added by the same commit
(`SetVertexBuffers_DefaultNullBindingThrows`) encoded the same over-reach.

**How it surfaced.** The Batch 2 stabilization's widened EasyGL principal control counted
`EasyGL_DeviceValidation` (Task 202, 2026-06-26) for the first time; its check 2 binds 16
default-constructed bindings asserting XNA's count-boundary contract and failed 3/3
deterministically — including on the binary built at Batch-1-checkpoint content, so the defect
predates both Batch 2 lanes and is not an integration regression.

**Fix** (integration branch, Batch 2 stabilization): the element null-throw loop removed from
`GraphicsDevice.cpp` (the FNA-faithful 16-binding ceiling, empty-vector unbind and null-safe
current-buffer assignment stay; the now-unused `ArgumentNullException` include dropped); the
header's `@throws` contract corrected to the FNA semantics; the unit test replaced by
`SetVertexBuffers_DefaultNullBindingIsAccepted` (acceptance + storage + null slot retrievable)
plus a new `SetVertexBuffers_SeventeenBindingsThrow` ceiling test. The `VertexBufferBinding`
parameterized-constructor validation (also GFX-039, XNA-documented) is deliberately untouched.

**Evidence.** Pre-fix: `EasyGL_DeviceValidation` 3/3 failed, "SetVertexBuffers(16) does not
throw". Post-fix: 3/3 pass; `GraphicsDeviceValidationTest` + `TextureCollectionValidationTest`
20/20 (the 17-binding mutation control throws); full post-fix EasyGL principal corpus and the
Wicked/Magnum focused controls recorded in `integration/BATCH_2_STABILIZATION.md` §6/§11.

---

## 20. `REMED-GFX-223` — a cache-reconstructed texture claimed to be a render target

**Status:** **CLOSED — DISCOVERED in the `skia` adaptation 2026-08-07, RESOLVED the same day** ·
**Severity:** HIGH · **Latent at the head; made live by the `skia` lane**

**Defect.** `Texture2D::gpuOnlyContent_` carries two different claims:

| | Claim | Consequences |
|---|---|---|
| **B** (weak) | an absent CPU shadow is normal here — fall back to the backend rather than throwing | `GetData` reads the backend *only when there is no shadow* |
| **A** (strong) | the live backend is the sole authority; the shadow is never trusted | `GetData` prefers the backend always; `SetData` drops the shadow and updates the existing backend **in place** instead of replacing it |

At the phase-1 head the flag only ever meant **B** — both of its readers
(`Texture2D.cpp:434` and `:509` at `aa9f3fb5`) sit *inside* an
`if (!cpuPixels_ || cpuPixels_->empty())` branch. `Texture2D::ReconstructFromCache` builds through
the protected constructor `RenderTarget2D` owns, inheriting `gpuOnlyContent_ = true` for an ordinary
content texture whose very next statement installs a CPU shadow. It only ever needed **B**, so the
mislabel was inert.

The `skia` lane promoted the same flag to **A**, correctly, for real render targets — at which point
every one of A's consequences became a false statement about a cache hit:

1. `SetData(const Color*, int)` began writing through the backend. An ordinary texture's backend is
   **shared**: `ContentManager`'s weak texture cache hands the same `ITextureBackend` to every
   wrapper reconstructed from a hit, so the upload was published into every other holder — the
   CNB-33 aliasing `CnjCacheIsolationTests` exists to pin, whose own header comment predicted it
   ("if a future change to `SetData` ever starts mutating in place instead of reassigning, these
   tests will catch it"). It also kept the cache entry's `weak_ptr` alive, turning what the head
   resolves as a *miss* (reload from disk) into a *hit* on mutated pixels.
2. `GetData(Color*, int, int)` moved its delegation above the shadow check, so a plain texture's
   read went to a backend that cannot serve it.

**First incorrect state transition.** `gpuOnlyContent_ = true` at `Texture2D.cpp:356`, executed on
behalf of `ReconstructFromCache` (`:2703`), producing the contradictory state
`gpuOnlyContent_ == true && cpuPixels_ != nullptr`. Everything else is downstream of that one store.

**How it surfaced.** The `skia` lane had never been run under the EasyGL principal control — its own
validation has always been the Skia suite. Running it failed
`CnjCacheIsolationTest.SidecarLoadedFirstDoesNotCorruptLaterNativeLoad` and
`.NativeLoadedFirstWithLiveHandleUnaffectedBySidecar`, in isolation and in the corpus. Traced with
`CNA_TEXTURE_TRANSFER_TRACE=1`: the head reads `source=cpuPixels_` and passes; the lane reads
`source=backend` and raises *"this graphics backend cannot read a render target's colour attachment
back to the CPU"* — for a texture that is not a render target.

**Fix** (`adapt/skia`, `9dbdd4cf`; 31 insertions / 17 deletions across two files):
`ReconstructFromCache` clears `gpuOnlyContent_`, and the in-place backend update in
`SetData(const Color*, int)` is gated on `gpuOnlyContent_` — that branch exists solely so a render
target does not have its backend swapped for an ordinary texture backend, leaving `RenderTarget2D`'s
cached `IRenderTargetBackend` view dangling, and that reason applies only to render targets. Every
ordinary texture returns to the head's behaviour, where a full-level upload builds a fresh backend
and thereby detaches from anything sharing the old one. The member's documentation now states
contract **A** and records that sharing the constructor is not the same as being a render target.

Relative to the head: ordinary textures are behaviourally identical; render targets keep the lane's
improvement; a cache hit whose shadow has legitimately expired now raises the ordinary-`Texture2D`
refusal instead of attempting a backend readback, matching the contract `ContextRecoveryTest`
already documents for every other plain texture. The in-place upload is deliberately **not**
extended to ordinary textures — making it safe there needs the weak texture cache invalidated or
made copy-on-write, which is a design change rather than a bug fix.

**Evidence.** Pre-fix: 2/2 `CnjCacheIsolationTest` failures under `EASYGL`, reproduced on demand.
Post-fix: EasyGL principal control **5911/5912** (the sole failure is `easy-gl-resource-smoke-tests`,
a sibling-repository binary containing zero CNA symbols); thirteen new cache/lifetime regression
tests **13/13**; Skia focused suite **172/172**; `Skia_Texture_RowStride` **8/8**; `CnaTests` under
`SKIA` **124 failures against the fork point's 125** — 8 fixed, 7 new, the 7 being exactly the
`OrdinaryDrawMultiStreamTest`/`InstancedDrawMultiStreamTest` rows already classified as the shared
2D-only class. The transfer trace shows the cache-hit path **lost** two failed backend readbacks and
gained none. Full record in `integration/lanes/skia.md` and `plan_skia.md`.

---

## 21. `REMED-GFX-224` — an EasyGL render target silently discards `SetData`

**Status:** OPEN · **Severity:** MEDIUM · **Discovered:** 2026-08-07, while resolving
`REMED-GFX-223` · **Pre-existing; not introduced by any integration lane**

**Defect.** `ITextureBackend::UpdatePixels` is declared with an **empty default body**, and
`EasyGLRenderTargetBackend` does not override it. Uploading to an EasyGL `RenderTarget2D` therefore
writes nothing to the GPU resource, and a subsequent `GetData` returns the target's cleared surface.

**Why it was invisible.** At the head, `SetData(const Color*, int)` replaced the render target's
backend with an ordinary texture backend and left a CPU shadow that `GetData` consulted first — so
the read returned the last *upload* rather than the target's real content. That is its own defect,
and the one the `skia` lane's in-place branch was written to remove. With `REMED-GFX-223` fixed the
render-target path is honest, and the backend gap became visible.

**Not a regression.** No test, example or documented contract depends on the round trip, and the
partial-rectangle `SetData` overload has always gone straight to the same no-op `UpdatePixels`, so
this is a pre-existing gap now correctly attributed.
`Texture2DCacheReconstructionTest.RenderTargetReadbackComesFromTheSurfaceNotAnUploadShadow`
deliberately does not pin the round trip, and says so in the source.

**To fix.** Implement `UpdatePixels`/`UpdatePixelsLevel` on `EasyGLRenderTargetBackend`, then audit
every other backend's render-target backend for the same missing override. Deliberately out of scope
for `REMED-GFX-223`, which is a shared-layer state-ownership defect in a different subsystem.

---

## 22. `REMED-GFX-225` — the `skia` lane's new `ITextureCubeBackend::GetSizeEXT` broke four backends

**Status:** **CLOSED — DISCOVERED AND RESOLVED in the `skia` stabilization, 2026-08-07** ·
**Severity:** HIGH · **Introduced by the `skia` lane** (`SKIA-149` area), never built before the
cross-backend control ran

**Defect.** The lane added `[[nodiscard]] virtual int GetSizeEXT() const noexcept { return 0; }` to
`ITextureCubeBackend` in the shared `IGraphicsBackend.hpp`, because `SkiaEffectBackend` needs the
cube edge length through the interface. At the phase-1 head that interface had **no** `GetSizeEXT`
at all — but four concrete cube backends already carried a same-named non-virtual accessor declared
**without** `noexcept`:

| Backend | Declaration site |
|---|---|
| `SokolTextureCubeBackend` | `Sokol/SokolGraphicsBackend.hpp:524` |
| `D3D11TextureCubeBackend` | `D3D11/D3D11Textures.hpp:76` |
| `D3D12TextureCubeBackend` | `D3D12/D3D12TextureCube.hpp:63` |
| `D3D9TextureCubeBackend` | `D3D9/D3D9Textures.hpp:79` |

All four derive from `ITextureCubeBackend`, so adding the base virtual silently turned each accessor
into an override with a **looser exception specification** — a hard compile error, not a warning:

```
error: looser exception specification on overriding virtual function
       'virtual int CNA::Internal::Backends::Sokol::SokolTextureCubeBackend::GetSizeEXT() const'
note:  overridden function is 'virtual int ITextureCubeBackend::GetSizeEXT() const noexcept'
```

**`CNA_GRAPHICS_BACKEND=SOKOL` did not build at all** from the adapted sources. `D3D11`, `D3D12` and
`D3D9` carry the identical break on Windows.

**How it surfaced.** The `skia` lane's Sokol cross-backend control — built from the adapted sources
per the precedent `integration/lanes/diligent.md` set — failed at 47%, in
`cna_backend_graphics_sokol`. No Skia test, and no EasyGL control, can reach it: EasyGL's cube
backend never had a `GetSizeEXT`, so the collision does not exist there. This is the second defect
in this lane that only a *different backend's* build could find, after `REMED-GFX-223`.

**Fix** (`adapt/skia`): `noexcept override` added to all four declarations. `override` is the part
that matters going forward — it makes the now-virtual relationship explicit and turns any future
signature drift back into an error at the derived class rather than a silent re-binding. Each body
is `return size_;` and cannot throw, so conforming to the base's `noexcept` is free.

The lane's five other new shared-interface virtuals — `GetDimensionsEXT`, `HasDefinedMipLevel`,
`DrawMeshEXT`, `CreateRenderTarget2DEXT`, `Ensure3DSupported` — were checked for the same hazard by
grepping every backend header for same-named members. **None collides**; only `GetSizeEXT` did.

**Evidence.** Pre-fix: `SOKOL` build fails at 47%, deterministic. Post-fix: `SOKOL` builds clean and
its dedicated suite and the shared cache controls are recorded in `integration/lanes/skia.md`.
**`D3D11`/`D3D12`/`D3D9` are corrected by inspection but not compiled** — none of the three builds on
this Linux host, and that limitation is stated rather than papered over.

---

## 23. `REMED-GFX-226` — Glide DualTexture ignored sampler slot 1

**Status:** **CLOSED — DISCOVERED AND RESOLVED in the Glide adaptation, 2026-08-08** ·
**Severity:** MEDIUM · **Historical lane defect**

**Defect.** The lane stored one filter/address/LOD tuple and submitted it to both TMU0 and TMU1.
`DualTextureEffect` therefore silently ignored `SamplerStates[1]`: a supported two-texture draw
could filter or select mip LOD with slot 0's state even when the caller supplied a distinct slot 1.

**Fix.** Glide retains slots 0 and 1 independently. Each TMU receives its own filter and LOD bias.
The implementation has only one native s/t vertex channel and performs wrap/mirror segmentation on
the CPU, so differing address modes cannot be represented; that combination now rejects before
native submission instead of silently choosing slot 0. Higher unused public slots remain inert.

**Evidence boundary.** Portable tests cover both equal/mismatched address decisions and the exact
representable MaxMipLevel/LOD-bias range; i686 whole-backend syntax and ASan/UBSan pass. No real
Glide runtime was present, so native call/value and image validation remain unavailable rather than
being inferred from the compatibility API.

---

## 24. `REMED-GFX-227` — Glide could release deferred SpriteBatch resources before submission

**Status:** **CLOSED — DISCOVERED AND RESOLVED in the Glide adaptation, 2026-08-08** ·
**Severity:** MEDIUM · **Historical lane defect**

**Defect.** The historical texture destructor called `grFinish`, but that fences only commands
already submitted to Glide. A final SpriteBatch can still exist solely in CNA-owned deferred vertex
storage. Destroying its texture could return the TMU range for reuse before those vertices were
submitted; destroying the device could close the context and drop them entirely.

**Fix.** Texture destruction flushes the CNA-owned SpriteBatch before unregistering, fencing, and
releasing TMU ranges. Backend destruction flushes and fences before `grSstWinClose`, preserving
command order and teardown lifetime. The weak backend reference still makes backend-first teardown
safe.

**Evidence boundary.** Ordering and ownership were audited over every queue-flushing entry point;
portable helpers pass under ASan/UBSan and the whole backend passes i686 syntax. Runtime teardown
capture remains unavailable without an external `glide3x.dll` and a linkable i686 CNA executable.

---

## 25. `REMED-GFX-228` — Glide TMU1 preparation could evict the active TMU0 texture

**Status:** **CLOSED — DISCOVERED AND RESOLVED in the Glide adaptation, 2026-08-08** ·
**Severity:** MEDIUM · **Historical lane defect**

**Defect.** `EnsureTmu1Resident(texture1)` first prepared texture 1 through its ordinary TMU0
residency path. Under TMU0 pressure, that allocation could evict texture 0 after texture 0 had
already been validated for the draw. The draw could then continue with an empty/stale first
texture while its independent TMU1 allocation remained valid.

**Fix.** After TMU1 upload, texture 0 is restored as the final TMU0 requester and its required
single-tile shape is revalidated before any native draw state is committed. Evicting texture 1's
temporary TMU0 copy does not affect its independently allocated TMU1 range.

**Evidence boundary.** The allocator/LRU pure tests and ASan/UBSan suite are green, and the entire
backend passes i686 syntax. The exact memory-pressure native sequence is build-covered only because
no production Glide runtime was available.

---

> **Classification note for §§26–32.** The synchronized GDI adaptation commits, plan, and backend
> documentation assign no separate severity or scheduling priority to these already-closed findings.
> None is invented here; each entry records only its exact ID, resolved mechanism, and evidence.

## 26. `REMED-GFX-229` — Software texture uploads accepted an undersized positive pitch

**Status:** **CLOSED — DISCOVERED AND RESOLVED in the GDI adaptation, 2026-08-08** ·
**Shared Software-2D supported-path defect**

**Defect.** `SoftwareTextureBackend` accepted a positive upload stride below `width * 4`, then copied
one complete RGBA8 row. The row contract was therefore internally contradictory and could consume
bytes beyond the caller-declared row or overlap the following row. This was distinct from the
established non-positive-stride convention, which deliberately requests a tight row.

**Fix.** Validate `stride >= width * 4` before resizing or mutating texture storage when the stride
is positive. Preserve the tight-row default for non-positive values and consume only the RGBA bytes
from each valid padded row.

**Evidence.** The current GDI/Software texture-allocation executable passes odd-width padded rows
with asymmetric RGBA channels, exact readback, stride-11 rejection for a 12-byte row, and retention
of the prior pixels after rejection. The current x64 MinGW/Wine GDI matrix passes 19/19 and the
native Texture2D control passes 40/40.

---

## 27. `REMED-GFX-230` — Software render-target uploads ignored row pitch

**Status:** **CLOSED — DISCOVERED AND RESOLVED in the GDI adaptation, 2026-08-08** ·
**Shared Software-2D supported-path defect**

**Defect.** `SoftwareRenderTargetBackend::UpdatePixels` treated every source as tightly packed even
when the caller supplied a valid padded positive pitch. Rows after the first therefore began at the
wrong byte, while an undersized positive pitch was not rejected transactionally.

**Fix.** Copy each row's `width * 4` RGBA bytes from the supplied pitch, retain the non-positive tight
default, and reject a short positive pitch before changing colour, multisample, mip, or retained
render-target state.

**Evidence.** Current 3×2 odd-width/asymmetric/padded exact-readback coverage passes, as does rejection
with prior-pixel retention. The native render-target readback control passes 102/102.

---

## 28. `REMED-GFX-231` — CPU `SourceAlphaSaturation` used source alpha twice

**Status:** **CLOSED — DISCOVERED AND RESOLVED in the GDI adaptation, 2026-08-08** ·
**Shared Software-2D supported-path defect**

**Defect.** The RGB `SourceAlphaSaturation` factor used inverse source alpha instead of inverse
destination alpha. Supported draws with distinct nontrivial source/destination alpha silently
produced the wrong colour. The alpha factor must remain one.

**Fix.** Compute the RGB factor as `min(sourceAlpha, 1-destinationAlpha)` and the alpha factor as one.

**Evidence.** Asymmetric source channels and distinct source/destination-alpha coverage pass in the
current GDI 2D regression. Native Software blend controls pass, including Additive 29/29.

---

## 29. `REMED-GFX-232` — DX3's standalone stencil hook contradicted its depth-only surface

**Status:** **CLOSED — DISCOVERED AND RESOLVED in the GDI adaptation, 2026-08-08** ·
**Pre-existing capability-reporting defect**

**Defect.** DX3 implements a real depth plane but no stencil plane. Its standalone stencil hook
nevertheless inherited an aggregate answer inconsistent with both its production surface and
`GraphicsCapability::StencilBuffer`, after the current integration architecture split stencil from
the aggregate depth/stencil question.

**Fix.** Override `SupportsStencilBuffer()` to return false while retaining DX3's real depth support.

**Evidence.** The x64 MinGW DX3 build and focused capability runtime pass 1/1 through Wine/Xvfb with
the DirectDraw-engagement wrapper active; the regression compares the hook and capability answer.

---

## 30. `REMED-GFX-233` — legacy empty-declaration persistent buffers reused vertex zero

**Status:** **CLOSED — EXPOSED AND RESOLVED in the GDI adaptation, 2026-08-08** ·
**Pre-existing at integration base `677f4c59`; not introduced by the GDI replay**

**Defect.** `VertexBuffer(device, count)` intentionally has an empty, zero-stride public declaration,
while typed `SetData` gives its backend buffer a real packed stride. The immutable one-stream
snapshot introduced by REMED-GFX-201 copied the public zero stride, so Software fetched record zero
for every vertex and submitted a degenerate primitive.

**Fix.** Only the exact legacy ordinary single-buffer/empty-declaration shape uses the named backend
buffer's stride fallback. Any nonzero declaration and every multistream or instanced route remains
on the authoritative current stream description; no removed `GpuDrawParams` field or blanket
fallback was restored.

**Evidence.** The Additive contract pins the empty-declaration precondition and exact indexed and
non-indexed pixels. Current native Software effects 7/7, Additive 29/29, and scissor 44/44 pass.

---

## 31. `REMED-GFX-234` — a stride-32 declaration lost the colour it declared

**Status:** **CLOSED — FOUND AND RESOLVED while triaging the permanently-red suite, 2026-08-29** ·
**Pre-existing silent wrong result**

**Defect.** `EasyGLRenderer::SelectStockProgramShape`'s `case 32:` routed every stride-32 draw to
the lit family unconditionally, because stride 32 is `VertexPositionNormalTexture`'s. A
Position+Colour vertex padded to 32 reaches it too. The lit programs take `{aPos, aNormal, aUV}` and
carry no colour input — `kLitColor` is chosen only at `stride == 36` — so the declared `Color`
element had no attribute to bind to and was silently dropped. The draw rendered correct geometry in
black.

**Why it hid.** The renderer is half-migrated and the two halves disagree. `REMED-GFX-218` landed:
`ConfigureDeclarationForStockProgramEXT` binds every stock attribute at the declared element's own
`getOffsetProperty()`, so element ORDER is honoured. `REMED-GFX-217` did not: which stock program a
draw gets is still decided by byte stride, with two hand-written declaration special cases (strides
24 and 36). Where a stride is ambiguous, that pair drops whatever the chosen program has no input
for — silently, because the guard's rule is asymmetric by design and a native attribute the
declaration does not name is not a violation.

It also hid behind a stale test arm. `VertexDeclarationLayoutTest` demanded that
`REMED-GFX-DECL-GUARD` REFUSE three colliding declarations on every renderer but bgfx. EasyGL had
stopped refusing them, so the arm failed on all three with `was ACCEPTED` — the same message for the
two it now renders correctly and for the one it rendered wrong. The measurement that separates them
is in the arm's own text: `colorPosition16` and `positionTextureColor24` render **byte-identically**
to their non-colliding twins, which is what correct binding produces because the fixture writes each
case's bytes at its own declared offsets; `positionColorPadded32` rendered black.

**Fix.** `case 32:` asks the declaration: one that names no `Normal` cannot be a lit vertex whatever
its stride, so it takes the coloured program. An absent declaration keeps the stride's answer, which
is the only thing there is to go on. This is a narrowing, not a translator: `REMED-GFX-217` stays
open.

`TranslatesDeclarations()` replaces four hand-written `CNA_RENDERER_IS(Bgfx)` skips and the arm's
own `!CNA_RENDERER_IS(Bgfx)`, so the refusal arms and their translating control can never disagree
about which renderer is which. EasyGL joins bgfx there; all five GL profiles share the one
implementation, and the reading was taken on OPENGLES3.

**Evidence.** Before: `positionColorPadded32` four columns `(0,0,0,255)`, geometry correct
(`lit=5760/4320/2880/1440`). After: `(204,45,35)`, `(51,89,70)`, `(102,22,140)`, `(204,89,140)` —
the same four its stride-16 twin renders. `VertexDeclarationLayoutTest` + `DeclarationGuardTest`:
**10 failing → 8 passing, 4 skipped, 0 failing.**

Mutation-checked, and this is the part worth keeping: with the fix reverted the suite no longer says
`was ACCEPTED`, it says *"positionColorPadded32 column 0: carried (0,0,0,255), expected
(204,45,35,255) -- the COLOUR attribute is being read from the wrong byte offset or format"*. The
test change is what turns a stale refusal expectation into an assertion that names the defect.

---

## 32. `REMED-GFX-235` — the XNA pixel-centre correction removed multisample coverage

**Status:** **CLOSED — owner chose this of two measured options, 2026-08-29** ·
**Pre-existing since `SAMPLE-001`; a side effect of a deliberate correction, not a defect in it**

**Defect.** `EasyGLRenderer::BindDrawParams` post-multiplies WVP by `xnaPixelCenter`, a clip-space
translation of `xnaPixelCenterScale_ = 63/64` over the viewport extent — **63/128 ≈ 0.492 window
pixels** — reproducing XNA 4.0's Direct3D 9 pixel-CENTRE addressing on a GL that addresses pixel
CORNERS. `SAMPLE-001` added it so the Primitives sample's 1×1 right triangles cover a pixel, and the
value is deliberately just under half a pixel so the centre stays on the covered side.

That margin is exactly what multisampling defeats. At four samples the outer sample positions sit at
a quarter of a pixel, inside the margin, so the same translation stops being a fill-rule decision
and starts removing coverage.

**Measured, in this order, and each step narrowed the next.** The 15 wrong texels of an 8×8 are row 0
(8) plus column 0 (7). `texel(0,0)` is **exactly ¼** of the expected colour and the other 14 exactly
½ — one and two of four samples. A quad drawn to NDC ±1.125, half a pixel outward, takes every
failure to zero, while ±1.0625 does not, so the shift is between a quarter and a half pixel. A quad
covering only NDC `x ∈ [-1,0]` leaves the **interior** seam at window x=4 half-covered too, so it is
a uniform geometry shift and not an edge artefact. Setting `xnaPixelCenterScale_ = 0` takes both
fixtures to zero. And the same failures reproduce byte-identically on real AMD hardware
(`DISPLAY=:0`, Radeon 780M/radeonsi), so no part of it is a llvmpipe artefact.

**Fix.** The correction is skipped while the destination is multisampled — asked of the bound
`rt2D`, `cube` and every live MRT slot, so no path is missed. It keeps doing the job it was tuned
for and stops doing one it was not.

**The cost is real and was accepted deliberately.** Geometry now differs by ~0.49px between a
multisampled destination and a single-sampled one, so a game toggling MSAA sees a sub-pixel shift.

**The alternative, and why it lost.** The four failing fixtures could have been taught to tolerate
partial coverage instead. But `rendertarget_msaa_first_readback_test.cpp` and
`rendertarget_msaa_mip_readback_test.cpp` are registered for **seven renderers** — bgfx, easygl,
headless, sdl-gpu, software, vulkan, webgpu — and only EasyGL applies this correction. Weakening
them would blind a cross-renderer contract on the other six, where full coverage is achievable and a
genuine edge-coverage defect would then pass unnoticed. That option was not measured on those six;
this configuration builds EasyGL only.

**Evidence.** `EasyGL_GFX164_BoundMsaaAlpha`, `EasyGL_MsaaFirstReadback`, `EasyGL_MsaaMipReadback`
and `EasyGL_InvalidMipLevel` all go to zero failures. `EasyGL_XnaPixelCenter` — `SAMPLE-001`'s own
guard, and the reason the correction exists — stays green, because it is not multisampled.

---

## 33. `REMED-GFX-236` — EasyGL ignored `GraphicsDevice.ReferenceStencil`

**Status:** **CLOSED, 2026-08-29** · **Pre-existing gap: the one renderer of 27 that never
implemented the hook**

**Defect.** `GraphicsDevice.ReferenceStencil` is a standalone device property in XNA/FNA
(`FNA3D_Get/SetReferenceStencil`), like `BlendFactor`: changing it must affect the next draw's
stencil compare **without** reassigning the whole `DepthStencilState`. Task 870/319 added
`IGraphicsRenderer::SetReferenceStencil(int)` and `GraphicsDevice::setReferenceStencilProperty`
forwards to it. **26 renderers implement it; EasyGL did not**, so it inherited the interface's
defaulted no-op and every override was silently discarded — the compare kept using whatever the
assigned state had baked in.

**Why the interface default hides it.** `SetReferenceStencil` is declared `virtual void
SetReferenceStencil(int) {}`, so a renderer that never implements it still builds and still runs.
The only thing that reports the gap is a pixel test, and this one had been reporting it.

**Fix.** GL has no call that sets the reference alone — `glStencilFunc` binds function, reference
and mask together — so `ApplyDepthStencilState` now records the function, the CCW function, the read
mask and the two-sided flag, and `SetReferenceStencil` reissues the function call(s) with the new
value. Two-sided state reissues **both faces**, because GL binds the reference per face. Recorded
even while the stencil test is off, since a later state may re-enable it. Nothing is reissued while
it is off; that state carries its own reference when it arrives.

**The test was reporting the truth and telling readers to ignore it.** Its header carried a note
saying `setReferenceStencilProperty` was a pure local no-op, that `IGraphicsRenderer` had no such
method at all, and that the test should be *expected* to fail on every renderer. All three had
stopped being true. The note is replaced rather than amended: an "expect this to fail" that outlives
its reason turns a real signal into background noise.

**Coverage, and a leg that initially defended nothing.** A two-sided leg was added for the new
per-face path. Its first version passed **with the back-face reissue deleted** — one quad reaches
one GL face, and this fixture's quad rasterizes as the FRONT face despite the file's own comment
about it being back-facing (that comment is about culling, not about GL's stencil faces). Measured
by removing each face's reissue in turn and watching which one the test noticed. The leg now draws
both windings, and removing either face fails it.

**Evidence.** `EasyGL_GraphicsDevice_ReferenceStencil` 1 FAIL → 2 PASS. Mutation-checked three
ways: storing the value without reissuing fails leg A; dropping the front face fails leg B;
dropping the back face fails leg B.

---

## 34. `REMED-GFX-237` — a clear opened the depth and stencil write masks and left them open

**Status:** **CLOSED, 2026-08-29** · **Pre-existing; the restore half of a correct override was
never written**

**Defect.** XNA's `Clear` ignores `DepthBufferWriteEnable` and `StencilWriteMask`; `glClear` obeys
both. EasyGL therefore forces `glDepthMask(true)` and `glStencilMask(0xFFFFFFFF)` around every
clear, which is right. It never put them back.

**The assumption that made that look safe is written in the source.** `ClearStencil`'s own comment
says *"ApplyDepthStencilState() reissues the real write mask before the next draw anyway"* — true
only if the game reassigns its `DepthStencilState` between the clear and that draw, and nothing
requires it to. `Clear` immediately followed by a draw through the state that was already active
drew with the wrong masks: depth writes that the state disabled, stencil bits that its write mask
forbade.

Note the same file already got this right for the COLOUR write mask (`REMED-GFX-077`): force,
clear, `ApplyCurrentColorWriteMasks()`. The depth and stencil halves of the same idea were missing
the third step.

**Fix.** `ApplyDepthStencilState` records `depthWriteEnable` and `stencilWriteMask` (joining the
function/mask state `REMED-GFX-236` already caches), `SetDepthWriteEnabled` and
`SetDepthTestEnabled` keep the depth value in step, and every clear path that forces a mask calls
`RestoreWriteMasksAfterClear`. All five do: `ClearDepth`, `ClearStencil`, `ClearColorAndDepth`,
`ClearDepthAndStencil`, `ClearColorDepthAndStencil`. The stencil mask is restored only while the
stencil test is on, matching `ApplyDepthStencilState`, which installs one only then; two-sided state
restores both faces.

**The stencil half was a latent hole nothing would have caught.** Only the depth half had a failing
test. Deleting the stencil restore left **all 65 stencil tests passing**, so it was measured before
being shipped and a leg was written for it: `EasyGL_GraphicsDevice_ClearStencil` Check D stamps
0x05, assigns a state with `StencilWriteMask=0x00`, clears the stencil to 0x05 — which only lands
because the clear forces the mask open — draws through that state, and compares. Restored, the
buffer still reads 0x05 and the compare draws green; not restored, the draw replaced it with its own
reference.

**Evidence.** `EasyGL_DepthStencilState_WriteEnable_Golden` FAIL → PASS;
`EasyGL_GraphicsDevice_ClearStencil` 3/3 → 4/4. Mutation-checked both halves: deleting the depth
restore fails the golden test, deleting the stencil restore fails Check D.

---

## 35. `REMED-BUILD-017` — the native GDI workflow omitted three correctness targets

**Status:** **CLOSED — DISCOVERED AND RESOLVED in the GDI adaptation, 2026-08-08** ·
**Build/evidence inventory defect, not a renderer defect**

**Defect.** The manual native-MSVC workflow and copied command inventory named only fourteen focused
correctness executables. They omitted the presentation-mode transaction, DC-release transaction,
and texture-allocation targets added by GDI-075/076/077, despite CMake's authoritative inventory
containing seventeen executables and nineteen registered cases.

**Fix and evidence.** The workflow and GDI documentation now name all seventeen correctness targets
before running the nineteen-case label. The current x64 MinGW build produced all seventeen and all
19/19 cases passed through Wine 10/Xvfb. Native MSVC remains an external manual gate, not an open
inventory defect.

---

## 36. `REMED-BUILD-018` — the capability test used an incomplete backend type

**Status:** **CLOSED — DISCOVERED AND RESOLVED in the GDI adaptation, 2026-08-08** ·
**Shared test-build defect, not a renderer defect**

**Defect.** `GraphicsDeviceCapabilityTests.cpp` called `SupportsStencilBuffer()` through
`GraphicsDevice::GetBackend()` without directly including the complete `IGraphicsBackend` type.
The focused native sanitizer object build exposed the incomplete-type compile failure.

**Fix and evidence.** The test now includes `IGraphicsBackend.hpp` directly. The current native
ASan/UBSan focused harness compiled with the sanitizers proven active and selected 151 tests:
149 passed, 2 intentionally skipped, and zero CNA sanitizer report was emitted. LeakSanitizer used
`detect_leaks=0` only because the supervising ptrace environment makes its leak mode unusable.

---

## 37. `REMED-GFX-238` — two contracts assert a pixel-centre convention XNA does not use

**Status:** **CLOSED — 2026-08-30** · **Test defect, not a renderer defect**

**Defect.** `EasyGLRenderer`'s `xnaPixelCenterScale_` (see `REMED-GFX-235`) reproduces XNA's
Direct3D 9 **integer** pixel-centre addressing. Two contracts assert the OpenGL/Direct3D 10
**half-integer** convention instead, so they mark XNA-correct output wrong and only EasyGL — the one
renderer that applies the correction — fails them:

- `point_sampling_contract_test.cpp` leg **U2** (3×3 point-sampled onto 10×10) expects destination
  pixel *x* to select `floor((x+0.5)/w · texw)`. Leg **U1** (8×4 onto 16×8) cannot see the question:
  at an integer magnification both conventions select the same texels.
- `descriptor_capacity_contract_test.cpp` legs **B1/C1** (2×2 onto 2×2) require every readback
  channel to be a clean 0 or 255 for all 27 sampler states.

**Measured on the real runtime.** `spikes/xna-pixel-center-spike/` compiles a C# probe against the
XNA 4.0 assemblies in `~/.wine-cna-xna40` and runs it. That prefix routes Direct3D 9 through
**DXVK, not wined3d**, so a D3D9-over-OpenGL translation cannot itself supply the half-pixel shift
under investigation.

    U1 8x4 -> 16x8:   HALF-INTEGER 128/128   INTEGER 128/128
    U2 3x3 -> 10x10:  HALF-INTEGER  81/100   INTEGER 100/100
            row0 selected i: 0000111222              = floor(x/10*3)

    LEG-C 2x2 -> 2x2 POINT : dirty=0/4   (255,0,0) (0,255,0) (0,0,255) (255,255,0)
    LEG-C 2x2 -> 2x2 LINEAR: dirty=3/4   (255,0,0) (128,128,0)* (128,0,128)* (128,128,64)*

XNA lands on integer centres **100/100**, and at 1:1 it blends three of four pixels at exactly
128 — the 50/50 weight integer centres predict, because a pixel centre at window x=1 falls on
texture coordinate 1.0, halfway between the texel centres at 0.5 and 1.5. The contract's "one texel
to one pixel" holds in XNA only for magnifying filters that do not interpolate.

**The failing counts identify the mechanism exactly.** Five of the nine filters `kFilters` sweeps
magnify with LINEAR (`Linear`, `Anisotropic`, `LinearMipPoint`, `MinPointMagLinearMipLinear`,
`MinPointMagLinearMipPoint`). B1 fails 5×3 = **15** of 27; C1 fails 256×5/9 = **142** of 256. No
mag-point state fails. U2's 19 wrong pixels of 100 are exactly the 19 on which the two conventions
disagree.

**Planned fix.** State both expectations in terms of integer pixel centres, and say in each fixture
**why**, so the OpenGL form is not restored later as an obvious correction. U2's expectation becomes
`floor(x/w · texw)`; B1/C1 keep the byte-exact identity requirement for mag-point states and expect
a blend from the mag-linear ones rather than treating it as a dropped draw. Do not weaken the legs
to tolerate either answer — that would blind them to the defect they were written for.

**Not to be confused with a renderer change.** EasyGL is already correct here. Nothing in
`EasyGLRenderer` was touched.

**XNA uses both conventions, chosen by path — which is why only the 3D legs moved.** Asked the same
magnification through `SpriteBatch`, the runtime answers **half-integer 100/100** and integer 81/100,
the exact mirror of its 3D answer: `SpriteEffect` applies its own `-0.5` offset. CNA reproduces both
already (no correction on the sprite path, `xnaPixelCenterScale_` on the 3D one), so legs A–I are
right as they stand and were left alone.

**Fix as landed.**
- `Exact3DLeg` samples at `x/rtW`, with the measurement and a "do not simplify this back" note in
  place, and the leg inventory marks U as the one entry on integer centres.
- The tie guard became a parameter. Under integer centres an *integer* magnification is
  structurally tied — `x/rtW · texw` is a whole number for every other `x` — so U1, V1 and V2 now
  **assert `ties > 0`** rather than zero. That is deliberate: it is the reversion guard. Flipping
  the formula back takes all three to `ties == 0` and fails them. U2, the non-integer leg, keeps
  the strict `ties == 0` and stays the discriminating one.
- `descriptor_capacity` legs B1/C1 judge the five magnifying-linear filters on coverage **and a
  visible blend** — a state collapsed onto a cached point descriptor would reproduce the identity
  exactly and pass a mere coverage check — and the other four on the byte-exact identity. Both legs
  assert the split (15 of 27, 142 of 256) so a filter added to `kFilters` cannot land in the wrong
  half unnoticed.

**Two arithmetic errors found by running it, not by reading it.** The rotation in C1 yields **143**
magnifying-linear draws, not the `256·5/9 ≈ 142` shortcut: `256 = 28·9 + 4`, and three of the
four-element tail are magnifying. And identity **0** encodes four identical texels, so interpolating
it is a no-op and it reads back clean; it is judged on the identity like a point draw, which brings
the count back to 142 and explains why the old leg reported 142 wrong rather than 143.

**Evidence.** EasyGL `146/146` and `29/29`, both with the correction on. Mutation-checked both
files: restoring `(x + 0.5)` fails U2 on 19 mismatches and all three tie guards; declaring every
filter magnification-point fails B1 and C1.

**The consequence REMED-GFX-239 predicted is now visible.** Rebuilt and run against Vulkan, the
same legs fail with the same counts EasyGL used to produce — U2 by 19, B1 by 15, C1 by 142. The
roles have swapped, which is the honest state: EasyGL matches XNA, Vulkan does not, and the suite
now says so instead of the reverse. The other five renderers registered for these contracts are not
buildable in this configuration and were not measured.

---

## 38. `REMED-GFX-239` — the XNA pixel-centre convention is guarded on one renderer only

**Status:** **CLOSED — 2026-08-30** · **Coverage defect**

**Defect.** `EasyGL_XnaPixelCenter` is registered for **EasyGL alone**. No other renderer is held to
XNA's pixel-centre convention, and the six others that pass `PointSamplingContract` are passing an
expectation XNA never held (`REMED-GFX-238`). Built against the Vulkan renderer and run,
`SAMPLE-001`'s own fixture reports:

    [FAIL] XNA 1x1 triangle: 0 covered pixel(s), expected at least 1
    [PASS] BasicEffect control triangle: 120 covered pixel(s), expected at least 32

The control triangle rules out a broken effect or readback path: Vulkan simply does not implement
the convention, and nothing in the suite says so. CNA therefore gives **different pixel coverage
depending on the renderer**, silently.

**Planned fix.** Promote the fixture to a renderer-neutral contract registered for every renderer
that can run it, the way `PointSamplingContract` already is. Renderers that do not implement the
convention then fail visibly and take a recorded capability boundary, rather than passing by not
being asked. Implementing the convention on the other renderers is **not** in this ticket's scope —
naming the divergence is.

**Sequencing.** `REMED-GFX-238` landed first, as required: promoting this fixture while the two
contracts still asserted the opposite convention would have put two registered cross-renderer
contracts in direct contradiction.

**Fix as landed.** The fixture moved from `modules/renderers/easygl/examples/` to
`modules/graphics/examples/xna_pixel_center_contract_test.cpp`, alongside the other renderer-neutral
contracts, and its header now cites the runtime measurement rather than asserting XNA's behaviour
from the sample. `EasyGL_XnaPixelCenter` keeps its name and its meaning; `Vulkan_XnaPixelCenter`
joins it.

**Evidence.**

    EasyGL  [PASS] XNA 1x1 triangle: 1 covered pixel(s)   [PASS] control: 136
    Vulkan  [FAIL] XNA 1x1 triangle: 0 covered pixel(s)   [PASS] control: 120

EasyGL's covered count and its control count both match what the real runtime produced (1 and 136).
Vulkan's control triangle is intact, so its failure is the convention and not a broken effect or
readback path — which is exactly what the control triangle is there to separate.

**`Vulkan_XnaPixelCenter` is expected to fail, and that is the deliverable.** It is not a regression:
the case was always false on Vulkan and the suite simply never asked. Implementing the convention on
Vulkan is separate work and is **not** in this ticket.

**Deliberately left undone.** `PointSamplingContract` is registered for seven renderers; this
contract is now registered for two. The remaining five — bgfx, headless, llgl, sdl-gpu, webgpu —
were **not** registered, because no configuration here builds them and registering five cases whose
outcome has not been observed would be speculation rather than coverage. Extending the registration
is a mechanical follow-up for whoever can build them, and each will either pass or record the same
divergence Vulkan just did.

---

## 39. `REMED-GFX-240` — the pixel-centre correction flattens the CNAEXT PCF kernel

**Status:** **CLOSED — 2026-08-30** · **Fragile test, NOT a shadow-layer defect —
the diagnosis this ticket opened with was wrong**

**Defect.** With `xnaPixelCenterScale_` at its shipped value,
`ShadowVisibilityTest.TheFilterRadiusChangesHowSoftTheEdgeIs` fails its **second** assertion —
`countPartials(2) > 0`, "a 5×5 kernel produced no soft edge at all" — with an actual of 0. The
radius-0 assertion passes. `ShadowVisibilityTest.TheCastersShadowIsVisibleOnTheGround` also passes,
so the shadow is present and only its softness is gone; this is not a vacuous pass on an absent
shadow.

**Attribution is direct, not inferred.** Setting `xnaPixelCenterScale_ = 0` and rebuilding **every**
dependent binary takes this test to green and takes `EasyGL_XnaPixelCenter` red; restoring it
reverses both. Rebuilding only the test executable and not the renderer that holds the constant
reports the opposite result — a stale binary keeps the old renderer linked in, and that trap cost a
wrong conclusion once already in this investigation.

| test | correction off | correction on |
|---|---|---|
| `EasyGL_XnaPixelCenter` | FAIL | pass |
| `EasyGL_PointSamplingContract` | pass | FAIL — `REMED-GFX-238` |
| `EasyGL_DescriptorCapacityContract` | pass | FAIL — `REMED-GFX-238` |
| `ShadowVisibilityTest.TheFilterRadiusChangesHowSoftTheEdgeIs` | pass | **FAIL — this ticket** |

**The opening diagnosis was wrong, and measuring it said so.** This ticket was filed as "a
half-pixel shift should not flatten a PCF kernel, so the shadow layer must be deriving its tap
offsets from a matrix that now carries the clip-space translation." The kernel is not flattened and
the offsets are fine.

Two hypotheses were tested and both refuted before the real cause appeared:

1. *The fixed sample point left the shadow, collapsing the `(shadowValue+2, litValue-2)` window.*
   Refuted: instrumented, the window is wide open — `lit(3,3)=255`, `shadow(centre)=38`.
2. *The kernel degenerates.* Refuted: the shader's 5×5 loop and both its uniforms are correct, and
   with the correction **off** the same frame does carry intermediate values.

**Actual cause — the penumbra is narrower than a pixel.** The case renders a `kFrame` = 64 frame
against a `ShadowQuality::Medium` = **1024** map, so the five taps span about `64/1024 × 5 ≈ 0.3` of
one frame pixel. Whether any pixel centre falls inside that band is sub-pixel luck. Instrumented,
the entire 64×64 frame carried **two** intermediate values with the correction off (`distinct=4`,
counting the lit and shadowed values) and **none** with it on (`distinct=2`). The soft edge was
always there; a ~0.49px shift simply stepped every sample over it.

So the assertion was sound and the dimensions were not: the fixture demanded that a sub-pixel
penumbra be sampled.

**Fix.** Give this one case dimensions in which the penumbra is resolvable — a 256-pixel frame
against the smallest (512) map puts five taps across ≈2.5 pixels, which no sub-pixel shift can step
over. `Frame` carries its own `size` and `Capture` takes one, defaulting to `kFrame`, so the other
sixteen cases in the fixture are untouched.

**Evidence.** With the correction **on**: radius 0 gives `distinct=2` (a hard edge, as asserted) and
radius 2 gives `distinct=7`. All 17 `ShadowVisibilityTest` cases pass. Mutation-checked by forcing
`uShadowPcfRadius` to 0 in `EasyGLRenderer`, which fails the case on its own message — so the
widened fixture still defends what it was written for rather than passing because it now samples
more pixels.

**Nothing in the shadow layer or the renderer changed.** The correction is XNA-correct
(`REMED-GFX-238`) and stays as it is. See `spikes/xna-pixel-center-spike/README.md`, commits
`55b93f910` and `91be3f7a8`.

---

**Also corrected while measuring the above:**
`GltfConformanceL6.ViewAndProjectionReachEveryDrawUnaltered` had been counted among the correction's
casualties. It is not one. It fails only under `ctest -j` and passes serially every time.
