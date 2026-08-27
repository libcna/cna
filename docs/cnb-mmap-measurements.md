# Memory-mapped `.cnb` access: what the benchmark said, and why the answer was "no"

`plans/plan_cnb.md` `CNBF-108`. Measured 2026-08-27 on the development machine (AMD, NVMe,
Debian 13, gcc `-O2`). Reproduction recipe in §5.

The task was: *"The `alignment` field and 16-byte geometry alignment exist for it. But a
`VertexBuffer` usually still has to be uploaded CPU→GPU, so mmap is not automatically zero-copy to
the GPU. Benchmark before implementing."*

The benchmark did not merely fail to justify mmap. **It found that the thing mmap would optimise
was not the bottleneck**, and pointed at something twelve times larger.

---

## 1. What a `.cnb` load actually spends its time on

Measured on a 32 MiB `.cnb` (eight 4 MiB `Rgba8` texture payloads), median of five runs:

| step | time | throughput |
|---|---|---|
| read the whole file (what `ParseFile` does) | 7.02 ms | 4560 MB/s |
| `mmap` + touch every page | 2.31 ms | 13 850 MB/s |
| `mmap` alone, never touched | 0.005 ms | *not a load — nothing was read* |
| **verify CRC-32C, table implementation** | **62.51 ms** | **512 MB/s** |
| verify CRC-32C, SSE4.2 | 3.28 ms | 9756 MB/s |

**Verifying the file cost nine times more than reading it.** Every chunk's CRC is checked at parse
(`docs/cnb-format.md` §4), and CNB's implementation was a byte-at-a-time table — correct, portable,
and about 512 MB/s.

## 2. The comparison that decided it

| change | saves |
|---|---|
| memory-mapping instead of reading | **4.71 ms** |
| folding the same CRC in hardware | **59.23 ms** |

The CRC win is **12.6×** the mmap win, on the same file, for a change that touches one function
instead of the container's whole ownership model.

The `mmap`-alone row is why this needed measuring rather than reasoning. Mapping a 32 MiB file
takes 5 µs, which looks like the entire load has disappeared. It has not: nothing has been read.
The bytes arrive on the first touch, and CNB touches all of them immediately, because it verifies
them.

## 3. The decision

**`CNBF-108` is not implemented, and should not be.** Not "deferred": the measurement says it is
the wrong optimisation, and that conclusion is stable in a way a schedule is not.

What was implemented instead is hardware CRC-32C — SSE4.2 on x86, the ARMv8 CRC32 extension on
aarch64 — detected **at runtime**, so one binary still runs on a machine without the instruction.
The polynomial, the seed and every produced value are unchanged; all four golden vectors are still
byte-identical, and a test proves the hardware path agrees with the table at every input length
modulo 8 and at every resume point.

End to end, through `cna_tool_cnb_info` on the same 32 MiB file, 20 runs each, including process
startup:

| | per run |
|---|---|
| before (table CRC) | **127.3 ms** |
| after (hardware CRC) | **19.8 ms** |

**6.4× faster**, and the remaining time is now dominated by actually reading the file — which is
where mmap's 4.7 ms would apply, and would still be the smaller half of a much smaller number.

## 4. What would change the answer

Recorded so a future reader can tell whether this conclusion still holds rather than re-deriving it:

* **A target without a CRC instruction.** On such a platform verification is back at ~512 MB/s and
  dominates again — but the fix there is a slicing-by-8 table fold, not mmap.
* **Loading only part of a file.** mmap's real advantage is not reading what you never touch. CNB
  verifies every chunk at parse, so today nothing is untouched. A future mode that verified lazily,
  per chunk, would change that — and would be trading the format's corruption guarantee for it,
  which is a different decision from this one.
* **Sharing one asset between processes.** `MAP_SHARED` costs one copy of the pages instead of N.
  Nothing in CNA does this today.

## 5. Reproducing it

```bash
ccache g++ -O2 -std=c++23 -o build-probe/cnbf108-measure \
    spikes/cnb-mmap-spike/measure.cpp
./build-probe/cnbf108-measure <a real .cnb>
```

Numbers are **warm-cache**: dropping the page cache needs privileges the probe does not assume, and
it says so rather than pretending. That biases *against* the conclusion, not for it — a cold cache
would make the read slower and the CRC's share of the total smaller, so the CRC's dominance here is
the conservative case.

## 6. A benchmark bug worth remembering

The first run reported CRC-32C at **0.000 ms for 32 MiB** — roughly 1.6 × 10⁹ MB/s. The compiler
had removed the call, because its result was discarded.

Had that gone unnoticed, the conclusion would have been the exact opposite of the truth:
"verification is free, so the read is everything, so implement mmap." The fix is a one-line
`asm volatile("" : : "r,m"(value) : "memory")` barrier, and the lesson is that an implausibly good
benchmark number is a bug report about the benchmark.
