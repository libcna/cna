# CNBF-105 compression probe

This standalone exploratory program compares byte buffers using zstd levels 1/3/9/19 and zlib-6.
It does not parse/extract CNB files. The [historical measurement record](../../docs/cnb-compression-measurements.md)
contains the 2026-08-27 table and its limitations; the original payloads/raw samples are not retained
here.

The listed zstd-3 compressed/raw ratios were 51.1% for one photograph, 26.8% for audio and 15.1%
for vertex bytes: approximately 49%, 73% and 85% **saved**, respectively. These are sample payload
ratios, not universal file/installed-size or load-time wins. The serial I/O break-even model is
conditional on actual throughput and omitted costs. The CNB writer's codec remains opt-in.

A synthetic fixture initially produced a much smaller ratio. Inspect representative source
content before quoting performance results; non-monotonic sizes across levels alone do not prove
an input is degenerate.

```bash
mkdir -p build-probe
ccache g++ -O2 -std=c++23 -o build-probe/cnbf105-measure \
    spikes/cnb-compression-spike/measure.cpp -lzstd -lz
./build-probe/cnbf105-measure path/to/photo.payload path/to/audio.payload
```

Use the shared cache and retain input hashes/codec versions. Timers report adaptive mean iteration
cost (≥50 ms or the repetition ceiling); they do not compute an outer median. Compression includes
scratch allocation, decompression reuses a buffer. Missing/empty input and codec/round-trip errors
can still leave exit zero, so inspect diagnostics and use production regression tests for
correctness. The probe is not an arbitrary-input validator or an end-to-end loader benchmark.
