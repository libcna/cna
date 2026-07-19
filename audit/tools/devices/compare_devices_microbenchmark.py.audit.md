# Audit: tools/devices/compare_devices_microbenchmark.py

## Metadata
- Source file: `tools/devices/compare_devices_microbenchmark.py` (96 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-devices` shard
- File type: Python script (benchmark regression comparator)
- XNA/FNA relevance: none — performance tooling for `Microsoft::Devices` sensors
- Main related tests: N/A (manually-invoked developer tool; see its own comment re: CI wiring)

## Purpose
Compares a fresh `cna_devices_microbenchmark` JSON-Lines run against a committed baseline, flagging
any benchmark whose p95 latency regressed by more than `--threshold` (default 50%) *and* more than
`--min-absolute-us` (default 1.0µs) relative to the baseline.

## Executive Verdict
Correct, and specifically hardened against a real, empirically-discovered false-positive class: the
`--min-absolute-us` floor's own comment (lines 46-55) states plainly that "two consecutive real runs
of the same unmodified binary produced a spurious 53% relative delta on a ~0.4us benchmark before
this floor was added" — a concrete, falsifiable account of why a purely-relative threshold alone
would be unreliable for very fast operations dominated by measurement/scheduling noise.

## Checklist Results
- `main()`'s regression condition (`relative_change > args.threshold and absolute_change >
  args.min_absolute_us`, line 74) correctly requires *both* conditions — not silently OR'd, which
  would defeat the absolute floor's whole purpose.
- New benchmarks (present in the current run but absent from baseline) are reported but explicitly
  not treated as regressions (line 66) — reasonable, since there's nothing to compare against.
- Baseline benchmarks missing from the current run are also reported (line 82) — a genuinely useful
  signal (a benchmark that silently stopped running would otherwise go unnoticed), correctly kept
  separate from the regression list.
- Exit code (0 = no regressions, 1 = at least one) matches the standard CI convention this tool's
  own comment describes as the intended (if not-yet-automated) usage.

## Detailed Findings
None.

## Cross-File Observations
Consumes the JSON-Lines format `devices_microbenchmark.cpp` (audited alongside this file) emits —
the two files' formats are confirmed consistent (`{"name":...,"p95_us":...}` matches
`compare_devices_microbenchmark.py`'s own `entry["p95_us"]`/`entry["name"]` field access).

## Missing or Weak Tests
No test was located for this comparison script itself (e.g. a synthetic baseline/current pair with
a known expected regression/no-regression outcome) — a regression in the comparison logic (e.g. an
inverted condition) would currently only be caught by a human noticing an unexpected CI result.

## Positive Findings
The `--min-absolute-us` floor's justification is a strong example of a tool's own design decision
being backed by a specific, reproducible empirical observation rather than a defensive guess.

## Final Assessment
No findings.
