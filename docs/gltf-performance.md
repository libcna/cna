# glTF import performance and lifetime (`plan_gltf.md` Phase 22)

What the import path costs, measured rather than estimated, and the decision each measurement led
to. Every row of `plan_gltf.md` Phase 22 accepts *"measured; fixed or documented with numbers"* —
this file is the numbers, and `GltfPerformanceTests.cpp` is where they come from.

## How to reproduce

```bash
./CnaTests --gtest_filter='GltfPerformance.*' --gtest_output=xml:perf.xml
grep -o 'name="[a-zA-Z_0-9]*" value="[0-9]*"' perf.xml | sort -u
```

Every measurement is a `RecordProperty`, so the numbers survive in the XML rather than in a log
nobody keeps. Durations are recorded in **microseconds** (the property names say `_x1000` because
they are thousandths of a millisecond).

The assertions in that file are deliberately **two orders of magnitude** above the measured values.
An assertion tuned to today's number is a flake generator on a loaded machine, and a benchmark that
fails at random gets deleted; what these guard is a change in the *shape* of the work — an
accidental O(n²), a re-read per primitive, a cache that stopped being hit.

## The numbers

Measured on the campaign's own machine (GCC 14, Debug + `-g`, `STUB` renderer, 2026-08-12) against
`mat-factor-only-gold` — a three-vertex asset, so these are **fixed costs**, not throughput.

| What | Measured | Task |
|---|---|---|
| One parse-and-load, fresh `ContentManager` | **95 µs** | `GLTF-433` |
| One `Load` of a name already loaded (cache hit) | **2 µs** — 47× cheaper | `GLTF-433`, `GLTF-437` |
| One load + `Unload` cycle, over 1 000 cycles | **89 µs** | `GLTF-436` |
| One occlusion remap (152-byte PNG → 222 bytes) | **73 µs** | `GLTF-443` |
| One morph weight change (3 vertices, 1 target) | **< 1 µs** | `GLTF-441` |
| Accessor bytes stored across the whole corpus | **13 537** | `GLTF-435` |
| Temporary `float` bytes those accessors expand to | **14 616** (1.08×) | `GLTF-435` |
| Worst single-accessor expansion | **4.0×** (byte → float) | `GLTF-435` |
| Vertex data held per morphed part | **2×** (uploaded buffer + `BaseVertexBytes`) | `GLTF-442` |

## The decisions

### `GLTF-433` — the premise was wrong, and the measurement is what said so

The row reads *"a GLB is fully re-parsed on every call."* It is not. `ContentManager` caches by
asset name — which is XNA's own documented contract for `Load<T>` — so **the parse happens once per
manager per asset name**, and a second `Load` of the same name is a 2 µs cache hit that returns the
*same instance*.

That has a consequence worth stating plainly, because it surprises people: two `Load<Model>` calls
with one manager return the same object graph, so mutating one model's effect mutates "both". A
caller who wants two independent copies uses two managers, or `Unload`s between them.
`LoadingOneNameTwiceReturnsTheSameInstanceAndAFreshManagerDoesNot` pins both directions.

**Decision: no cache added.** There is one, at the level XNA puts it.

### `GLTF-434` — the offline converter parses once

`gltf_to_cnj` calls `cgltf_parse_file` **once** and then walks the mesh groups from the same parsed
document, writing one `Model` `.cnj` per group. There is no per-group re-parse to measure.

**Decision: documented, nothing to fix.**

### `GLTF-435` — the float expansion is bounded at 4× and is per accessor

`UnpackAccessor` expands every accessor to `float` before the packing loop narrows it again. The
temporary is exactly `count × components × 4` bytes — arithmetic, not a timing, so the number is
identical on every machine.

Across the corpus that is 14 616 bytes of temporary against 13 537 stored: **1.08× overall**,
because most corpus data is already `float`. The worst *single* accessor expands **4×**, which is
the format's own worst case — an `UNSIGNED_BYTE` component widened to a `float`. A normalized
`SHORT` stream expands 2×; a `float` stream not at all.

The temporary is per accessor and freed immediately, so peak overhead is one accessor, not one
model. For a 200 k-vertex `POSITION` stream that is 2.4 MB.

**Decision: documented, not reduced.** Removing it means decoding each component directly into the
packed layout — a per-format switch inside the packing loop, replacing one vectorisable pass with
branchy per-component work, to save a transient that is 4× the *narrowest* possible input. The
measurement does not justify that, and `GLTF-041` exists to keep this decode path stable.

### `GLTF-436` / `GLTF-438` — 1 000 cycles, and where the leak assertion actually lives

`AThousandLoadUnloadCyclesLeakNothing` builds and destroys the whole object graph 1 000 times at
89 µs a cycle. Nothing in the test asserts a leak, and that is the point: **the assertion is the
sanitizer build**. Run this file under ASan with `detect_leaks=1` (see `docs/gltf-conformance.md`)
and a leak becomes a failure; run it in an ordinary build and it is a stress test that proves the
loop survives.

`UnloadThenLoadAgainYieldsAWorkingModel` covers the half a caller can check without a sanitizer:
after `Unload`, the manager is still usable and the model it returns is a real one — effect, vertex
buffer and a positive primitive count — rather than a husk pointing at freed buffers.

### `GLTF-437` — the sharing boundary is the manager, not the load

The row's wording ("correct within a load; nothing across loads") is off by one level. Within one
`ContentManager` a second load returns the cached model and decodes nothing. Across two managers,
the same asset is decoded twice — textures included.

**Decision: keep it.** Two managers is exactly how an application asks for two independent copies,
and a process-wide texture cache would make `Unload` on one manager free another's textures. The
cost of the second decode is the 95 µs above, plus the image decode for a textured asset.

### `GLTF-440` — a copied `Model` shares its resources and outlives the original

`Model::ownedResources_` is a `shared_ptr<void>`, so copying a `Model` shares the buffers rather
than duplicating them. `ACopiedModelSharesItsOwnedResourcesAndOutlivesTheOriginal` pins the property
a caller depends on without knowing it: the copy is still valid after the original is destroyed, and
still points at the same vertex buffer.

**Decision: documented and tested.** The alternative — deep-copying on assignment — would silently
double the memory of every model a caller passes by value.

### `GLTF-441` / `GLTF-442` — the morph duplication is 2×, exactly

Morphing is CPU-side: `SetMorphWeightsEXT` re-blends the whole vertex array from `BaseVertexBytes`
and re-uploads it. So a morphed part holds its vertex data **twice** — the uploaded buffer and the
rest pose it blends from. That is exact rather than sampled, and it is the number that decides
whether a GPU morph path is worth building: **2× on a 200 k-vertex mesh at stride 48 is 19 MB**.

The re-blend itself is linear in vertex count and immeasurably fast on the corpus's three-vertex
fixture (< 1 µs), which is precisely why the memory figure is the one that matters here.

**Decision: scoped as ROBUST, not fixed now.** A GPU morph path needs a shader variant on every
PBR-capable renderer, and this campaign's rule is that a renderer change nobody can verify is not a
fix (`GLTF-157`'s lesson). The duplication is documented so a caller sizing a scene can predict it.

### `GLTF-443` — the occlusion remap is three codec passes

The dual-texture lightmap approximation decodes the occlusion PNG, halves one channel, and
re-encodes it as PNG — which the texture loader then decodes a *second* time. Three codec passes for
an arithmetic operation on one channel, at **73 µs** for a 1×1 image, i.e. essentially all fixed
cost.

**Decision: not short-circuited, and the reason is the reach rather than the cost.** The remap
produces an *image file* because that is what the `.cnj` path can carry — a sidecar PNG the texture
loader reads like any other. Short-circuiting it means teaching that path to carry a raw pixel
buffer with its own halving flag, which is a format change for a case reachable only by an unlit
material with an occlusion map. Since `GLTF-215` an ordinary metallic-roughness material takes the
PBR path instead, where the occlusion map is bound **unremapped** and the shader applies §3.9.3's
strength formula itself — so this cost is paid by a narrow and shrinking set of assets.

## The gate's own time budget (`GLTF-404`)

What the glTF gate costs per commit, measured the same way (same machine, Debug build):

| Step | Measured | Budget |
|---|---|---|
| `scripts/regenerate-gltf-goldens.sh --check` | 0.14 s | 5 s |
| `scripts/regenerate-gltf-goldens.sh --determinism` (two full emissions) | 0.22 s | 10 s |
| `CnaTests --gtest_filter='*Gltf*'` | 21.5 s | 60 s |
| `ctest -L gltf-conformance` (10 rungs, each its own process) | 32.0 s | 120 s |
| the same selection under ASan + UBSan with `detect_leaks=0` locally | 105.5 s | 300 s |
| `EasyGL_Gltf_ContextLoss` (real offscreen Mesa GLES 3.2 context) | 0.15 s | 30 s |

**Under one minute** for the functional gate and **under three minutes** including the local
sanitizer selection, on the completed 145-asset corpus and 549 tests. Leak detection alone is off
in that local figure because this execution environment denies the process inspection it needs;
ASan and UBSan are active, and the identical CI matrix retains `detect_leaks=1`. The budgets leave
roughly a 3× margin on the slower native steps, for the same reason the performance assertions are:
a budget tuned to today's number fails on a loaded CI runner and gets raised until it means
nothing.

Enforcement is already in place and per rung rather than per suite: every `gltf-conformance` entry
is registered with `TIMEOUT 300` (`cmake/UnitTests.cmake`), so a rung that hangs fails *as that
rung* instead of stalling the job. The ladder costs more than the single filtered run (32.0 s versus
21.5 s) because each rung is a separate process that re-parses the corpus — deliberate, since it is
what makes CTest's own result line name the divergent layer (`GLTF-402`).

**What changed.** The earlier 75-asset estimate predicted near-linear growth. The completed corpus
is 145 assets, including four real Draco streams, and remains inside every existing budget without
raising one. Future rows must remeasure after materially increasing either fixture size or the
number of subprocess-based tool cases; no further extrapolation is needed for `GLTF-399`.

## What is not measured here, and why

| Row | Why not |
|---|---|
| Large-asset budgets (`GLTF ROBUST` §27.2 row 9) | Needs a ≥ 50 MB, ≥ 200 k-triangle, ≥ 150-joint asset, which is `GLTF-405`'s licensed third-party corpus. |
