#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Run and compare PLAT-7/PLAT-120 fixed-scene renderer benchmarks.

Each comparison pair brackets the current binary with two independent baseline runs. The
baseline A/B spread is the measured noise floor; the current result is compared with their
geometric mean, which limits bias from thermal/frequency drift. Confidence intervals operate on
per-run medians rather than treating thousands of adjacent frames as independent observations.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import platform
import statistics
import subprocess
import sys
from pathlib import Path


KEYS = (
    ("stable", "submission"),
    ("stable", "end_to_end"),
    ("churn", "submission"),
    ("churn", "end_to_end"),
)


def run_benchmark(executable: Path, frames: int) -> dict[tuple[str, str], list[float]]:
    environment = os.environ.copy()
    environment["CNA_BENCH_PHASE_FRAMES"] = str(frames)
    environment.setdefault("SDL_AUDIODRIVER", "dummy")
    completed = subprocess.run(
        [str(executable.resolve())],
        check=True,
        capture_output=True,
        text=True,
        env=environment,
    )
    result: dict[tuple[str, str], list[float]] = {}
    for line in completed.stdout.splitlines():
        if not line.startswith("{"):
            continue
        record = json.loads(line)
        if record.get("schema") != 1:
            continue
        key = (str(record["phase"]), str(record["metric"]))
        result[key] = [float(value) for value in record["samples_ms"]]

    missing = [key for key in KEYS if key not in result]
    if missing:
        raise RuntimeError(
            f"{executable} did not emit the expected benchmark records: {missing}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return result


def t_critical_95(degrees_of_freedom: int) -> float:
    # Two-sided Student-t 97.5th percentiles. Runs above 30 use the asymptotic value.
    values = {
        1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571,
        6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228,
        11: 2.201, 12: 2.179, 13: 2.160, 14: 2.145, 15: 2.131,
        16: 2.120, 17: 2.110, 18: 2.101, 19: 2.093, 20: 2.086,
        21: 2.080, 22: 2.074, 23: 2.069, 24: 2.064, 25: 2.060,
        26: 2.056, 27: 2.052, 28: 2.048, 29: 2.045, 30: 2.042,
    }
    return values.get(degrees_of_freedom, 1.960)


def log_mean_ci(values: list[float]) -> tuple[float, float, float]:
    mean = statistics.fmean(values)
    if len(values) == 1:
        return mean, mean, mean
    margin = (
        t_critical_95(len(values) - 1)
        * statistics.stdev(values)
        / math.sqrt(len(values))
    )
    return mean, mean - margin, mean + margin


def as_percent(log_ratio: float) -> float:
    return math.expm1(log_ratio) * 100.0


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summarize(
    baseline_a: list[dict[tuple[str, str], list[float]]],
    baseline_b: list[dict[tuple[str, str], list[float]]],
    current: list[dict[tuple[str, str], list[float]]],
    minimum_floor_percent: float,
) -> tuple[dict[str, object], bool]:
    metrics: dict[str, object] = {}
    all_passed = True
    for key in KEYS:
        comparisons: dict[str, object] = {}
        metric_passed = True
        for estimator_name, estimator in (
            ("p50", lambda values: statistics.median(values)),
            ("p95", lambda values: percentile(values, 0.95)),
        ):
            baseline_a_values = [estimator(run[key]) for run in baseline_a]
            baseline_b_values = [estimator(run[key]) for run in baseline_b]
            current_values = [estimator(run[key]) for run in current]

            noise_ratios = [
                math.log(after / before)
                for before, after in zip(baseline_a_values, baseline_b_values)
            ]
            current_ratios = [
                math.log(now / math.sqrt(before * after))
                for before, after, now in zip(
                    baseline_a_values, baseline_b_values, current_values
                )
            ]
            noise_mean, noise_low, noise_high = log_mean_ci(noise_ratios)
            delta_mean, delta_low, delta_high = log_mean_ci(current_ratios)
            noise_floor = max(
                minimum_floor_percent,
                abs(as_percent(noise_low)),
                abs(as_percent(noise_high)),
            )
            delta_ci = [as_percent(delta_low), as_percent(delta_high)]
            passed = delta_ci[0] <= noise_floor
            metric_passed &= passed
            comparisons[estimator_name] = {
                "baseline_noise_percent": {
                    "mean": as_percent(noise_mean),
                    "ci95": [as_percent(noise_low), as_percent(noise_high)],
                    "effective_floor": noise_floor,
                },
                "current_delta_percent": {
                    "mean": as_percent(delta_mean),
                    "ci95": delta_ci,
                },
                "passed": passed,
            }
        all_passed &= metric_passed

        baseline_frames = [
            value for run in baseline_a + baseline_b for value in run[key]
        ]
        current_frames = [value for run in current for value in run[key]]
        name = f"{key[0]}.{key[1]}"
        metrics[name] = {
            "baseline_frame_ms": {
                "p50": percentile(baseline_frames, 0.50),
                "p95": percentile(baseline_frames, 0.95),
            },
            "current_frame_ms": {
                "p50": percentile(current_frames, 0.50),
                "p95": percentile(current_frames, 0.95),
            },
            "comparisons": comparisons,
            "passed": metric_passed,
        }
    return metrics, all_passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--current", type=Path, required=True)
    parser.add_argument("--renderer", required=True)
    parser.add_argument("--device", default="not recorded")
    parser.add_argument("--baseline-revision", required=True)
    parser.add_argument("--current-revision", required=True)
    parser.add_argument("--runs", type=int, default=8)
    parser.add_argument("--frames", type=int, default=1200)
    parser.add_argument("--minimum-floor-percent", type=float, default=0.5)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.runs < 4:
        parser.error("--runs must be at least 4 for a meaningful run-level confidence interval")
    if args.frames < 100:
        parser.error("--frames must be at least 100")

    baseline_a = []
    baseline_b = []
    current = []
    for pair in range(args.runs):
        # Reverse the bracketing order on alternate pairs to avoid consistently favouring either
        # binary as the machine warms. The baseline still has one observation on both sides.
        if pair % 2 == 0:
            baseline_a.append(run_benchmark(args.baseline, args.frames))
            current.append(run_benchmark(args.current, args.frames))
            baseline_b.append(run_benchmark(args.baseline, args.frames))
        else:
            baseline_b.append(run_benchmark(args.baseline, args.frames))
            current.append(run_benchmark(args.current, args.frames))
            baseline_a.append(run_benchmark(args.baseline, args.frames))
        print(f"completed comparison pair {pair + 1}/{args.runs}", file=sys.stderr)

    metrics, passed = summarize(
        baseline_a, baseline_b, current, args.minimum_floor_percent
    )
    report = {
        "schema": 1,
        "renderer": args.renderer,
        "device": args.device,
        "baseline_revision": args.baseline_revision,
        "current_revision": args.current_revision,
        "method": {
            "scene": "480x270, 500 moving 2x2 RGBA8 sprites per frame",
            "warmup_frames": 120,
            "measured_frames_per_phase": args.frames,
            "comparison_pairs": args.runs,
            "baseline_runs": args.runs * 2,
            "current_runs": args.runs,
            "estimator": "per-run p50 and p95; paired log-ratio Student-t 95% CI",
            "minimum_regression_floor_percent": args.minimum_floor_percent,
        },
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "cpu_affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else [],
        },
        "metrics": metrics,
        "passed": passed,
    }

    print("metric.estimator               baseline/current ms   delta 95% CI       floor  result")
    for name, value in metrics.items():
        baseline = value["baseline_frame_ms"]
        now = value["current_frame_ms"]
        for estimator in ("p50", "p95"):
            comparison = value["comparisons"][estimator]
            delta = comparison["current_delta_percent"]
            noise = comparison["baseline_noise_percent"]
            verdict = "PASS" if comparison["passed"] else "REGRESSION"
            print(
                f"{name + '.' + estimator:31s} "
                f"{baseline[estimator]:7.4f}/{now[estimator]:7.4f} ms "
                f"[{delta['ci95'][0]:+6.2f},{delta['ci95'][1]:+6.2f}]% "
                f"{noise['effective_floor']:6.2f}%  {verdict}"
            )

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
