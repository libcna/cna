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
| `generate.py` | the probe families, `build()` (batch 1) and `build_targeted()` (batch 2) |
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
7. **Components come out highest-face-index first**, which is why a mesh with no shared vertices
   comes back exactly reversed.
8. **The answer is a sequence of runs**, each a generalized triangle strip: from a face entered
   across the ordered pair `(older, newer)` with third vertex `r`, the next face carries
   `(newer, r)` -- the strip step -- or `(older, r)`, which keeps `older` as a fan pivot. Both are
   taken in the corpus, and the second is taken even where the first is available.
9. **An open strip or an open fan runs without a cap**: `row_24` answers 48 faces in one run and
   `ofan_16` 16.
10. **A closed fan of nine or more stops at exactly seven**, and one of eight does not stop at all;
    a two-ring cylinder behaves the same way (`cyl_2x4` runs 8, `cyl_2x6` runs 7 then restarts).

## What is not settled

Two rules, both isolated to a single decision each:

- **The seed.** "The highest-index unused face containing a vertex of minimum remaining live
  count" reproduces most seeds, and preferring a vertex still in the cache reproduces most of the
  rest, but `cfan_9` seeds at the *lowest* such face and `sphere_2x4_reversed` seeds at a face with
  no minimum-live vertex at all.
- **When a run ends.** `grid_4x4`'s first run and its second run reach, after eight faces, states
  that are identical in every feature the walk can read -- the same run length, the same live
  counts on the same shaped candidates, the same cache ages, the same absence of a preferred edge
  and the same available swap -- and the first restarts while the second takes the swap. No rule
  over live counts, cache position, run length, remaining faces, face index or vertex index can
  separate them, and the history probes rule out the emitted count. Whatever separates them is not
  in the state this model keeps.
