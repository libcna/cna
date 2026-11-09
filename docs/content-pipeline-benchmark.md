# CNA Content Pipeline performance records

The scheduler harness is `tools/content/benchmark_content_pipeline.py`. It compares explicit
`--workers 1` with a selected parallel count, hashing sorted relative file paths and contents
(including `.cna-content-manifest.json`) after each sample. It compares the resulting digest sets
at the end of each scenario and fails on differences, including differences between repetitions
at one worker count. Empty directories, permissions and timestamps are not part of this check.
This tests byte determinism for the selected fixtures, not every custom component or graph.

**Status of every table on this page:** developer performance evidence measured on a shared
developer host with a Debug build (CP-028 on 2026-08-29, XNAP-93 on 2026-09-03, XNAPP-301 on
2026-09-07). None is a released performance guarantee or a CI threshold, and absolute times must
not be compared across machines. The original binary hashes, full settings and raw samples are not
retained here; a current run measures the current source/dependency/build configuration.

Run it from the repository root after building the stock and example custom compilers:

```bash
python3 tools/content/benchmark_content_pipeline.py \
  --content-executable cmake-build-debug/cna-content \
  --custom-content-executable cmake-build-debug/cna_custom_content_compiler_example \
  --workers 4 \
  --iterations 7 \
  --json-output /tmp/cna-content-benchmark.json
```

The harness creates fixtures under the OS temporary root and removes them in `finally`, unless
`--keep-workspace` is requested. It generates assets there, not CMake builds. Fixture copying,
seed builds, output hashing and cleanup are outside the timer. Serial/parallel order alternates
each iteration; samples are subprocess wall times, including startup and all compiler work.

JSON retains every timing, medians and a floor-index p95 (with seven samples this is the maximum).
The "cold" scenario deletes output/manifest, not OS page caches. The JSON records the checkout
revision and basic host/count metadata, but no binary/input hashes, complete compiler/cache
configuration, dependency versions or digest values. Preserve those separately for comparisons.

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

The run reports all serial/parallel tree checks passed. Its synthetic custom nodes
perform little parsing/encoding; the smaller observed speedup is consistent with a larger share of
process/graph/staging/hashing/filesystem cost, but these aggregate timers do not profile that share.
The result supported opt-in parallelism for these mixed directories. The current CLI still
defaults to one worker; independently authored components need their own reentrancy qualification.

The harness and retained per-run JSON provide a method for comparing matched current
configurations after scheduler changes.

## XNAP-93: XNB output and texture-policy costs

These are end-to-end `cna-content` wall-clock times — process start, source decode, processing,
serialization and atomic publication all included — so they answer "what does adding this cost a
build?" rather than "how fast is this loop?". Each figure is the best of three runs on an
otherwise-idle container, Debug build, single asset, single worker.

**Recorded environment**: Linux 6.18, GCC 13, `cmake-build-unit` (Debug, `CNA_PLATFORM=SDL3`,
`CNA_GRAPHICS_RENDERER=STUB`), no sanitizer. The tables entered in `682a73760`/`5d69a3400`
are a separate campaign from the scheduler median table; do not mix their estimators.

### One 1024x1024 RGBA source (1 megapixel, 4 MiB of level-zero pixels)

| Build | Time | Output |
|---|---|---|
| `Color`, no mips | 0.33 s | 4 194 491 bytes |
| `Color`, full mip chain (11 levels) | 0.46 s | 5 592 631 bytes |
| `Dxt1`, no mips | 1.08 s | 524 475 bytes |
| `Dxt5`, full mip chain | 1.71 s | 1 398 355 bytes |
| `Color`, LZ4 compressed | 0.53 s | 3 552 636 bytes |

What the differences say:

- The mip-enabled end-to-end sample was **0.13 s slower** for this one source. It includes the
  changed processing/output work; it is not an isolated resampler throughput or universal cost
  per megapixel.
- The Dxt1 sample added about **0.75 s**, corresponding to roughly 1.4 megapixels/s if attributed
  entirely to the added work. This is an inference from process totals, not a standalone encoder
  comparison. The encoder's bounding-box/farthest-pair seeds and configurable `refinementRounds`
  are source contracts; the historical quality work is XNAP-53.
- LZ4's sample added about **0.20 s** and produced about 85% of the uncompressed XNB size.
  Attributing that delta to four MiB gives roughly 20 MiB/s, not independently measured codec
  throughput. The recorded flat 32×32 sample fell from 4283 to 478 bytes; these two sources do not
  establish best/worst cases for arbitrary content.
- Serialization and PNG decoding were not timed independently, so this table does not establish
  their individual shares or show that serialization is negligible.

### One `.spritefont`, 190 glyphs at 32 px

| Build | Time | Output |
|---|---|---|
| `.spritefont` + TTF -> SpriteFont `.xnb` | 0.044 s | 1 058 241 bytes (512x512 atlas) |

This one font's recorded total was under a tenth of a second. Cost for many fonts, larger glyph
sets, sizes or different faces was not measured; it is not a bound on a project's font budget.

### Large models

The campaign used `tools/xnb/generate_large_model.py` to author increasing grids/materials/
hierarchies rather than relying on the small correctness fixtures. The current generator remains
available; keep its revision and environment when regenerating bytes. This is synthetic scaling
evidence, not a representative production character or a claim that licensed external reference
assets cannot be used. It stresses what a Model build spends time on:
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
cmake-build-debug/cna-content build /tmp/large/src -o /tmp/large/out-xnb --format xnb --quiet
cmake-build-debug/cna-content build /tmp/large/src -o /tmp/large/out-cnb --format cnb --quiet
```

What the three sizes say:

- The medium/large XNB samples show about **91,000/97,000 vertices per second**. Similar
  throughput across these two synthetic sizes is consistent with approximately linear behavior
  over that range; it cannot prove asymptotic complexity or rule out every resource-interning bug.
  `LargeModelScalingTest` separately checks generator counts, a small/medium XNB output/source-size
  band (0.5–1.5× the small ratio), and a nonempty medium CNB build. It does not time the route,
  build the largest scale or validate semantic equivalence of both outputs.
- XNB/CNB best-of-three totals were close (0.66/0.62 s and 10.01/10.04 s). The routes share
  importer/processor work, but these samples alone do not quantify noise, prove equal cost or
  isolate serialization from glTF/canonical-model construction.
- The record gives **33 MiB** for the 2,659,384-byte medium source and **346 MiB** for the
  42,509,644-byte large one: about **13.0× and 8.5×**, not 6× source bytes. The RSS estimator/raw
  logs are not retained here. These sample multipliers are not a general budget; overlapping
  document/canonical/output storage and multiple concurrently built assets also matter.
- The recorded warm rebuild was **1.72 s**, about one sixth of that full build. It includes
  process/coordinator/manifest/fingerprint work; no phase profile here assigns all of it to source
  hashing.

The size-band and loose timing-ratio tests described here check narrower regressions; they do not
assert that a current build matches these absolute times.

### What this means for a real build

`--workers` schedules build nodes/assets rather than subdividing one node's work. A single
expensive model or texture can limit batch speedup; codec libraries and other subprocess work are
not guaranteed to consume exactly one CPU core. The incremental manifest avoids reprocessing
unchanged nodes when their dependencies/configuration remain valid. Measure the project's actual
mix before predicting whether block compression, hashing, graph work or a large model dominates.

## XNAPP-301: the XNA routes at size

Measured in a Debug `cmake-build-debug`, one asset per run unless the row says otherwise, the
fastest of three. `modules/content-pipeline/tests/CNA/Content/Pipeline/XnaRouteScalingTests.cpp`
has four synthetic XNB route tests, which execute when selected, not merely when the project
builds. If the small best-of-three run takes at least 20 ms, the larger must take strictly less
than eight times as long for four times the input; below that floor the test only reports a ratio.
This loose two-point timing gate is not an asymptotic-complexity proof.

| Route | Small | Large | Small | Large | Ratio for 4x the input |
|---|---|---|---:|---:|---:|
| `.tga` → `Texture2D` | 512x512 | 1024x1024 | 384 ms | 1258 ms | **3.3x** |
| `.wav` → `SoundEffect` | 5 s | 20 s | 144 ms | 410 ms | **2.8x** |
| `.xml` → `List<string>` | 25 000 | 100 000 elements | 355 ms | 1289 ms | **3.6x** |
| whole tree | 400 | 1600 assets | 613 ms | 3887 ms | **6.3x** |

The three single-asset ratios were below four in that run. Fixed overhead, caching and host noise
can all affect such ratios; this is consistent with the intended scaling without proving linear
or better-than-linear complexity.

The fourth row is the one worth writing down. The coordinator's cost per asset is not flat — it
rises from about 1.6 ms at 400 assets to 2.5 ms at 1600 and 4.1 ms at 3200:

| Assets | 100 | 200 | 400 | 800 | 1600 | 3200 |
|---|---:|---:|---:|---:|---:|---:|
| build | 210 ms | 358 ms | 709 ms | 1623 ms | 4282 ms | 13 215 ms |
| per asset | 2.10 ms | 1.79 ms | 1.77 ms | 2.03 ms | 2.68 ms | 4.13 ms |
| exponent over the previous row | — | 0.77 | 0.99 | 1.19 | 1.40 | 1.63 |

The all-skip rebuild also grew similarly (542 ms at 400, 3769 ms at 1600), so route
execution was not required for the observed overhead. Splitting assets into groups of fifty did
not remove it in that experiment (1.69/2.60 ms per asset versus 1.59/2.48 flat). Three workers
improved one 1600-asset sample (5068 → 2332 ms), showing that some work could overlap; this does
not rule out serial coordination, directory effects at other scales or mixed costs. The exact
root cause was not established by these samples. The current test gates only its 400/1600 pair,
not every row in the six-size table or all future corpus sizes.
