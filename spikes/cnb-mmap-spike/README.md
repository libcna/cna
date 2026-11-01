# CNBF-108 read/mmap/CRC probe

The 2026-08-27 campaign prioritized hardware CRC-32C over a mapped document reader on one 32 MiB
workload. The [historical record](../../docs/cnb-mmap-measurements.md) retains the timings and
limits; it does not rule out mmap for every future workload. Current CNB storage remains owned
bytes with eager validation/decompression.

```bash
mkdir -p build-probe
ccache g++ -O2 -std=c++23 -o build-probe/cnbf108-measure \
    spikes/cnb-mmap-spike/measure.cpp
./build-probe/cnbf108-measure path/to/recorded-input.cnb
```

This POSIX/GNU-style probe does not link CNA or parse CNB. It reports the minimum of three runs,
not a median of five. Its mapped loop touches one byte per 4096-byte interval, while the read and
CRC loops process the whole extent; allocation is outside the read timer. `DropCaches` is defined
but never called. Throughput uses MiB/s although the program prints MB/s.

On x86-64 the spike invokes SSE4.2 unconditionally; production CRC dispatch has a separate runtime
feature check and portable fallback. Open/read/mmap errors and empty inputs are not handled
robustly. The compiler barrier consumes CRC results and the two CRC implementations are compared,
but exit zero does not validate the entire experiment. Retain input hashes, versions, cache state
and repeated-run output before drawing a new performance conclusion.
