# Texture source corpus (generated)

Written by `tools/xna-pipeline-oracle/texture/make_texture_fixtures.py`.
Every fixture is 2x2 and carries the same four pixels -- opaque red, opaque
green, opaque blue, half-transparent white -- so a format's own limits show up
in what the importer answers rather than in the source. Nothing here was
downloaded and nothing is third-party content: the bytes of every format but
JPEG are written by the script itself, and the JPEG by the host FFmpeg from
the same raw pixels.

| File | Bytes |
|---|---:|
| `empty.png` | 0 |
| `garbage.tga` | 25 |
| `probe.bmp` | 70 |
| `probe.dds` | 144 |
| `probe.dib` | 56 |
| `probe.hdr` | 143 |
| `probe.jpg` | 366 |
| `probe.pfm` | 60 |
| `probe.png` | 76 |
| `probe.ppm` | 23 |
| `probe.tga` | 34 |
| `probe.xyz` | 76 |
| `probe_3x2.png` | 85 |
| `probe_4x4.png` | 119 |
| `probe_flat.hdr` | 89 |
| `truncated.dds` | 60 |
| `truncated.png` | 20 |
