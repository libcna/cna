# CNB chunk compression: what the measurements actually say

`CNBF-105`. Historical 2026-08-27 measurements, recorded in `0149f1910`, on the development
machine (AMD, NVMe, Debian 13, GCC, `-O2`). The probe source is retained, but the exact payload
files, hashes, codec versions and raw repeated-run output are not retained with this page. §5 can
measure new inputs; it cannot by itself reproduce every historical number. Current source and
regression verification is stated at the end.

The result supported an optional chunk codec, disabled by default. Its size ratios and simplified
I/O model inform that choice; they do not establish every asset's ratio or end-to-end load time.

---

## 1. The premise that turned out to be wrong

`docs/cnb-format.md` §8 originally justified having no codec like this:

> most of what a game ships is already compressed (PNG, Ogg)

That premise did not describe the measured decoded payloads. These were the relevant schema-1
representations in the original campaign:

| schema | payload | stored as |
|---|---|---|
| `Texture2D`/`Cube`/`3D` | `TEXD` | uncompressed `Rgba8` |
| `SoundEffect` | `AUDD` | uncompressed `Pcm16` |
| `Model` | `MVTX`/`MIDX` | raw interleaved `f32` and raw indices |

Those texture/audio routes decoded PNG to RGBA and Ogg to PCM before writing CNB. This is not a
claim that every CNB chunk/schema is raw float or RGBA today: standalone Texture2D can also carry
BC1/BC2/BC3 variants with an RGBA fallback, and other asset families have different representations.
Container compression is separate from a texture's GPU block-compression format
([current format](cnb-format.md)).

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

In this table zlib-6 is smaller for the photograph **and** index data. zstd-3 is smaller for the
normal map, vertex data and audio. zstd-19 improves the listed ratios by about 2–15 percentage
points over zstd-3; compression-time repetitions are not retained here, so the old 50–200× cost
claim is not independently auditable from this table. These five samples do not rank codecs for
all content.

## 3. Decompression cost

The historical record describes five runs summarized by a median and spread. The committed probe
itself prints one adaptively repeated **mean per iteration** per invocation; it does not compute
that outer median or retain five-run samples. It divides bytes by 1,048,576, so its printed "MB/s"
and the derived thresholds below are **MiB/s**.

| payload | ratio | decompress | throughput | spread |
|---|---|---|---|---|
| `Texture2D` `Rgba8` photo, 4 MiB | 51.1 % | 4.29 ms | **932 MiB/s** | 696–1085 |
| `SoundEffect` `Pcm16`, 172 KiB | 26.8 % | 0.08 ms | **2007 MiB/s** | 1627–2223 |
| `Model` vertex bytes, 1 MiB | 15.1 % | 0.79 ms | **1264 MiB/s** | 1166–1517 |

## 4. The number that decides it

For a serial model with raw size `S`, compressed/raw ratio `r`, read throughput `B` and
decompression throughput `D` measured in raw bytes/s, compare `S/B` with `r*S/B + S/D`.
If all other costs are equal, compression wins when `B < D*(1-r)`:

This is a model, not a measured end-to-end CNB threshold. It excludes allocation/copying,
CRC over stored bytes, per-chunk overhead, CPU contention, I/O overlap, downstream decoding/upload,
and filesystem/transport compression. Current parsing also keeps compressed file bytes and an
expanded buffer, so the memory tradeoff is separate.

| payload | break-even device read speed |
|---|---|
| `Texture2D` `Rgba8` photo | **456 MiB/s** |
| `Model` vertex bytes | **1073 MiB/s** |
| `SoundEffect` `Pcm16` | **1469 MiB/s** |

The original record reports storage at **2.5 GB/s cold, 4.4 GB/s from page cache**, without a
retained storage-test command/cache-control log. At read throughput above the listed thresholds,
this model predicts a loss for these samples; below them it predicts a gain. Neither the name
"NVMe" nor "SATA", "phone" or "network" supplies the actual workload throughput/ratio, and these
numbers do not prove that every load on such a device gets faster or slower.

## 5. Reproducing it

```bash
# First extract and hash the intended raw payloads; the probe does not extract .cnb chunks.
mkdir -p build-probe
# Probe source: spikes/cnb-compression-spike/measure.cpp
ccache g++ -O2 -std=c++23 -o build-probe/cnbf105-measure \
    spikes/cnb-compression-spike/measure.cpp -lzstd -lz
./build-probe/cnbf105-measure build-probe/cnbf105-photo.payload build-probe/cnbf105-audio.payload
```

The standalone Linux/GNU-style recipe needs zstd/zlib development headers and libraries, not CNA.
It measures zstd levels 1/3/9/19 and zlib-6. Each timer grows its repetition count until ≥50 ms or
the 1,048,576-iteration ceiling, then reports elapsed/iterations. Compression timing includes a
scratch allocation per iteration; decompression reuses an output buffer.

It compares the final restored bytes, but timed codec return values are ignored; initial codec
errors/round-trip mismatches print diagnostics without making main return failure, and
empty/missing inputs are skipped. Exit zero alone is therefore not valid codec or input coverage.
Record the actual payload hashes, versions, output and outer repetitions; use the production
regression suite for correctness rather than treating this exploratory probe as a validator.

---

## 6. A methodology finding worth keeping

The first run of this measurement used the repository's own image fixture
(`tests/assets/media/thumbnails/large_400x300.png`) as the "photograph". It reported
**0.6 %** — a 160× saving.

The historical inspection reported 405 unique colours across 120,000 pixels in that placeholder
and two other single-color fixtures. A 0.6% compressed/raw ratio is about 167:1; the photograph's
51.1% is about 2:1. The compressed/raw ratios differ by about 85×, not the percentages of bytes
saved. This is why the actual source corpus matters.

The recorded non-monotonic level sizes prompted inspection; that behavior alone does not prove
a fixture is synthetic or invalid. The content inspection is the evidence for this particular
fixture. Keep plumbing fixtures for correctness, and inspect/hash a representative corpus before
using it for performance or distribution-size claims; do not generalize these placeholders to
every repository asset.

---

## 7. Conclusion

The listed samples save roughly half the raw photograph bytes, three quarters of the audio and
six sevenths of the vertex data. Those are payload savings: complete-container alignment/metadata,
filesystem allocation and an already-compressing package/transport can change distribution,
installed-size or download-time benefits. CPU architecture is not a different Zstandard byte
format ([codec specification](https://github.com/meta/zstd/blob/dev/doc/zstd_compression_format.md)),
but does affect performance and available decoder builds.

Current CNA has optional Zstandard (codec 2), **off by default for writing**. Capability depends
on the build; ask `IsCnbCompressionSupported(CnbCompression::Zstd)`. The writer accepts a codec/
level policy and selects storage per chunk, retaining the raw form when the compressed candidate
is not smaller; `CMET`/`XREF` stay uncompressed. It is not a renderer/platform-wide automatic
optimization. Current readers validate stored CRCs and declared/aggregate limits, then eagerly
decompress compressed chunks; a codec-less reader refuses the file rather than exposing just its
uncompressed metadata.

One consequence has to be stated plainly: **a compressed chunk cannot be read by an older CNA
without its codec**, because readers before the codec landed reject a non-zero `compression`. Compression
is therefore opt-in in a second sense — turning it on raises the minimum runtime version for that
file. It is not a transparent build-time switch.

## 8. Current verification

The 2026-11-01 audit checked both codec probe and production writer/parser/codec paths. The content
target builds; all 23 selected compression, known-answer and hardware/table CRC tests pass privately
with HEADLESS, with Zstandard present and the host x86 hardware path selected. This is fresh
correctness evidence, not a rerun of the historical payload/storage/timing campaign or ARM/other-host
qualification.
