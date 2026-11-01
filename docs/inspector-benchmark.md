# CNA Inspector performance record

Historical INSP-0001 measurements (2026-09-17), source-audited on 2026-11-01.
These retained figures are not a current-HEAD performance qualification.

## Method

The benchmarks are:

- `modules/inspector/benchmarks/InspectorCompiledOutBenchmark.cpp`, which has no Inspector include
  or link dependency and is built even when Inspector support is compiled out;
- `modules/inspector/benchmarks/InspectorBenchmark.cpp`, which measures the same application loop
  before starting an agent, while an agent has no client, and then measures real authenticated
  loopback requests.

Both use a `Release` build. The live overview contains one metric and the full 240-frame history.
The profiling response contains 512 resolved zone events. The preview source allocates/fills a synthetic 1 MiB payload with byte 0x5A and labels it image/png;
it is not a decodable PNG. That case measures callback allocation plus protocol/transport handling,
not image encoding/decoding, renderer synchronization or GPU readback.

```sh
cmake -S . -B /tmp/cna-inspector-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCNA_BUILD_INSPECTOR=ON \
  -DCNA_BUILD_BENCHMARKS=ON \
  -DCNA_BUILD_TESTS=OFF \
  -DCNA_DIAGNOSTICS=FULL \
  -DCNA_GRAPHICS_RENDERER=HEADLESS \
  -DCNA_PLATFORM=HEADLESS \
  -DCNA_AUDIO_PLATFORM=NULL \
  -DCNA_ENABLE_VIDEO=OFF \
  -DCNA_ENABLE_DRACO=OFF \
  -DCNA_ENABLE_NET=OFF \
  -DCNA_BUILD_EXAMPLES=OFF
cmake --build /tmp/cna-inspector-release --target \
  cna_inspector_compiled_out_benchmark cna_inspector_benchmark --parallel 8

/tmp/cna-inspector-release/modules/inspector/cna_inspector_compiled_out_benchmark
/tmp/cna-inspector-release/modules/inspector/cna_inspector_benchmark
```

The live benchmark averages 500 snapshot and 200 event round trips within each process, and takes
the median of five preview requests separated by 55 ms. Its agent uses benchmark-specific limits
(1,024 requests/s, 50 ms preview cooldown), not production defaults. Run each executable seven times
on an otherwise idle machine and report the median of those per-process results. The idle-agent
case also prints the provider-call count, which must remain zero.

## Results

Measured on an AMD Ryzen 7 PRO 7840U (8 cores/16 threads), GCC 14.2.0, Linux x86-64, boost enabled.
The system was not booted with an isolated benchmark core, so small loop differences are noise.

| Case | Seven-run median | Interpretation |
|---|---:|---|
| Inspector compiled out control | 1.144 ns/iteration | No Inspector dependency or code in the executable. |
| Support linked, agent not started | 1.165 ns/iteration | +0.021 ns/iteration (1.8%), within observed run noise. |
| Agent enabled, no client/browser | 1.161 ns/iteration | +0.016 ns/iteration (1.4%), within noise; **0 provider calls**. |
| Live basic statistics view | 71.289 us/request | Authenticated binary response containing one metric and 240 frames; agent thread only. |
| Active profiling view | 121.416 us/request | Authenticated response containing 512 resolved zone events; agent thread only. |
| Explicit synthetic preview payload | 1.386 ms/request | Median callback/payload/transport time for a synthetic 1 MiB response, not a decoded image or GPU result. |

The first three cases are the historical application-loop comparison. Raw run distributions and
confidence intervals are not retained here, so these medians alone do not prove statistical
equivalence or a universal idle-overhead bound. The linked and idle-agent medians bracket the
compiled-out control across individual runs and perform no provider polling. There is no continuous
serialization, snapshotting, object enumeration, or preview work in either state.

The two live-view measurements are request latency, not render-thread stalls: the agent calls the
provider and serializes on its own network thread. The browser bridge's JSON conversion is in the
separate process and is not included here.

The overview/event fixture is a canned provider, not a live engine registry; its copy/serialization
cost does not establish the cost of a game with many resources/producers. Source/benchmark smoke
checks on 2026-11-01 do not reproduce the original seven-run campaign.

The preview number intentionally excludes renderer/GPU time. No CNA renderer currently installs a
preview provider. A future provider must issue asynchronous readback and return `Pending`; the
polling response may return bytes only after the renderer reports them available. That GPU path
must be measured separately on each renderer rather than inferred from this transport result.
