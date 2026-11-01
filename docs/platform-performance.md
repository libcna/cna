# Historical platform abstraction performance comparison

This is the retained PLAT-7/PLAT-120 report, not a benchmark of today's HEAD. Its recorded revisions
are `e19e52fb6` (baseline) and `4d99e6d19` (post-migration plus benchmark instrumentation).
Neither id resolves in this checkout's current Git object database on 2026-11-01. The retained
report was committed in `77f8afff9`; its historical description says both trees received identical
benchmark-only instrumentation. Exact old-tree reproduction needs those original trees/patches;
the JSON summaries alone do not establish their identity.

## Method

`cna_bench_graphics_renderer` renders a 480×270 frame containing 500 moving 2×2 RGBA8 sprites. A
stable-tint phase exercises the normal cached path and a churn phase changes every tint every frame.
Fixed timestep and vertical retrace are disabled. Native builds use `steady_clock` and emit every
submission and end-to-end frame sample as JSON.

Each result below uses a Release build pinned to CPU 0, 120 warm-up frames, 2,400 measured frames per
phase and 12 comparison pairs. A pair brackets one current run with two independent baseline runs,
alternating order between pairs. Consequently each renderer result contains 24 baseline runs and 12
current runs: 57,600 baseline samples and 28,800 current samples per phase. Confidence intervals use
the per-run p50/p95 log ratios, not the statistically invalid shortcut of treating adjacent frames
as independent. The effective noise floor is the larger absolute endpoint of the baseline-A/B
95% interval, with 0.5% as the minimum. The table's pooled-frame percentiles and paired-run delta
estimates are different statistics; dividing the displayed percentile columns need not reproduce
the reported mean delta.

The checked-in machine-readable reports contain both submission and end-to-end p50/p95 results:

- [`platform-performance-software.json`](platform-performance-software.json)
- [`platform-performance-opengles3.json`](platform-performance-opengles3.json)

The comparison driver is `tools/platform/compare_renderer_benchmark.py`. It exits non-zero only when
the lower bound of a comparison's 95% interval exceeds that noise floor (or when execution/input
validation fails). It does not test equivalence within ±0.5%. The checked-in JSON holds aggregate
statistics and metadata, not the original per-frame samples or run pairing, so the original
confidence intervals cannot be independently recomputed from those files alone.

## Results

The table shows the end-to-end path, which includes host event polling, update, rendering and
presentation. Delta is current versus the pre-migration baseline; negative is faster.

| Renderer / phase | Statistic | Baseline → current | Delta, 95% CI | Noise floor | Result |
|---|---:|---:|---:|---:|---:|
| SOFTWARE / stable | p50 | 0.3414 → 0.3413 ms | −0.05% [−0.76%, +0.65%] | 2.41% | PASS |
| SOFTWARE / stable | p95 | 0.3766 → 0.3723 ms | −0.88% [−1.70%, −0.05%] | 3.20% | PASS |
| SOFTWARE / churn | p50 | 0.3443 → 0.3444 ms | +0.43% [−0.56%, +1.43%] | 0.95% | PASS |
| SOFTWARE / churn | p95 | 0.3784 → 0.3801 ms | −0.11% [−1.25%, +1.04%] | 1.91% | PASS |
| OPENGLES3 / stable | p50 | 0.2690 → 0.2706 ms | +0.78% [−1.46%, +3.07%] | 4.17% | PASS |
| OPENGLES3 / stable | p95 | 0.3413 → 0.3347 ms | −1.69% [−7.08%, +4.02%] | 8.13% | PASS |
| OPENGLES3 / churn | p50 | 0.2715 → 0.2736 ms | +1.29% [−2.03%, +4.72%] | 3.34% | PASS |
| OPENGLES3 / churn | p95 | 0.3433 → 0.3390 ms | −2.87% [−7.12%, +1.56%] | 6.61% | PASS |

All eight submission p50/p95 comparisons pass as well. Every comparison's lower interval endpoint
is below its effective regression floor. SOFTWARE stable end-to-end p95 is the only listed
interval excluding zero, in the faster direction. These historical runs found no regression under
that rule; their upper bounds do not establish a universal overhead below 0.5%, or the performance
of later platform/rendering changes.

OPENGLES3 was the GPU-API renderer in this historical two-renderer gate. The report records an
OpenGL ES 3.2 context on Mesa 25.0.7 llvmpipe under Xvfb, explicitly constrained to
one llvmpipe worker. It validates the GPU renderer path and platform/context dispatch, but these
absolute times must not be presented as hardware throughput or as today's host configuration.
SOFTWARE is CNA's CPU rasterizer; a display-free application also needs compatible platform/audio/
window policy. Choosing a CPU renderer alone is not a universal no-display guarantee.

## Re-running

Build `cna_bench_graphics_renderer` from two matched Release configurations selecting the same
renderer, platform/audio policy and scene, with no extra CNA_BENCH_MESH_DRAWS. Record compiler,
flags, binary/patch identity and device. The driver's --renderer/--device/--*-revision values are
labels supplied by the caller; it does not verify them against the executable. Its current
defaults are eight pairs and 1,200 frames, so request the historical counts explicitly:

```sh
tools/platform/run_gpu_tests_private.sh --exec \
  env -u CNA_GRAPHICS_RENDERER -u CNA_BENCH_MESH_DRAWS \
  taskset -c <allowed-cpu> python3 tools/platform/compare_renderer_benchmark.py \
  --baseline /path/to/baseline/cna_bench_graphics_renderer \
  --current /path/to/current/cna_bench_graphics_renderer \
  --renderer SOFTWARE --device '<actual device/driver>' \
  --baseline-revision <revision> --current-revision <revision> \
  --runs 12 --frames 2400 --output /tmp/platform-performance.json
```

For an OPENGLES3 software-driver comparison, add SDL_VIDEODRIVER=x11, LIBGL_ALWAYS_SOFTWARE=1
and LP_NUM_THREADS=1 to that private command's env arguments and change --renderer. The private
runner uses Weston/Xwayland rather than the original Xvfb setup, so this is a new controlled
comparison, not an exact reproduction of those absolute times. A hardware
run should instead name the actual driver/device in `--device` and establish its own baseline noise
floor; it must not be compared numerically with these llvmpipe times. Never run either binary on
the owner's live desktop. The 2026-11-01 audit checked the report arithmetic, source/driver and
metadata; it did not perform a new performance campaign or replace the historical JSON.
