# glTF import performance and lifetime (`GLTF-433`…`GLTF-443`)

Each `GltfPerformance` case records what it measured and asserts a ceiling generous enough that
only a pathological regression trips it. The decisions below cite one measured number each; they
are fixed-cost observations on tiny fixtures, not a benchmark of the current tree.

## How to reproduce

```bash
tools/platform/run_gpu_tests_private.sh --exec env CNA_GRAPHICS_RENDERER=STUB \
  cmake-build-multi/CnaContentTests --gtest_filter='GltfPerformance.*' \
  --gtest_output=xml:/tmp/gltf-perf.xml
```

Every measurement is a `RecordProperty`, so the numbers survive in the XML rather than in a log
nobody keeps. Durations are recorded in **microseconds** (the property names say `_x1000` because
they are thousandths of a millisecond).

The helper measures calling-thread CPU time: the fastest call on non-Windows, or a whole-batch mean
on Windows. It is not end-to-end wall time and can exclude native driver/worker work. ASan builds
multiply generous absolute ceilings by ten; a cached load must also be faster than a fresh parse.
Tiny fixed fixtures catch gross regressions/cache loss, not asymptotic complexity or large-asset
scaling. Truncated integer microseconds can report zero for small work. Use a matching
single-renderer configuration if the multi tree is unavailable.

## The decisions

### `GLTF-433` — a GLB is not re-parsed on every load

`ContentManager` caches by asset name — which is XNA's own documented contract for `Load<T>` — so
**successful loads are cached per manager/name**. A second load of a loaded name was a cache hit
roughly 47× cheaper than the fresh parse (about 2 µs versus 95 µs on a three-vertex asset). C++
`Load<Model>` returns a value; that copy shares the cached graph/resources, not the address of the
Model wrapper.

That has a consequence worth stating plainly, because it surprises people: two `Load<Model>` calls
with one manager return the same object graph, so mutating one model's effect mutates "both". A
caller who wants two independent copies uses two managers, or `Unload`s between them.
`LoadingOneNameTwiceReturnsTheSameInstanceAndAFreshManagerDoesNot` pins both directions.

**Decision: no cache added.** There is one, at the level XNA puts it.

### `GLTF-434` — the offline converter parses once

Each normal Convert call in `tools/gltf_to_cnj/gltf_to_cnj.cpp` parses once, builds the scene graph
once and walks the groups from that document. The separate `--dump-oracle` command has its own
parse; two source call sites do not mean a normal conversion reparses once per group.

**Decision: documented, nothing to fix.**

### `GLTF-435` — logical accessor expansion is distinct from peak memory

`UnpackAccessor` expands requested attributes to float before packing narrows them again. The
temporary is exactly `count × components × 4` bytes — arithmetic, not a timing, so the number is
identical on every machine. `AccessorUnpackTemporaryBytesAreComputedForTheWholeCorpus` records
the corpus totals; because most corpus data is already float, the total expansion is close to 1×.
The worst *single* accessor expands **4×**, which is the format's own worst case — an
`UNSIGNED_BYTE` component widened to a `float`. A normalized `SHORT` stream expands 2×; a `float`
stream not at all.

The test sums count*components*component-size logical dense payload, not actual file/storage
bytes: padding, sparse representation and Draco compression are not that denominator. Some
attributes are not unpacked on a selected path. The 4× bound applies to widening a byte component,
not to all importer memory versus an encoded file.

Peak overhead is not one accessor: the packing function retains position, normal, UV, weight and
color vectors together, plus connectivity/tangent/split/packed data as needed. One 200k-vertex
POSITION float stream alone is 2.4 MB; several such streams can overlap.

**Decision: documented, not reduced.** Removing it means decoding each component directly into the
packed layout — a per-format switch inside the packing loop, replacing one vectorisable pass with
branchy per-component work, to save a transient that is 4× the *narrowest* possible input. The
measurement does not justify that, and `GLTF-041` exists to keep this decode path stable.

### `GLTF-436` / `GLTF-438` — 1 000 cycles, and where the leak assertion actually lives

`AThousandLoadUnloadCyclesLeakNothing` creates/destroys a fresh manager and graph 1,000 times and
records the per-call estimator. There is no in-test leak counter: leak evidence requires an
effective leak-detection run. `.github/workflows/gltf-sanitizers-ci.yml` runs this file in its
ASan+UBSan build with `ASAN_OPTIONS=detect_leaks=1`, where a leak becomes a failure; in an
ordinary build it is a stress test that proves the loop survives.

`UnloadThenLoadAgainYieldsAWorkingModel` covers the half a caller can check without a sanitizer:
after `Unload`, the manager is still usable and the model it returns is a real one — effect, vertex
buffer and a positive primitive count — rather than a husk pointing at freed buffers.

### `GLTF-437` — the sharing boundary is the manager, not the load

Within one `ContentManager` a second load returns the cached model and decodes nothing. Across two
managers, the same asset is decoded twice — textures included
(`TexturesAreSharedWithinAManagerAndNotAcrossManagers`).

**Decision: keep it.** Separate managers have separate loaded graphs. A hypothetical shared cache
could use ownership/refcounts, but is not the implementation. The fresh-parse fixture excludes the
image decode costs a textured asset adds; it is not a general second-manager timing formula.

### `GLTF-440` — a copied `Model` shares its resources and outlives the original

`Model::ownedResources_` is a `shared_ptr<void>`, so copying a `Model` shares the buffers rather
than duplicating them. `ACopiedModelSharesItsOwnedResourcesAndOutlivesTheOriginal` pins the property
a caller depends on without knowing it: the copy is still valid after the original is destroyed, and
still points at the same vertex buffer.

**Decision: documented and tested.** The alternative — deep-copying on assignment — would silently
double the memory of every model a caller passes by value.

### `GLTF-441` / `GLTF-442` — two vertex payloads are not the total morph memory

Morphing is CPU-side: `SetMorphWeightsEXT` blends `BaseVertexBytes` into a temporary vector and
re-uploads it. Uploaded payload plus base pose gives two logical payloads (19.2 MB for 200k
vertices at stride 48), but this excludes target deltas, weights, connectivity, blend/recomputed
normal scratch, native storage and CPU upload/recovery shadows. The test's reported 200% is a
hard-coded accounting convention, not a measured process-memory ratio or peak bound.

The re-blend itself is linear in vertex count and below 1 µs on the corpus's three-vertex fixture,
which is precisely why the memory figure is the one that matters here.

**Decision: scoped as ROBUST, not fixed now.** A GPU morph path needs a shader variant on every
PBR-capable renderer, and a renderer change nobody can verify is not a fix (`GLTF-157`). The
duplication is documented so a caller sizing a scene can predict it.

### `GLTF-443` — the occlusion remap is three codec passes

The dual-texture lightmap approximation decodes the occlusion PNG, halves one channel, and
re-encodes it as PNG — which the texture loader then decodes a *second* time. Three codec passes for
an arithmetic operation on one channel, about **73 µs** for a 1×1 image, i.e. essentially all fixed
cost.

**Decision: not short-circuited, and the reason is the reach rather than the cost.** The remap
produces an *image file* because that is what the `.cnj` path can carry — a sidecar PNG the texture
loader reads like any other. Short-circuiting it means teaching that path to carry a raw pixel
buffer with its own halving flag, which is a format change for a case reachable only by an unlit
material with an occlusion map. An ordinary metallic-roughness material takes the PBR path
instead (`GLTF-215`), where the occlusion map is bound **unremapped** and the shader applies
§3.9.3's strength formula itself — so this cost is paid by a narrow set of assets.

## The gate's own time budget (`GLTF-404`)

| Step | Budget |
|---|---|
| `scripts/regenerate-gltf-goldens.sh --check` | 5 s |
| `scripts/regenerate-gltf-goldens.sh --determinism` (two full emissions) | 10 s |
| `CnaTests --gtest_filter='*Gltf*'` | 60 s |
| `ctest -L gltf-conformance` (each rung its own process) | 120 s |
| the same selection under ASan + UBSan | 300 s |
| `EasyGL_Gltf_ContextLoss` (real offscreen Mesa GLES 3.2 context) | 30 s |

The budgets leave roughly a 3× margin over the measured Debug times of the slower native steps, for
the same reason the performance assertions do: a budget tuned to today's number fails on a loaded
CI runner and gets raised until it means nothing. Remeasure after materially increasing either
fixture size or the number of subprocess-based tool cases.

Enforcement is per rung rather than per suite: the L0–L6/Perf/Ledger/Tool loop registers each entry
with `TIMEOUT 300` (`cmake/UnitTests.cmake`); separately registered L7/auxiliary checks have their
own policies. A timeout identifies the affected rung; it is not a blanket timeout for every labeled
test or a bound on the entire job. The ladder costs more than the filtered run because each rung is
a separate process that re-parses the corpus — deliberate, since it is what makes CTest's own
result line name the divergent layer (`GLTF-402`).

## Large-asset budgets (`GLTF ROBUST` row 9)

Measured against `GLTF-405`'s fetched, pinned reference models. The assets are **not** committed —
`GLTF-019` decided a benchmark input is fetched rather than carried, because it is an input and not
evidence — so the budget tests are opt-in and skip without them:

```bash
scripts/fetch-gltf-sample-assets.sh DEST Sponza RecursiveSkeletons
CNA_GLTF_LARGE_MESH_ASSET=DEST/Models/Sponza/glTF/Sponza.gltf \
CNA_GLTF_MANY_JOINT_ASSET=DEST/Models/RecursiveSkeletons/glTF/RecursiveSkeletons.gltf \
  tools/platform/run_gpu_tests_private.sh --exec \
  cmake-build-multi/CnaContentTests --gtest_filter='GltfToCnjToolTest.LargeReference*'
```

No single model in the pinned sample set meets all three thresholds: the row's ≥ 50 MB, ≥ 200 k
triangles and ≥ 150 joints do not co-occur, so the pair below meets them between them and each
threshold is named with the asset that actually carries it.

| Asset | On disk | Triangles | Joints / nodes | Median wall | Peak RSS | `.cnj` out |
|---|---|---|---|---|---|---|
| `Sponza` | 50.2 MB | 262 267 | 0 / 1 | **5.31 s** | **18.8 MB** | 53 MB |
| `RecursiveSkeletons` | 1.0 MB | 76 | **840** over 84 skins / 924 | **1.16 s** | 8.2 MB | 9.6 MB |
| `BrainStem` (control) | 3.2 MB | 61 666 | 18 / 22 | 0.99 s | 17.9 MB | 6.3 MB |

Three runs each on an idle machine, median reported, Debug build of `cna_tool_gltf_to_cnj`; wall
clock and peak RSS from `/usr/bin/time -v`.

**The memory number is the one worth reading twice: a 50 MB asset imports inside 18.8 MB.** Peak
RSS does not track asset size because it does not track the part that makes these assets large.
Sponza is 50 MB of JPEG/PNG, and the converter copies encoded image files through to the output
rather than decoding them — 69 images arrive in the `.cnj` payload as files. The observed resident
set is dominated by geometry/working data, which is why 262 k triangles (18.8 MB) and 61 k
triangles (17.9 MB) land so close together and why the 840-joint asset, whose geometry is 76
triangles, is the smallest of the three at 8.2 MB.

**That bound is the offline converter's, not the runtime's.** `ContentManager`'s direct `.gltf`
path decodes every image into a `Texture2D`, so an application loading Sponza pays the decoded
texture payload on top of this, and that figure is a renderer-memory question this document does
not answer. Stated here so the 18.8 MB is not read as the cost of showing Sponza on screen.

The opt-in tests' wall-time ceilings are 60 s and 20 s — an order of magnitude above the medians,
because a ceiling tuned to today's number is a flake generator. Peak RSS is deliberately *not*
asserted in a test: `ru_maxrss` is a process-wide high-water mark, so inside the shared `CnaTests`
binary it reports whichever suite ran first, not this import.

## What is not measured here, and why

| Row | Why not |
|---|---|
| Runtime (`ContentManager`) memory for a large textured asset | Needs decoded-texture accounting through a real `GraphicsDevice`; the offline bound above deliberately excludes it. |
