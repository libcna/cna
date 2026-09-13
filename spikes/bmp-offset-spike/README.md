# `bmp-offset-spike`

**What it proves.** The vendored `stb_image` decodes a bitmap of more than eight bits per pixel
from the wrong place when the file puts bytes between its DIB header and its pixels.

A `.bmp` says where its pixels start in the file header's `bfOffBits`, and such a bitmap is allowed
to carry filler there -- a colour table written for 256-colour displays is the usual one, and
SAMPLE-141's `riemerstexture.bmp` has 864 bytes of exactly that. Feeding the same image to the
decoder twice, once with the filler and once without, gives two different images:

```
gap 0 decodes 256x256, taken as the truth
gap 4     decodes 256x256, 38984 of 262144 bytes differ from the truth
gap 16    decodes 256x256, 46079 of 262144 bytes differ from the truth
gap 64    decodes 256x256, 49933 of 262144 bytes differ from the truth
gap 256   decodes 256x256, 57613 of 262144 bytes differ from the truth
gap 768   decodes 256x256, 18762 of 262144 bytes differ from the truth
gap 864   decodes 256x256, 55886 of 262144 bytes differ from the truth
gap 1024  decodes 256x256, 58680 of 262144 bytes differ from the truth
```

Those counts are **byte for byte** what CNA produced for `riemerstexture.bmp` and for the same
synthetic gaps before the fix, which is what attributes the defect to the decoder rather than to
anything CNA does around it. The corruption is not a clean displacement -- a gap of one whole row
(768) damages *less* than a gap of four bytes -- which is why reading it off the output alone did
not name it.

The genuine XNA pipeline is correct here: its `riemerstexture.xnb` equals an independent,
spec-following decode of the file byte for byte.

**What CNA does about it.** `CNA::Internal::Graphics::WithoutBitmapPixelGap` removes the filler and
corrects `bfOffBits` before the bytes reach the decoder, for bitmaps of more than eight bits per
pixel only -- at eight or fewer the table *is* the palette, the decoder reads it correctly, and
removing it would destroy the image. `ImageLoaderTests.ABitmapWhosePixelsDoNotFollowItsHeaderDecodesToTheSameImage`
is the regression test; `plans/plan_xna_sample_xnb_sweep.md` `XNASWEEP-221` is the row.

**Running it.**

```bash
cd spikes/bmp-offset-spike
cc -O1 -I../../third_party/stb -o bmp_offset_spike bmp_offset_spike.c -lm
./bmp_offset_spike <any 24-bpp .bmp>
```
