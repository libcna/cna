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
| **Silent wrong result** | A supported public call is accepted, submitted, and renders or reports the wrong thing with no exception and no diagnostic | `REMED-GFX-211`, `REMED-GFX-212`, `REMED-GFX-213` and `REMED-GFX-215` — all four measured at pixel level on 2026-08-03, and **all four DONE**. `REMED-GFX-215` showed the class's sharpest lesson: a white-`DiffuseColor` oracle cannot see a `DiffuseColor` defect, so a defective backend was certified a correct control |
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
> blocker and is now DONE (2026-08-03)**; it spawned `REMED-GFX-216`. `REMED-GFX-214` is still OPEN
> and uninvestigated.

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
| `REMED-GFX-211` | ~~Vulkan, Bgfx, WebGPU~~ **ALL THREE DONE — TICKET DONE** *(2026-08-03)* — instanced route | **Supported-path silent wrong result** — measured on all three, both stream sides | **None.** Classic 1+1 draw is accepted and submitted; instance records 0..3 consumed instead of 1..4, and the per-vertex stream renders its decoy | MEDIUM / P2 | **P1** | ~~REVIEW~~ **YES** *(triaged 2026-08-03)* | NO | GFX-202 (value already delivered); precedent GFX-122, GFX-123 | ~~Immediate remediation campaign~~ **COMPLETE — checkpoint blocker RESOLVED** | MEDIUM | Triaged: WebGPU pixel measurement taken; per-vertex side confirmed on all three; the two streams fail **independently** (`both-offsets-ignored`), so a fix must carry both. D3D9 unmeasured — no D3D display. **Vulkan corrected §4.1: instance offset into the copy's source base, per-vertex offset folded into `vkCmdDrawIndexed`'s own `vertexOffset` — the per-vertex loss point was `d.baseVertex`, not the neighbouring copy. Bgfx corrected §4.1 the same way at its own two expressions: the instance copy's source base became `cpuData.data() + vertexOffset * instStride`, and the per-vertex offset joined `baseVertex` in `bgfx::setVertexBuffer`'s `_startVertex` — bgfx has no draw-time base-vertex argument, so that start element IS the addend, which is also why the ordinary bgfx route was already correct (it folds into `params.baseVertex` upstream) and the instanced one was not (it folds nothing, by GFX-202 design). Vulkan and Bgfx now both in `CNA_INSTANCED_BINDING_OFFSET_ORACLE`; WebGPU corrected §4.1 at its own two expressions, and the per-vertex loss point was Vulkan's shape rather than bgfx's: `command.vertexData` copies the whole per-vertex buffer and the replay binds it at native offset 0, so `command.baseVertex = params.baseVertex` was the only term that could carry the offset and now carries `params.baseVertex + perVertexOffset`; the instance copy's source base became `vertexOffset * instVbStride`. All three backends are now in `CNA_INSTANCED_BINDING_OFFSET_ORACLE`, which emptied `CNA_INSTANCED_OFFSET_DEFECT_MEASURED` — the macro and its four arms were deleted, leaving the triage legs' `#else` UNCLASSIFIED arm for the still-unmeasured D3D9.** |
| `REMED-GFX-212` | **~~Vulkan, WebGPU~~ BOTH DONE — TICKET DONE** *(2026-08-03)*; D3D11, D3D12 remain *(source-identified, unmeasured)* — stock instanced shader | **Supported-path silent wrong result.** Reference settles it: case A | **None.** The draw renders silently in the wrong colour; no diagnostic | LOW / P3 | ~~REVIEW~~ **P1** | ~~REVIEW~~ ~~**YES**~~ **NO — RESOLVED 2026-08-03** | NO | ~~an FNA reference determination~~ — **answered** | **Immediate remediation campaign** | MEDIUM *(LARGE if shader sources change)* | Triaged: FNA's shader index has no instancing term, so the same VS runs on both routes. Vulkan/WebGPU colour their **own** ordinary route from the stream and the instanced one from `DiffuseColor`. D3D11/D3D12 still unmeasured — no D3D display |
| `REMED-GFX-213` | **~~Bgfx, Vulkan, WebGPU~~ ALL THREE DONE — TICKET DONE** *(2026-08-03)* — per-instance divisor *(scope widened by triage)* | **Supported-path silent wrong result.** Escalation condition confirmed | **None.** An adequately sized instance buffer clears every check and renders `InstanceFrequency = 2` at divisor 1 | MEDIUM / P2 | ~~P2~~ **P1** | ~~REVIEW~~ **YES** *(triaged 2026-08-03)* | NO | GFX-202; distinct from GFX-121; shares its copy site with GFX-211 | ~~Immediate remediation campaign~~ **COMPLETE — checkpoint blocker RESOLVED** | MEDIUM | Triaged: `frequency` occurs **0 times** in all three backends' sources. Not an absent native capability — all three can emulate a divisor in the buffer they already build. **Vulkan corrected §4.3 by replication: no divisor extension is enabled (API 1.1, `VK_KHR_swapchain` only), so the binding keeps divisor 1 and slot `i` takes source record `vertexOffset + i / frequency`. Destination cardinality unchanged, so no pipeline-key term was needed. Bgfx corrected §4.3 the same way, after classifying its native capability from the installed header rather than a grep count: bgfx's whole public instancing surface (`allocInstanceDataBuffer` / `setInstanceDataBuffer` / `setInstanceCount`, `InstanceDataBuffer{data,size,offset,num,stride,handle}`) has no divisor, step-rate or frequency parameter at all, so category C again — slot `i` takes source record `vertexOffset + i / frequency` in the instance-data buffer the route already allocates. Cardinality measured identical at frequency 1 and 2 (5 submissions / 16 prims / 1024 transient B for a 4-draw frame at both). Bgfx's over-long-range gate was generalised from `instanceCount * stride` to the highest source record at the same time. WebGPU corrected §4.3 the same way and classified its native capability from the installed wgpu-native v29.0.1.1 headers rather than a grep count: `WGPUVertexBufferLayout` is only `{nextInChain, stepMode, arrayStride, attributeCount, attributes}`, `WGPUVertexStepMode` is only `{Undefined, Vertex, Instance}`, none of the eleven `WGPUNativeSType` chains extends a vertex layout or vertex state, and no `WGPUNativeFeature` adds a step rate — category C again, so slot `i` takes source record `vertexOffset + i / frequency` in the command's own `instVbData`. Cardinality measured by a new permanent WebGPU regression (22/22) against the backend counters and wgpu-native's `wgpuGenerateReport()`: 1 pass, 1 submit, 0 new pipeline variants at every frequency and offset pair, and no native buffer added by doubling the instance count.** |

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
| **Before branch integration** | ~~`REMED-GFX-211`, `REMED-GFX-212`, `REMED-GFX-213` triage (§4)~~ — triage done 2026-08-03, and **all three tickets are now DONE on every backend they name**. `REMED-GFX-215`, spawned by `REMED-GFX-212`, was triaged against §2, confirmed a supported-path silent wrong result, treated as an immediate checkpoint blocker, and is **DONE (2026-08-03)**. **The checkpoint-blocker set is EMPTY.** The next action is post-audit exit reconciliation / checkpoint preparation, not another remediation ticket. `REMED-GFX-214` (MEDIUM, OPEN, uninvestigated) and `REMED-GFX-216` (MEDIUM, OPEN, spawned by `REMED-GFX-215`) are **not** checkpoint blockers: both fail loudly — a deterministic rejection and a visibly wrong geometry coverage — rather than silently returning wrong pixels on a supported path |
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
  **DONE (2026-08-03)**. It spawned `REMED-GFX-216` (bgfx derives its native vertex layout from the
  buffer stride rather than the `VertexDeclaration`). `REMED-GFX-214` and `REMED-GFX-216` are both
  OPEN, both MEDIUM, and neither is a checkpoint blocker: each fails loudly rather than silently
  returning wrong pixels on a supported path.
- **Deferred work stays visible** through this file, and through the `DEFERRED` status and
  `Target plan: plan_postaudit.md` pointer on every affected row in
  `remediation/REMEDIATION_INDEX.md` and `remediation/REMEDIATION_PROGRESS.md`.

**The remediation campaign is not declared complete by this document.** No checkpoint has been taken
and no tag has been created.
