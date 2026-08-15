# Platform abstraction performance baseline

This is the PLAT-7 baseline and the PLAT-120 post-migration comparison. It measures the same public
XNA fixed scene at the last revision before `plan_platform.md` was introduced (`e19e52fb6`) and at
the completed platform migration (`4d99e6d19`, plus the benchmark-only instrumentation being
measured). The instrumentation patch is identical in both trees; it does not change renderer or
platform implementation code.

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
as independent. The separately measured baseline-A/baseline-B spread establishes each row's noise
floor, with 0.5% as the minimum floor.

The checked-in machine-readable reports contain both submission and end-to-end p50/p95 results:

- [`platform-performance-software.json`](platform-performance-software.json)
- [`platform-performance-opengles3.json`](platform-performance-opengles3.json)

The comparison driver is `tools/platform/compare_renderer_benchmark.py`. It exits non-zero only when
the lower bound of a current slowdown's 95% confidence interval exceeds the independently measured
noise floor. This is deliberately a regression test, not a claim that noisy point estimates are
exact.

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

All eight submission p50/p95 comparisons pass as well. No current slowdown is statistically
distinguishable from the baseline's own run-to-run variation. The SOFTWARE stable p95 improvement
is the only interval that excludes zero; no regression interval does. This supports the contract's
"typically under 0.5%, probably below a consistently measurable difference" expectation without
pretending that every noisy point estimate is itself below 0.5%.

OPENGLES3 is the GPU-API renderer in this two-renderer gate. This host has no exposed hardware GPU,
so it ran a real OpenGL ES 3.2 context on Mesa 25.0.7 llvmpipe under Xvfb, explicitly constrained to
one llvmpipe worker. It validates the GPU renderer path and platform/context dispatch, but these
absolute times must not be presented as hardware throughput. SOFTWARE is CNA's deterministic CPU
rasterizer and needs no display server.

## Re-running

Build `cna_bench_graphics_renderer` from two Release configurations selecting the same renderer,
then run:

```sh
taskset -c 0 python3 tools/platform/compare_renderer_benchmark.py \
  --baseline /path/to/baseline/cna_bench_graphics_renderer \
  --current /path/to/current/cna_bench_graphics_renderer \
  --renderer SOFTWARE --baseline-revision <revision> --current-revision <revision> \
  --runs 12 --frames 2400 --output /tmp/platform-performance.json
```

For OPENGLES3, run the same command under one Xvfb session with `SDL_VIDEODRIVER=x11`,
`LIBGL_ALWAYS_SOFTWARE=1` and `LP_NUM_THREADS=1` when reproducing this llvmpipe result. A hardware
run should instead name the actual driver/device in `--device` and establish its own baseline noise
floor; it must not be compared numerically with these llvmpipe times.
