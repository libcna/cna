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
