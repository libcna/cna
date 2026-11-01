# Memory-mapped `.cnb` access: the CNBF-108 measurement and decision

`CNBF-108`. Historical 2026-08-27 record (`21ceccfe1`), development machine: AMD, NVMe,
Debian 13, GCC `-O2`. The exact 32 MiB input, complete commands and raw run logs are not retained
with this page. The committed exploratory probe can measure new inputs; it does not by itself
reproduce these numbers or validate a production CNB load.

The campaign selected hardware CRC-32C before changing document ownership to mmap. That was a
priority decision for the measured workload, not proof that mmap can never help. No mapped
`CnbDocument` storage path is currently implemented; `ParseFile` owns a vector of file bytes.

## 1. Historical component measurements

The original page describes a 32 MiB file with eight 4 MiB RGBA payloads:

| Step | Recorded time | Derived throughput |
|---|---:|---:|
| Read the whole file into an allocated buffer | 7.02 ms | 4560 MiB/s |
| `mmap` and touch one byte per 4096-byte interval | 2.31 ms | 13,850 MiB/s of mapped extent, not bytes actually read by the loop |
| `mmap`/unmap without touching data | 0.005 ms | No payload-read or validation measurement |
| CRC-32C, portable byte-table fold | 62.51 ms | 512 MiB/s |
| CRC-32C, SSE4.2 fold | 3.28 ms | 9756 MiB/s |

The old prose called this a median of five runs. The committed `TimeMs` actually returns the
**minimum of three** invocations and contains no outer five-run aggregation. Without raw logs the
table's claimed median cannot be reconstructed. Its printed "MB/s" uses bytes/1,048,576, hence
MiB/s here.

The read loop copies every byte; the mapped loop only accesses one byte per hardcoded 4096-byte
interval. It forces sparse page touches, not a complete scan/CRC/upload. CRC timings separately
scan the already-read vector as one buffer, while production parsing checks the header, table and
individual stored chunks. These component timings are not interchangeable full-parser workloads.

## 2. What supported the original priority

Subtracting the recorded component times gives:

| Candidate change | Estimated component saving |
|---|---:|
| Read → mapped sparse touches | 4.71 ms |
| Table → hardware CRC | 59.23 ms |

The second difference is about 12.6 times the first. This supported accelerating CRC first on that
host. It does not measure a mapped parser with the **new** fast CRC, include allocation in the read
timer, prove a permanent bottleneck, or establish a universal upper bound on mmap's benefit.

The historical end-to-end `cna_tool_cnb_info` record reports 20 runs including startup:

| Configuration | Recorded time per run |
|---|---:|
| Before, table CRC | 127.3 ms |
| After, hardware CRC | 19.8 ms |

The ratio is about 6.4×. The source now has the corresponding fast CRC path, but this audit did not
recreate those binaries/runs or profile the remaining 19.8 ms. The component differences alone
cannot allocate all end-to-end time to reading or checksum work.

## 3. Current parser and checksum contract

`CnbDocument::ParseFile` stats the file, enforces its file-size limit, reads it into owned bytes,
then parses it. Parsing validates header/table and every stored chunk CRC before eager
decompression and metadata/external-reference decoding. Compressed chunks retain both stored and
expanded buffers. Mmap would need an explicit ownership/lifetime/error model and would not
automatically eliminate decompression buffers or GPU uploads.

Production `CnbCrc32c.cpp` keeps the portable table and dispatches to hardware where available:
x86 checks SSE4.2 through CPUID; Linux aarch64 checks HWCAP_CRC32; Apple arm64 selects its CRC path;
other unsupported detection paths retain the table. This is distinct from the spike's unconditional
x86-64 hardware call. No architecture-wide assertion that every x86-64 CPU has SSE4.2 is needed
for the runtime dispatch contract.

The polynomial, initial/final inversion and continuation meaning remain unchanged. Current tests
cover four known answers, lengths 0–64 and selected larger awkward lengths, ten split points of a
4096-byte buffer and selected bit flips. They compare the selected dispatch path with the table;
a pass on x86 does not execute the ARM implementation or force every fallback configuration.

## 4. When to measure again

- A different CPU or a target without CRC instructions changes checksum cost. A portable
  slicing implementation is a candidate to benchmark, not a measured replacement specified here.
- Different asset sizes, many small loads, cold/cache-hot reads, pressure or repeated reuse can
  change the balance. Even eager CRC must read bytes, but it does not logically require copying
  them into an owning vector.
- A lazy/partial reader would need a separate validation contract: current parse success means all
  stored chunks were checked. Changing when validation happens changes when corruption is reported.
- Cross-process reuse would need measured memory and lifetime behavior. `MAP_SHARED` controls
  update visibility/writeback; `MAP_PRIVATE` is copy-on-write. Those flag names alone are not an
  N-fold memory-saving measurement ([Linux mmap manual](https://man7.org/linux/man-pages/man2/mmap.2.html)).

These are conditions for a future proposal, not a new implementation plan or a reversal of the
original task disposition.

## 5. Probe recipe and limits

```bash
mkdir -p build-probe
ccache g++ -O2 -std=c++23 -o build-probe/cnbf108-measure \
    spikes/cnb-mmap-spike/measure.cpp
./build-probe/cnbf108-measure path/to/recorded-input.cnb
```

This is a POSIX/GNU-style standalone probe, not a CMake target or CNB parser. On x86-64 it invokes
SSE4.2 without runtime detection, so use a suitable host. It does not robustly check open/read/mmap
failures, empty files or exact read counts; success alone does not validate the input or measurements.

The `DropCaches` function is defined but **never called**. Runs reuse page cache and vector data,
are ordered rather than interleaved, and make no cold-cache guarantee. Do not infer a one-sided
bias from that: cache state can change read, mapping and CRC comparisons. Record file hashes,
versions, affinity/load, repetition summaries and cache controls for a new benchmark; this audit
does not drop the machine's shared page cache.

## 6. Benchmark result consumption

The historical first attempt reported 0.000 ms CRC because the unused result could be optimized
away. The retained probe feeds CRC results to an `asm volatile` compiler barrier and compares its
table/hardware outputs. This protects that local operation from elimination, not the completeness
or statistical validity of the entire benchmark.

## 7. Current verification

The 2026-11-01 audit checked the probe, owned/eager parser, runtime dispatch and regression scope.
The content target builds and all 23 selected compression/known-answer/hardware-table CRC tests
pass privately with HEADLESS on the current x86 host. No new mmap implementation, original
32 MiB timing replay, cold-cache run, ARM or multi-process qualification is claimed.
