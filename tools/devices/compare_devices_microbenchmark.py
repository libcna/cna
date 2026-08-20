#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
#
# Task PERF2-001: compares a fresh cna_devices_microbenchmark run against a committed baseline
# (docs/devices-benchmark-baseline.jsonl), flagging any benchmark whose p95 latency regressed by
# more than --threshold (default 50%) relative to the baseline's own p95. Exit code 0 if nothing
# regressed (or a benchmark is new, with no baseline entry to compare against -- reported, not
# treated as a failure), 1 if at least one regression was found.
#
# Usage:
#   ./cna_devices_microbenchmark > /tmp/run.jsonl
#   python3 tools/devices/compare_devices_microbenchmark.py docs/devices-benchmark-baseline.jsonl /tmp/run.jsonl
#
# This is the local comparison mechanism this task's own "CI stores benchmark baselines and
# flags material regressions" acceptance criterion asks for -- actual GitHub Actions wiring to
# run this automatically on every push is a separate, not-yet-done follow-up (see this task's own
# plans/plan_devices.md resolution note).
import argparse
import json
import sys


def load_jsonl(path):
    results = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            entry = json.loads(line)
            results[entry["name"]] = entry
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", help="Path to the committed baseline JSON-Lines file")
    parser.add_argument("current", help="Path to a fresh cna_devices_microbenchmark run's JSON-Lines output")
    parser.add_argument(
        "--threshold",
        type=float,
        default=0.50,
        help="Fractional p95 regression threshold before flagging (default 0.50 = 50%% slower)",
    )
    parser.add_argument(
        "--min-absolute-us",
        type=float,
        default=1.0,
        help="Minimum absolute p95 increase (microseconds) required before flagging, alongside "
             "--threshold (default 1.0us) -- sub-microsecond benchmarks are dominated by "
             "measurement/scheduling noise on this scale; a purely relative threshold flags that "
             "noise as a false-positive regression on very fast operations. Verified empirically: "
             "two consecutive real runs of the same unmodified binary produced a spurious 53%% "
             "relative delta on a ~0.4us benchmark before this floor was added.",
    )
    args = parser.parse_args()

    baseline = load_jsonl(args.baseline)
    current = load_jsonl(args.current)

    regressions = []
    new_benchmarks = []

    for name, current_entry in current.items():
        if name not in baseline:
            new_benchmarks.append(name)
            continue
        baseline_p95 = baseline[name]["p95_us"]
        current_p95 = current_entry["p95_us"]
        if baseline_p95 <= 0:
            continue
        absolute_change = current_p95 - baseline_p95
        relative_change = absolute_change / baseline_p95
        if relative_change > args.threshold and absolute_change > args.min_absolute_us:
            regressions.append((name, baseline_p95, current_p95, relative_change))

    missing = [name for name in baseline if name not in current]

    if new_benchmarks:
        print(f"New benchmarks with no baseline entry (not flagged as regressions): {', '.join(new_benchmarks)}")
    if missing:
        print(f"Baseline benchmarks missing from this run: {', '.join(missing)}")

    if not regressions:
        print(f"No material p95 regressions (threshold {args.threshold:.0%}).")
        return 0

    print(f"Material p95 regressions found (threshold {args.threshold:.0%}):")
    for name, baseline_p95, current_p95, relative_change in regressions:
        print(f"  {name}: baseline p95={baseline_p95:.4f}us, current p95={current_p95:.4f}us "
              f"({relative_change:+.1%})")
    return 1


if __name__ == "__main__":
    sys.exit(main())
