# CNBF-105 compression probe

This standalone exploratory program compares byte buffers using zstd levels 1/3/9/19 and zlib-6.
It does not parse/extract CNB files. [`docs/cnb-format.md` §8.1](../../docs/cnb-format.md) holds
the historical 2026-08-27 tables and their limitations; the original payloads/raw samples are not
retained.

The listed zstd-3 compressed/raw ratios were 51.1% for one photograph, 26.8% for audio and 15.1%
for vertex bytes: approximately 49%, 73% and 85% **saved**, respectively. These are sample payload
ratios, not universal file/installed-size or load-time wins. The serial I/O break-even model is
conditional on actual throughput and omitted costs. The CNB writer's codec remains opt-in.

A synthetic fixture initially produced a much smaller ratio. Inspect representative source
content before quoting performance results; non-monotonic sizes across levels alone do not prove
an input is degenerate.

```bash
cmake --build cmake-build-multi --target cna_cnb_compression_probe
sha256sum path/to/photo.payload path/to/audio.payload
cmake-build-multi/spikes/cnb-probe-tests/cna_cnb_compression_probe \
    path/to/photo.payload path/to/audio.payload
```

The native Linux test-enabled CMake target needs Python, enabled Zstandard support and zlib.
It builds this standalone source at `-O2`, without linking CNA. `--codec=zstd` or `--codec=zlib`
selects one codec family. Compiler/codec versions are printed; retain them with input hashes.
Timers report adaptive mean iteration
cost (≥50 ms or the repetition ceiling); they do not compute an outer median. Compression includes
scratch allocation and final-output retention, decompression reuses a buffer. Every codec return
and decoded length is checked, and the preflight/final round trips and final timed compressed
output must match. Missing/empty/nonregular inputs, read errors and codec/mismatch failures return
nonzero, even in a batch with successful inputs. Failed result-output writes also fail the run.
The registered `CnbMeasurementProbeTests` exercises
these failures with separate linker-wrapped executables and synthetic payloads. The probe does not
validate CNB structure or establish end-to-end load performance or statistical confidence.
