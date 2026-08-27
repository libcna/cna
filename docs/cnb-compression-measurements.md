# CNB chunk compression: what the measurements actually say

`plans/plan_cnb.md` `CNBF-105`. Measured 2026-08-27 on the development machine
(AMD, NVMe, Debian 13, gcc, `-O2`). Every number here is reproducible with the recipe in §5.

The task was written as "measure before choosing, and the honest answer may be *not worth it*".
The measurement says something more specific than either yes or no, so the numbers come first and
the decision follows from them.

---

## 1. The premise that turned out to be wrong

`docs/cnb-format.md` §8 originally justified having no codec like this:

> most of what a game ships is already compressed (PNG, Ogg)

That is true of the **source** files. It is not true of what CNB v1 actually stores. Every schema 1
payload is raw:

| schema | payload | stored as |
|---|---|---|
| `Texture2D`/`Cube`/`3D` | `TEXD` | uncompressed `Rgba8` |
| `SoundEffect` | `AUDD` | uncompressed `Pcm16` |
| `Model` | `MVTX`/`MIDX` | raw interleaved `f32` and raw indices |

A PNG becomes raw RGBA at compile time; an Ogg becomes raw PCM. So the premise inverted the
situation: CNB is *exactly* the place where the data is uncompressed.

---

## 2. Compression ratio, real content

zstd level 3 unless stated. Ratio is compressed ÷ raw, so **lower is better**.

| payload | raw | zstd-3 | ratio | zstd-19 | zlib-6 |
|---|---|---|---|---|---|
| `Texture2D` `Rgba8`, 1024×1024 photograph | 4 194 304 | 2 144 512 | **51.1 %** | 41.4 % | 49.7 % |
| `Texture2D` `Rgba8`, 512×512 normal map | 1 048 576 | 603 166 | **57.5 %** | 42.3 % | 59.8 % |
| `SoundEffect` `Pcm16`, 2 s 44.1 kHz mono | 176 400 | 47 320 | **26.8 %** | 18.8 % | 55.0 % |
| `Model` vertex bytes, 32 768 verts × 32 B | 1 048 576 | 158 171 | **15.1 %** | 13.0 % | 42.1 % |
| `Model` index bytes, 65 536 triangles | 786 432 | 223 683 | **28.4 %** | 22.2 % | 27.0 % |

zstd beats zlib everywhere except index data, and by a wide margin on geometry (15 % vs 42 %) and
audio (27 % vs 55 %). zstd-19 buys another 5–10 points for 50–200× the compression time, which is
a build-time cost only — but see §4 before assuming it is free.

## 3. Decompression cost

Median of five runs, with the observed spread, because this machine's throughput varies with load.

| payload | ratio | decompress | throughput | spread |
|---|---|---|---|---|
| `Texture2D` `Rgba8` photo, 4 MiB | 51.1 % | 4.29 ms | **932 MB/s** | 696–1085 |
| `SoundEffect` `Pcm16`, 172 KiB | 26.8 % | 0.08 ms | **2007 MB/s** | 1627–2223 |
| `Model` vertex bytes, 1 MiB | 15.1 % | 0.79 ms | **1264 MB/s** | 1166–1517 |

## 4. The number that decides it

Compression saves load time only when reading the bytes it removes costs more than decompressing
the bytes it keeps. With ratio `r` and decompression throughput `D`, a compressed chunk is faster
exactly when the device reads slower than `D × (1 − r)`:

| payload | break-even device read speed |
|---|---|
| `Texture2D` `Rgba8` photo | **456 MB/s** |
| `Model` vertex bytes | **1073 MB/s** |
| `SoundEffect` `Pcm16` | **1469 MB/s** |

Measured storage on this machine: **2.5 GB/s cold, 4.4 GB/s from page cache.**

So on this machine — and on any NVMe — **compression makes every load slower.** On a SATA SSD
(~500 MB/s) geometry and audio win and textures are roughly break-even. On a hard disk (~120 MB/s),
over a network, or on a phone's slower flash, everything wins substantially.

## 5. Reproducing it

```bash
# Payload extraction (real .cnb chunks, a real photograph, real 16-bit audio)
#   the probe source is committed at spikes/cnb-compression-spike/measure.cpp
ccache g++ -O2 -std=c++23 -o build-probe/cnbf105-measure \
    spikes/cnb-compression-spike/measure.cpp -lzstd -lz
./build-probe/cnbf105-measure build-probe/cnbf105-*.payload
```

The probe times each codec in a repeat loop until at least 50 ms has elapsed, so a fast codec on a
small buffer is not recorded as "0 ms", and it verifies every round trip byte for byte.

---

## 6. A methodology finding worth keeping

The first run of this measurement used the repository's own image fixture
(`tests/assets/media/thumbnails/large_400x300.png`) as the "photograph". It reported
**0.6 %** — a 160× saving.

That fixture is a synthetic placeholder: 405 unique colours across 120 000 pixels. Two other image
fixtures are a **single colour**. Measured on those, compression looks like the most valuable
feature in the project; measured on a real photograph it is 51 %, i.e. **eighty times less
impressive**.

The tell was visible before the content was checked: zstd-3 produced a *larger* output than
zstd-1, and zstd-9 larger still. Non-monotonic ratios across levels mean the input is degenerate,
not that the codec is strange. Any future performance work in this repository should check what a
fixture actually contains before quoting a number from it — the assets here are placeholders for
*plumbing* tests, and they are not representative of anything.

---

## 7. Conclusion

The saving is real and large: roughly **half** off textures, **three quarters** off audio,
**six sevenths** off vertex data. Distribution size, install size and download time improve by
those amounts, always, on every platform.

Load time is the opposite: it improves only below the break-even read speeds in §4, and on modern
desktop storage it gets *worse*.

Those two facts point at one design, and it is the design the container already allows: **a codec
that exists, is off by default, and is chosen per chunk.** `compression` is a per-chunk field
rather than a per-file one (`docs/cnb-format.md` §4), which this measurement retroactively
justifies — a shipped build can compress a 4 MB texture atlas and leave a 200-byte header alone,
and a platform with slow storage can make a different choice from one with fast storage.

One consequence has to be stated plainly: **a compressed chunk cannot be read by an already-shipped
CNA**, because every reader before the codec landed rejects a non-zero `compression`. Compression
is therefore opt-in in a second sense — turning it on raises the minimum runtime version for that
file. It is not a transparent build-time switch.
