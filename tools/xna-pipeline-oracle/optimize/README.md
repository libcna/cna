# `MeshHelper.OptimizeForCache`, measured

`plans/plan_xna_sample_xnb_sweep.md` `XNASWEEP-149`. Every unexplained reference left in the sweep
turns on the order genuine XNA 4.0's `MeshHelper.OptimizeForCache` puts a mesh's triangles in, or
on a number that follows from it. This directory is the black-box laboratory for that one method:
it generates small meshes whose whole answer can be reasoned about, runs the **genuine** method
over them under Wine, and scores candidate reconstructions against what came back.

Rounds one to five read nothing of Microsoft's implementation: the only input was what the method
*does*. Round six is different and deliberately so -- the project owner authorised the use of
Microsoft's **open-source, MIT-licensed DirectXMesh** on 2026-09-09, which publishes the same
algorithm the legacy D3DX entry point used. Nothing here decompiles or disassembles anything;
`d3dx9_43.dll` and the XNA assemblies are still black boxes, loaded by their documented names and
observed only through what they return.

## Running it

```bash
python3 tools/xna-pipeline-oracle/optimize/generate.py \
        build/xna-sample-sweep/optimize/probes.txt \
        build/xna-sample-sweep/optimize/probes.json
bash tools/xna-pipeline-oracle/optimize/run-optimize-oracle.sh \
        build/xna-sample-sweep/optimize/probes.txt \
        build/xna-sample-sweep/optimize/answers.txt
python3 tools/xna-pipeline-oracle/optimize/model.py
```

The measured answers are frozen under `tests/reference/xna40/optimize/`; the generator is
deterministic, so a re-run reproduces the probes byte for byte.

## Files

| File | What it is |
|---|---|
| `generate.py` | the probe families: `build()` (batch 1), `build_targeted()` (batch 2), `build_rules()` (batch 3) |
| `OptimizeForCacheOracle.cs` | the driver: reads a probe file, calls the genuine method, prints the answer |
| `run-optimize-oracle.sh` | compiles it with `mcs` and runs it under Wine against the real assemblies |
| `corpus.py` | loads probes + answers and decodes each answer into a permutation of the input faces |
| `probes.py`, `oracle.py`, `dump.py` | the same for the 35 `OptimizeForCache` cases in the graphics oracle |
| `mine.py`, `decisions.py`, `replay.py`, `search.py` | the walk replayed over the truth, one row per decision |
| `families.py` | published algorithm families, implemented from their descriptions, scored |
| `model.py`, `hoppe.py` | the black-box reconstructions of rounds four and five, and their scores |
| `D3dxOptimizeOracle.c`, `run-d3dx-oracle.sh` | the genuine D3DX9 redistributable, under Wine |
| `DxMeshOptimizeOracle.cpp`, `run-dxmesh-oracle.sh` | the genuine DirectXMesh library, cross-compiled and run under Wine |
| `dxmesh.py` | a line-for-line port of DirectXMesh's `OptimizeFaces`, and the candidate adjacencies |
| `dxcompare.py` | scores the port and the binary against genuine XNA, and the port against the binary |
| `nonmanifold.py` | the 594 probes that decide the adjacency, where three or more faces meet on one edge |
| `heldout.py` | 240 fresh probes generated after the rule was settled |
| `validate_model.py`, `validate_corpus.py` | the same question asked of real corpus models |

## What the measurements settle

1. **The emitted triangle keeps its input corner order.** Not one of the 233 probes comes back with
   a triangle rotated or reversed, so the whole answer is a permutation of the face list.
2. **Vertex coordinates are ignored.** `grid_4x4_far_coords` scales every position by 1000 and
   answers the identical permutation.
3. **Vertex numbering is ignored.** `grid_4x4_vertexflip` and `grid_4x4_vertexshuffle` renumber
   every position and answer the identical permutation.
4. **Winding is ignored.** `strip_8_wind` reverses every triangle and answers the identical
   permutation.
5. **The input face order decides.** `grid_4x4_perm7`, `_perm3` and `_shuffled` are the same
   topology in a different list order and answer differently.
6. **A connected component's answer does not depend on what was processed before it.**
   `hist_grid44_soup6_after` puts six disjoint triangles ahead of the grid in the emitted order and
   the grid's own 32 faces come back exactly as they do alone; `hist_grid42_twice` answers two
   disjoint copies of one grid identically.
7. **A component is not entered because it is last in the list but because it holds the seed the
   rule picks.** `pair_fan_strip_stripfirst` puts a strip at indices 0--5 and a closed fan at
   6--11 and the *strip* comes first, because only the strip has a vertex of minimum live count;
   a mesh with no shared vertices comes back exactly reversed for the same reason, every one of
   its vertices being minimal.
8. **The answer is a sequence of runs**, each a generalized triangle strip: from a face entered
   across the ordered pair `(older, newer)` with third vertex `r`, the next face carries
   `(newer, r)` -- the strip step -- or `(older, r)`, which keeps `older` as a fan pivot. Both are
   taken in the corpus, and the second is taken even where the first is available.
9. **An open strip or an open fan runs without a cap**: `row_24` answers 48 faces in one run and
   `ofan_16` 16.
10. **A closed fan of nine or more stops at exactly seven**, and one of eight does not stop at all;
    a two-ring cylinder behaves the same way (`cyl_2x4` runs 8, `cyl_2x6` runs 7 then restarts).
    The stop is not a cap: `ladder_8_at0` -- a row of eight quads with one quad below its far end
    -- answers **sixteen** faces in one run, and `row_24` forty-eight.
11. **A closed fan's answer does not depend on where its face list starts or on the centre's
    number.** `cfan9_rot0`, `_rot2`, `_rot4`, `_rot6`, `_rot8`, `cfan9_centre0`, `_centre5` and
    `_centre9` all answer the identical index sequence.

## The sharpest measurement, and what it turned out to be about

Batches four to seven are one experiment repeated. A strip of `n` faces carries one extra triangle
glued to face `T`; the strip's face list is then rewritten so that face `T` sits at index `j`, once
by swapping it with the face at `j` and once by removing it and re-inserting it there, and `j` is
swept over the whole list. Nothing else changes.

The seed's first move flips at exactly `j = T`: it continues into the neighbour for every `j < T`
and ends the run after one face for every `j >= T`, on a 16-face strip and a 24-face one, with `T`
of 2, 8 and 14, and the swap and the insert agree at every `j`.

**Batch eight says what that is about, and it is not the index.** The pendant triangle is
`(a, extra, b)` with `(a, b)` the first two corners of face `T` -- and in a strip that edge already
carries *two* faces, `T` and `T - 1`. 108 of these 112 probes hold an edge with three faces, and
the walk's move across it is decided by which of the two candidates comes first in the input list:

| the first face on the edge, other than the current one | what the walk does |
|---|---|
| the one wound consistently with the current face | crosses to it |
| the one wound the same way, which no strip walk can enter | ends the run, and does **not** look at the second |

`ins_f8_at07` puts the consistently wound face at index 7 and the other at 8, and the walk crosses;
`ins_f8_at08` swaps them and the walk restarts at the far end of the strip although the
consistently wound face is still there, one index further on. `chain_j5_a15_b0` and
`chain_j5_a0_b15` hold the neighbour's own index fixed at 5 and move only the two faces sharing its
edge, and the answer follows them and not it. So an edge's faces are looked up in input order, the
first is taken, and a wrongly wound first candidate ends the run rather than being skipped.

That rule is exact on this family, and putting it into `model.py` moves the reconstruction from
135 of 372 probes to **179** -- so it is not only about pendant triangles. Three readings of "the
face across this edge" were scored: taking the first face listed on the edge and ending the run
where it is used or wrongly wound scores 179; skipping the face the walk stands on first scores
160; taking the first *unused* face, which is what a plain adjacency walk does, scores 135. A fourth -- passing over the *used*
faces and ending the run only when the first **unused** survivor is wrongly wound -- scores 178.

**The fourth is the one kept, and the aggregate is the reason to distrust the others.** Three
probes discriminate between it and the 179-scoring reading, and all three want the fourth:
`ins_f8_at08` restarts because the first unused face on its edge is wrongly wound; `strip_8_rot1`
walks its whole strip forward, which is only possible if the used face the edge lists first is
passed over; and `grid_4x4` leaves its first run after two faces under any reading that stops at a
used face. With a seed pivot taken as the *last* minimum-live corner of the seed face rather than
the first -- worth 18 probes on its own -- the reconstruction stands at **178 of 372**, nineteen
below a reading that is wrong wherever the corpus can tell.

## Round five: the method is `D3DXOptimizeFaces`, and the algorithm is published

The reconstruction above was built decision by decision and stopped at 178 of 372, with two pairs
of decisions that were identical in every feature the walk could read and came out differently.
It did not need to be inferred.

**Genuine XNA 4.0's `MeshHelper.OptimizeForCache` is the documented public `D3DXOptimizeFaces`,
with the vertices renumbered in the order the reordered face list first reaches them.**
`D3dxOptimizeOracle.c` runs the Microsoft D3DX9 redistributable over the same probe file the .NET
oracle runs, `d3dx.py` scores the two against each other, and `heldout.py` generates 240 fresh
probes -- grids, cylinders, fans, spheres and pseudo-random triangle soups, with random face
orders, random corner rotations, half the triangles reversed, shared or unshared at random -- so
that a disagreement is possible.  **616 probes, 7,481 held-out faces, no disagreement**, and the
first-encounter vertex renumber reproduces XNA's vertex order on all 616.

`D3DXOptimizeFaces` in turn implements a published algorithm: Hugues Hoppe, *Optimization of mesh
locality for transparent vertex caching*, SIGGRAPH 1999, whose Figure 3 gives the pseudocode of the
greedy strip-growing technique.  Microsoft's own public documentation for the same algorithm's
later library gives the two constants the D3DX9 entry point used -- a simulated vertex cache of
**12** and a restart threshold of **7** -- and the remap direction, `oldLoc = faceRemap[newLoc]`,
which is what the corpus had already picked.  The queue `Q` of restart locations in that pseudocode
is the "data structure whose order depends on the order faces were added" that round four
concluded had to exist.

`hoppe.py` implements it.  What the paper leaves to the implementation is measured here:

| Rule | Measured | Evidence |
|---|---|---|
| **Adjacency.** A face has at most one neighbour per edge: the *first other face in input order* on that edge, and only when it is wound the opposite way.  A later face on the same edge is never reached. | exact | `ins_f8_at07` crosses, `ins_f8_at08` restarts although the consistently wound face is one index further on |
| **Seed.** The unvisited face with the fewest such neighbours; the scan keeps the **last** minimum, which is why a mesh of disjoint triangles comes back exactly reversed. | **616 / 616** | every probe's first face, designed and held-out |
| **Direction.** A face entered across edge `e` continues across edge `(e + 2) % 3`, and pushes `(e + 1) % 3` on Q.  A seed offers all three edges in index order and leaves by the first available. | **1,088 / 1,088** and **400 / 400** | every decision where both continuations were live; every seed with a neighbour |
| **Restart target.** The first unvisited face of Q, FIFO, after which Q is cleared; a global reseed when Q holds none. | 545 / 577 probes | LIFO scores 409 against 461 |

**What is not settled is when a strip is cut**, and the reconstruction stands at **461 of 616** on
that alone.  Hoppe's published rule -- restart when `C(0) < C(i)` for every `i` in a lookahead of
`k + 5` simulations -- is implemented and **falsified**: on a closed fan continuing and restarting
cost exactly the same, so the strict form never fires, and genuine D3DX cuts a twelve-face fan
after seven.  The non-strict form fires on 5,867 of 7,019 decisions that were not cuts.

Two designed sweeps say what the cut actually tracks:

- **Closed fans of 3 to 40 triangles.** Up to eight the fan comes back in one run; from nine on it
  runs exactly **seven** faces and then takes the queued face.  The seed pushes on Q at the first
  face.
- **A row of twelve quads with one correctly wound pendant triangle glued to the bottom edge of
  face `2j`.** The pendant seeds; for every `j >= 4` the first run is exactly **eight** faces.  The
  push happens at the *second* face, not the first.  The same row with no pendant runs all
  forty-eight faces in one strip, and never cuts at all.

So the counter starts when a restart location is first queued, not when the strip starts.  Counting
faces from the pushing face gives 7 for both sweeps, but over the whole corpus it takes values 6
to 12; counting **cache misses** from the same point is tighter -- 5 or 6 in 322 of 371 cut strips
-- and is not constant either.  371 cut strips are recorded; the rule that reproduces all of them
is the one thing this method still owes.

## Round six: the method is Microsoft's own open-source `OptimizeFaces`, and it is exact

Round five settled everything except *when a strip is cut*, and stopped at 461 of 616 on that
alone.  It did not need to be inferred either.

**Microsoft's DirectXMesh is the same algorithm, published under the MIT licence.**  Its
`DirectX::OptimizeFaces` implements Hoppe's greedy strip-growing reorder, and Microsoft documents
it as what the legacy D3DX9 entry point did, with `D3DXMESHOPT_DEVICEINDEPENDENT`-equivalent
defaults -- the same cache of 12 and restart threshold of 7 round five had already measured.  The
project owner authorised its use on 2026-09-09.  `DxMeshOptimizeOracle.cpp` links the genuine
upstream library (`~/deps/DirectXMesh`, revision `bd17eb215d463d98f2b3a13082ce13979219314f`, tag
`oct2025`), cross-compiled with MinGW and run under Wine over the same probe file the other two
oracles read; `dxmesh.py` is a line-for-line port of the same code, and `dxcompare.py --verify`
checks the port against the binary on every probe so the experiments below are measured on
Microsoft's algorithm rather than on a paraphrase of it.

**What the cut rule turned out to be** answers the two facts round five recorded, and says why
neither was a threshold.  The decision is not asked before every face: it is asked at the face
where the strip can no longer go straight, which is exactly the 2,467 decisions the corpus
isolated.  At that point the walk counts `nf`, the faces a counter-clockwise ring from here would
still reach, and restarts when `locnext + nf > vertexCache - restart` -- five.  `locnext` is the
number of **cache misses since a restart location was queued**, and it is reset to zero at the
moment of queueing, which is round five's "the counter starts when a restart location is first
queued".  It takes values 5 or 6 in most cut strips because `nf` is usually 0 or 1, which is why a
threshold on misses alone almost worked and could not be made to work.  The queue itself is one
pending corner, not a FIFO of many.

## Round six's one open question, and the answer the corpus gave

`D3DXOptimizeFaces` computed its adjacency internally; `DirectX::OptimizeFaces` is handed one.  So
the only thing left to measure was which adjacency reproduces the legacy entry point.

Scored on all 616 probes, with the genuine library:

| adjacency handed to `DirectX::OptimizeFaces` | designed 376 | held-out 240 |
|---|---:|---:|
| the first other face in input order on each edge (round four's reading) | 331 | 212 |
| DirectXMesh's own `GenerateAdjacencyAndPointReps`, over the positions | 373 | 231 |
| **the pairing below** | **376** | **240** |

and the split is total: **every one of the 449 probes whose every edge carries at most two faces
comes back exactly right under all three readings**, and all 167 disagreements are probes with an
edge three or more faces meet on.  `nonmanifold.py` generates 594 probes for that one question --
three and four faces on a single edge, every winding, every input order; the same with a manifold
strip hanging off, so a crossing is observable; two such edges in one mesh; and a row of quads with
a third face on one interior edge.  Genuine XNA, genuine D3DX and DirectXMesh were all run over
them.

**The rule.**  An undirected edge holds the faces that carry it, in input order.  Walking those in
input order, a face that is not yet paired takes the **last** still-unpaired face on that edge
whose own winding on it is the reverse; the two are paired and both leave the pool, and a face left
over gets no neighbour there.  On an edge two faces meet on this is simply "link them when they are
wound the other way", which is why no manifold probe can see it.

It was not guessed.  For the three-faces-on-one-edge family every possible adjacency was
enumerated and simulated, and the answers admit exactly one physical adjacency: the first face on
the edge pairs with the last one wound the other way, and the middle one is left isolated -- which
`first other` gets wrong whenever the first face's own first neighbour is wound the same way, and
`first opposite` gets wrong whenever a later face would also have served.  The four-face family
then separates "last unpaired opposite" from "last opposite" outright.

## Where it stands

| | designed 376 | held-out 240 | non-manifold 594 | total |
|---|---:|---:|---:|---:|
| genuine XNA vs genuine D3DX9 | 376 | 240 | 594 | **1,210 / 1,210** |
| genuine XNA vs genuine DirectXMesh + this adjacency | 376 | 240 | 594 | **1,210 / 1,210** |
| genuine XNA vs **CNA production** | 376 | 240 | 594 | **1,210 / 1,210** |

The last row is `CnaContentPipelineTests`'s `XnaOptimizeForCacheProbes` suite, which rebuilds each
probe the way the .NET oracle's driver builds it, runs `MeshHelper::OptimizeForCache` over it and
compares the vertex order and the whole index buffer.  Production is
`modules/content-pipeline/src/Internal/DirectXMeshOptimizeFaces.cpp` (the algorithm, adapted from
DirectXMesh, MIT, attributed in `THIRD_PARTY_NOTICES.md` and
`tools/provenance/derived-sources.json`) and `LegacyMeshAdjacency.cpp` (the pairing above, which is
CNA's own measurement and is MS-PL).

## Running the third oracle

```bash
python3 tools/xna-pipeline-oracle/optimize/nonmanifold.py \
        build/xna-sample-sweep/optimize/nm-probes.txt \
        build/xna-sample-sweep/optimize/nm-probes.json
bash tools/xna-pipeline-oracle/optimize/run-dxmesh-oracle.sh legacy \
        build/xna-sample-sweep/optimize/nm-probes.txt \
        build/xna-sample-sweep/optimize/nm-dxmesh.txt
python3 tools/xna-pipeline-oracle/optimize/dxcompare.py                 # every adjacency, scored
python3 tools/xna-pipeline-oracle/optimize/dxcompare.py --verify \
        build/xna-sample-sweep/optimize/nm-dxmesh.txt pair_last          # port == binary
```

The runner refuses to build against a DirectXMesh checkout that is not at the pinned revision.

## Where the cut rule stands, and the two facts that narrow it

Everything above except *when a strip is cut* is exact.  These are the measurements a next attempt
should start from rather than repeat.

**A strip is only ever cut where it has to turn.**  Over all 616 probes there are 9,645 mid-strip
decisions with at least one live candidate, and they split by which candidate is available:

| available | queue | what truth did |
|---|---|---|
| the preferred edge `(e+2) % 3` | either | took it, 2,706 times, **never cut** |
| both edges | either | took the preferred one, 2,129 times, **never cut** |
| only `(e+1) % 3` | empty | took it 2,340 times, cut 3 |
| only `(e+1) % 3` | live | took it 2,118 times, **cut 349** |

So the decision is not "should this strip end" asked before every face.  It is asked once, at the
face where the strip can no longer go straight, and it is a choice between the turn and the queued
restart.  Every one of the 371 cut strips ends at such a face.

**The counter starts when a restart location is first queued, not when the strip starts.**  Two
designed sweeps:

- closed fans of 3 to 40 triangles -- up to eight the fan comes back whole; from nine on it runs
  exactly **seven** faces and takes the queued face.  Its seed pushes on Q at the first face.
- a row of twelve quads with one correctly wound pendant triangle glued to the bottom edge of face
  `2j` -- the pendant seeds, the push happens at the *second* face, and for every `j >= 4` the run
  is exactly **eight**.  The same row without the pendant never pushes and never cuts: all
  forty-eight faces come back in one strip.

Both are seven faces counted from the pushing face.  Over the whole corpus that count takes values
6 to 12, and counting cache misses from the same point takes 5 or 6 in 322 of 371 cut strips, so
neither is the counter by itself.

**Ruled out by implementation and measurement** before round six answered it -- kept because it is
what a reconstruction of this rule costs, and because each row is still true of the rule it names:

| candidate | result |
|---|---|
| Hoppe's published test, `C(0) < C(i)` over `k + 5` simulations | fires on 15 of 208 cuts; on a closed fan continuing and restarting cost exactly the same, so the strict form can never fire, and D3DX cuts a twelve-face fan after seven |
| the same, non-strict | fires on 5,867 of the 7,019 decisions that were *not* cuts |
| the same, gated on a minimum strip length, over horizons 12/13/17, sims 7/12/17, average or total misses | best 276 of 370 designed probes -- worse than no lookahead at all |
| a hard cap on strip length | best at 7, and worse than the queue-aware rule; `row_24` runs 48 faces |
| a k-face lookahead at the turning face, comparing the turn against the queued restart | catches 239-299 of 349 with 546-1,088 false positives, at every horizon from 4 to 17 |
| LIFO instead of FIFO on Q | 409 of 616 against 461 |
| favouring a restart seed whose vertices are still cached, as the paper says | worse in every configuration tried |
| a threshold on faces since the push, misses since the push, cache occupancy, queue length, the queued face's cached vertices, or how close they are to eviction | none separates; several are non-monotonic, which no threshold can be |

That reconstruction cut `restart` faces after the first push and reached **461 of 616**.  It is
`hoppe.py`, and it is kept as the record of what black-box reconstruction reached; `dxmesh.py` is
what production is measured against now.
