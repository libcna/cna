# CNA benchmark integration

## Status — 2026-10-27

| ID | Task | Status |
|---|---|---|
| BENCH-001 | Run `../../libcnalab/cna-benchmark` against current CNA and repair findings | Complete |

The benchmark's September defaults still named the retired `SDL_RENDERER` and absent
`cnanext`/`sharp-runtimenext` siblings. Its own repository now falls back to the current
`../../libcna/{cna,sharp-runtime}` checkouts, selects `SDL_GPU` in the default renderer set,
documents the current build and private-display commands, and registers a HEADLESS streaming
regression test.

The first OPENGLES3 run measured six of seven scenes. `streaming` failed on its second texture
upload with `The texture resource is in use.` SpriteBatch leaves the texture in sampler slot zero
after `End()`, and CNA's XNA resource-in-use rule correctly refuses `SetData` on a sampled texture.
The benchmark now unbinds that slot before the next upload. This is a consumer fix, not a change
to CNA's texture-binding rule.

The scene also exposed a CNA upload cost: `Texture2D::SetData(const Color*, int)` allocated a new
native texture on every full upload, even when the old renderer belonged to only one wrapper.
`BENCH-001` updates a unique single-level renderer in place. A shared renderer still detaches on
upload so cached value copies cannot observe each other's writes; render targets keep their
existing live-surface behavior. The new `RepeatedFullUploadsReuseAnUnsharedRenderer` test checks
renderer identity and pixel values across three uploads, beside the existing cache-isolation and
render-target tests. No new public API, stubs, dependencies or intentional behavior deviations
were introduced.

Evidence on this Linux host (Mesa under the private Weston/Xwayland runner):

- Release multi-renderer benchmark built with OPENGLES3, SDL_GPU, SOFTWARE, HEADLESS and STUB.
- Benchmark CTest: 10/10, including the new repeated streaming upload.
- Debug OPENGLES3 `Texture2DTest.*:Texture2DCacheReconstructionTest.*`: 55/55.
- Standard 60 warm-up + 300 measured-frame runs: all seven scenes completed on SDL_GPU and
  OPENGLES3, with output pixels verified and both JSON documents accepted by `verify-result.py`.
- SOFTWARE completed the streaming scene after the CNA change. HEADLESS completed seven scenes
  but cannot verify pixels; STUB correctly skipped its five unsupported 3D scenes.

These runs establish functional completion, not a stable performance comparison: the host is
shared and the before/after frame timings varied outside the changed scene. No further CNA
failure was reproduced in the benchmark's default renderer set.
