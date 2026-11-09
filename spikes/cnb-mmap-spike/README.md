# CNBF-108 read/mmap/CRC probe

The 2026-08-27 campaign prioritized hardware CRC-32C over a mapped document reader on one 32 MiB
workload. [`docs/cnb-format.md` §15.3](../../docs/cnb-format.md) retains the timings and
limits; it does not rule out mmap for every future workload. Current CNB storage remains owned
bytes with eager validation/decompression.

```bash
cmake --build cmake-build-multi --target cna_cnb_mmap_probe
sha256sum path/to/recorded-input.cnb
cmake-build-multi/spikes/cnb-probe-tests/cna_cnb_mmap_probe path/to/recorded-input.cnb
```

The native Linux test-enabled CMake target needs Python and a GNU/Clang compiler. It builds this
POSIX standalone source at `-O2`, without linking CNA or parsing CNB. It reports the minimum of
three component runs, not a median of five. `read + full CRC` and `mmap + full CRC` do the same
complete-input checksum work; read-only, untouched mapping and separate CRC timings are also
reported. Allocation is outside the read timer. Runs reuse uncontrolled page cache; nothing
drops the host's caches. Throughput is labelled MiB/s.

SSE4.2 is selected only after x86-64 CPUID detection; other hosts use the table. `--crc=table`
forces the portable comparison. Open/stat/read/mmap failures, empty/nonregular inputs, short reads
and whole-input checksum disagreement return nonzero; interrupted reads retry and failed output
writes fail the run. Use stable files:
the probe does not provide snapshot isolation against external file mutation. The registered
`CnbMeasurementProbeTests` checks known CRCs, table selection, mapped-tail corruption and I/O
failures with isolated linker wrappers. These checks do not qualify production CRC on every
architecture. Retain hashes, compiler/version, cache state and repeated-run output before drawing
a new performance conclusion; no original measurement is recreated by this source correction.
