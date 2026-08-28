# CNB chunk compression measurement

`plans/plan_cnb.md` `CNBF-105`. The probe that decided whether CNB should have a codec at all.

## What it proved

Running it on real content (a photograph, real 16-bit audio, real interleaved `f32` geometry)
showed that zstd level 3 takes **51 %** off a `Texture2D` `Rgba8` payload, **27 %** off
`SoundEffect` `Pcm16` and **15 %** off `Model` vertex bytes — and that decompression runs at
0.9–2.0 GB/s, so compression only *saves load time* on storage slower than 456–1469 MB/s. The
development machine reads at 2.5 GB/s, where it makes loading slower.

That split — size always wins, time only sometimes — is what produced the design: the codec exists,
is off by default, and is chosen per chunk.

Full numbers, break-even arithmetic and the reproduction recipe are in
`docs/cnb-compression-measurements.md`.

## It also caught a bad fixture

The first run used the repository's own image fixture as the "photograph" and reported 0.6 % — a
160× saving. That fixture is a synthetic placeholder with 405 unique colours; two other image
fixtures are a single colour. The tell was that zstd-3 produced a *larger* output than zstd-1:
non-monotonic ratios across levels mean the input is degenerate, not that the codec is strange.

## Building it

```bash
ccache g++ -O2 -std=c++23 -o build-probe/cnbf105-measure \
    spikes/cnb-compression-spike/measure.cpp -lzstd -lz
./build-probe/cnbf105-measure <payload files...>
```

It links only zstd and zlib — no CNA — because it measures codecs against byte buffers, and the
payloads are extracted from real `.cnb` files beforehand. Binaries build into `build-probe/` and
are gitignored; this source is not, because the finding is only as good as the method that
produced it.
