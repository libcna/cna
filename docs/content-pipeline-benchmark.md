# CNA Content Pipeline scheduler benchmark

> Status: developer performance evidence for `CP-028`, measured 2026-08-29. This is not a released
> performance guarantee or a CI threshold.

The reproducible harness is `tools/content/benchmark_content_pipeline.py`. It compares the explicit
`--workers 1` serial fallback with a selected parallel worker count and verifies the SHA-256 of the
complete output tree, including `.cna-content-manifest.json`, after every sample. A byte difference
between serial and parallel results fails the run instead of being reported as performance data.

Run it from the repository root after building the stock and example custom compilers:

```bash
python3 tools/content/benchmark_content_pipeline.py \
  --content-executable cmake-build-debug/cna-content \
  --custom-content-executable cmake-build-debug/cna_custom_content_compiler_example \
  --workers 4 \
  --iterations 7 \
  --json-output /tmp/cna-content-benchmark.json
```

The harness creates its fixtures outside the repository and removes them after the run. Fixture
copying, seed builds, output-tree verification, and cleanup are not timed. Serial and parallel
sample order alternates each iteration to reduce systematic filesystem-cache/thermal ordering
bias. Results are medians of wall-clock compiler subprocess duration; p95 is retained in JSON.

## CP-028 reference run

Reference revision: `191b56de7d386c47f3e230b59e93e04d2f320c5b`

Environment:

- Linux 6.12.100, glibc 2.41;
- AMD Ryzen 7 PRO 7840U, 8 cores / 16 logical CPUs;
- GCC 14.2.0;
- HEADLESS Debug build, no sanitizer;
- seven samples per worker count, alternating worker order.

The mixed fixture contains 32 PNG/Texture2D, 32 WAV/SoundEffect, 32 valid skinned glTF/Model and 32
Curve CNJ assets (128 primary nodes). The graph fixture contains one custom shared node and 96
custom parents; every custom writer also produces one bounded child output.

| Scenario | workers=1 median | workers=4 median | Median speedup | workers=1 p95 | workers=4 p95 |
|---|---:|---:|---:|---:|---:|
| mixed cold full build | 2.093700 s | 0.786388 s | 2.662x | 2.257120 s | 0.970620 s |
| mixed incremental no-op | 0.488066 s | 0.217601 s | 2.243x | 0.558645 s | 0.239037 s |
| one changed image in mixed tree | 0.452893 s | 0.208050 s | 2.177x | 0.468337 s | 0.222804 s |
| one changed shared dependency, 96 parents | 0.094938 s | 0.076481 s | 1.241x | 0.098621 s | 0.078200 s |

All serial/parallel tree checks passed. The smaller speedup for the custom graph is expected: its
nodes perform little parsing or encoding, so process, graph, staging, hashing, and filesystem costs
dominate. The measurements justify opt-in parallelism for non-trivial mixed directories while also
supporting the conservative `workers=1` default for small builds and custom components whose
reentrancy has not been established.

Because this is a shared developer host and a Debug build, absolute times must not be compared
across machines or used as a release threshold. The harness and JSON output are the authoritative
way to repeat the experiment after scheduler changes.

## XNAP-93: XNB output and texture-policy costs

> Status: developer performance evidence for `plans/plan_xnapipeline.md` `XNAP-93`, measured
> 2026-09-03. Not a released guarantee and not a CI threshold.

These are end-to-end `cna-content` wall-clock times — process start, source decode, processing,
serialization and atomic publication all included — so they answer "what does adding this cost a
build?" rather than "how fast is this loop?". Each figure is the best of three runs on an
otherwise-idle container, Debug build, single asset, single worker.

**Environment**: the same container the rest of this plan's work was done in; Linux 6.18, GCC 13,
`cmake-build-unit` (Debug, `CNA_PLATFORM=SDL3`, `CNA_GRAPHICS_RENDERER=STUB`), no sanitizer.

### One 1024x1024 RGBA source (1 megapixel, 4 MiB of level-zero pixels)

| Build | Time | Output |
|---|---|---|
| `Color`, no mips | 0.33 s | 4 194 491 bytes |
| `Color`, full mip chain (11 levels) | 0.46 s | 5 592 631 bytes |
| `Dxt1`, no mips | 1.08 s | 524 475 bytes |
| `Dxt5`, full mip chain | 1.71 s | 1 398 355 bytes |
| `Color`, LZ4 compressed | 0.53 s | 3 552 636 bytes |

What the differences say:

- **Mip generation costs about 0.13 s per megapixel** for the whole chain — the integer
  area-average resampler, run ten times over geometrically shrinking images.
- **Block compression is the expensive step, at roughly 1.4 megapixels per second.** That is
  slower than a tuned production encoder and it is a deliberate trade: the encoder refines two
  endpoint seeds per block (the colour bounding box and the most distant texel pair) rather than
  one, which is what stops a red-and-blue block collapsing to flat magenta. `refinementRounds` is
  the dial — 0 skips the least-squares refinement entirely — and quality is recorded in
  `plans/plan_xnapipeline.md` `XNAP-53`.
- **LZ4 runs at roughly 20 MB/s** and took this texture to 85% of its uncompressed size; the
  synthetic source has per-texel noise, so it is close to a worst case. A flat 32x32 texture goes
  from 4283 to 478 bytes.
- Serialization itself does not appear as a line here because it is not measurable against the
  rest: the `Color`/no-mips row is dominated by PNG decoding.

### One `.spritefont`, 190 glyphs at 32 px

| Build | Time | Output |
|---|---|---|
| `.spritefont` + TTF -> SpriteFont `.xnb` | 0.044 s | 1 058 241 bytes (512x512 atlas) |

FreeType rasterization, shelf packing and serialization together cost well under a tenth of a
second for a full Latin-1 font, so a project's font count is not a build-time concern.

### Large models

The committed glTF corpus exists to cover *shapes*, not sizes -- its largest fixture is a few
hundred kilobytes -- so this row was honestly absent until a source existed to measure.
`tools/xnb/generate_large_model.py` authors one deterministically rather than downloading one,
which would be neither reproducible nor licensable. It stresses what a Model build spends time on:
vertex and index counts, mesh and primitive counts (a part is three shared-resource references),
material count, and hierarchy depth.

| Scale | Vertices | Triangles | Parts | Nodes | Source | XNB | CNB | XNB time | CNB time |
|---|---|---|---|---|---|---|---|---|---|
| `small` | 1 352 | 2 304 | 8 | 16 | 63 848 B | 82 050 B | 82 864 B | 0.03 s | 0.02 s |
| `medium` | 60 000 | 110 592 | 96 | 168 | 2 659 384 B | 3 569 930 B | 3 578 592 B | 0.66 s | 0.62 s |
| `large` | 968 256 | 1 843 200 | 576 | 1 056 | 42 509 644 B | 57 697 840 B | 57 737 568 B | 10.01 s | 10.04 s |

Regenerate and re-measure with:

```bash
python3 tools/xnb/generate_large_model.py --scale large --out /tmp/large/src/model.glb
cna-content build /tmp/large/src -o /tmp/large/out --format xnb --quiet
```

What the three sizes say:

- **The Model route is linear in vertex count.** 60 000 vertices in 0.66 s and 968 256 in 10.01 s
  is 91 000 and 97 000 vertices per second -- the same rate at sixteen times the size. A Model
  writer that interned shared resources by identity rather than by value, or that rebuilt the bone
  table per part, would show a rate that fell as the model grew; this one does not. The committed
  `LargeModelScalingTest` asserts the same property on the output-size axis, which is the half a
  timing run on one machine cannot be trusted for.
- **XNB and CNB cost the same to within noise** (0.66 vs 0.62 s, 10.01 vs 10.04 s). Both are fed
  by one importer and one processor, and serialization is not where the time goes -- glTF decoding
  and canonical-model construction are.
- **Peak memory is roughly 6x the source**: 33 MiB for the 2.6 MB medium source, 346 MiB for the
  42.5 MB large one. The decoded glTF document, the canonical model and the output buffer are all
  live at once. A build machine sizing itself for content should budget from the largest single
  asset, not from the total.
- **The warm rebuild of the large model is 1.72 s**, all of it hashing the 42.5 MB source to prove
  it has not changed. Incremental correctness is not free on a large asset, but it is six times
  cheaper than rebuilding.

Not measured: nothing on this page has been run on anything but the container described above, and
none of it is a threshold. A build that got twice as slow would still pass every test in the
repository; these numbers exist so that somebody notices.

### What this means for a real build

The pipeline parallelizes across assets (`--workers`), not within one, so these are per-asset
costs on one core. A project whose textures are all `DxtCompressed` should expect block
compression to dominate its content build, and should expect the incremental manifest to make that
cost appear once rather than every build. A project with one very large model should expect that
model to set its wall clock, because no worker count divides a single asset.
