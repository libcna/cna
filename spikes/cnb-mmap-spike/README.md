# Would memory-mapping a `.cnb` make loading faster?

`plans/plan_cnb.md` `CNBF-108`. The probe that answered **no**, and found the real bottleneck
while doing it.

## What it proved

On a 32 MiB `.cnb`: reading the whole file takes 7.0 ms, memory-mapping and touching every page
takes 2.3 ms — and **verifying the file's CRC-32C took 62.5 ms**, nine times the I/O it protects,
because CNB's checksum was a byte-at-a-time table at ~512 MB/s.

So mmap could have saved 4.7 ms, while folding the same CRC in hardware saves 59.2 ms. The CRC win
is **12.6× the mmap win**, for a change to one function rather than to the container's whole
ownership model.

`CNBF-108` was therefore closed as *the wrong optimisation*, and hardware CRC-32C was implemented
instead. End to end that made loading the same file **6.4× faster** (127.3 ms → 19.8 ms).

Full numbers, the cases that would change the answer, and the reproduction recipe are in
`docs/cnb-mmap-measurements.md`.

## Two things it caught about benchmarking

**`mmap` alone measures nothing.** Mapping 32 MiB takes 5 µs, which looks like the whole load
vanished. Nothing has been read at that point; the bytes arrive on first touch. The probe touches
every page for exactly this reason.

**An implausibly good number is a bug in the benchmark.** The first run reported CRC-32C at
0.000 ms for 32 MiB, because the compiler removed a call whose result was discarded. Believing it
would have produced the opposite conclusion — "verification is free, so the read is everything, so
implement mmap".

## Building it

```bash
ccache g++ -O2 -std=c++23 -o build-probe/cnbf108-measure spikes/cnb-mmap-spike/measure.cpp
./build-probe/cnbf108-measure <a real .cnb>
```

Links nothing but libc: it compares `read`, `mmap` and two CRC implementations against the same
bytes. Binaries build into `build-probe/` and are gitignored; this source is not, because a finding
is only as good as the method behind it.
