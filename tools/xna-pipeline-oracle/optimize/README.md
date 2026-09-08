# `MeshHelper.OptimizeForCache`, measured

`plans/plan_xna_sample_xnb_sweep.md` `XNASWEEP-149`. Every unexplained reference left in the sweep
turns on the order genuine XNA 4.0's `MeshHelper.OptimizeForCache` puts a mesh's triangles in, or
on a number that follows from it. This directory is the black-box laboratory for that one method:
it generates small meshes whose whole answer can be reasoned about, runs the **genuine** method
over them under Wine, and scores candidate reconstructions against what came back.

Nothing here reads Microsoft's implementation. The only input is what the method *does*.

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
| `model.py` | the current reconstruction, and its score |

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
