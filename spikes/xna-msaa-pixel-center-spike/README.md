# XNA 4.0 pixel centres on a multisampled render target

Answers, on the real XNA 4.0 runtime, the question the 2026-10-26 dependency audit raised as FNA3D
finding B2-04: does Direct3D 9's integer pixel-centre convention -- which
`spikes/xna-pixel-center-spike/` measured for single-sampled 3D draws -- also hold when the
destination is multisampled?

Every CNA renderer and FNA3D's three drivers apply their XNA pixel-centre correction (a 63/128
pixel displacement of 3D geometry) only to single-sampled destinations. The audit's reading was
that MSAA changes the sample pattern, not the origin.

## What it measures

An axis-aligned quad covering exactly an 8x8 target, `[0,8] x [0,8]` in window pixels, and one
covering `[0,4] x [0,8]` for an interior seam, drawn with `BasicEffect` at 0, 4 and 8 samples.
The red channel of every pixel is printed. With integer centres, pixel 0's samples sit around
x = 0, so the first column and row are half covered and the corner a quarter; with half-integer
centres every pixel of the full quad is covered.

## How to run

    DISPLAY=<private display> ./build-and-run.sh     # WINEPREFIX=~/.wine-cna-xna40

Inside CNA's checkout run it through `tools/platform/run_gpu_tests_private.sh --exec env -u
SDL_VIDEODRIVER ./build-and-run.sh`; never on the live desktop. The prefix and `csc.exe` are the
ones `spikes/xna-pixel-center-spike/` describes; Direct3D 9 is DXVK there.

## Result (2026-10-26, AMD Radeon 780M, RADV PHOENIX)

    FULL quad [0,8]x[0,8] requested=0 granted=0     every pixel 255
    FULL quad [0,8]x[0,8] requested=4 granted=4     corner 64, rest of row 0 and column 0 128, rest 255
    HALF quad [0,4]x[0,8] requested=4 granted=4     column 4 128 (corner 64), row 0 128, columns 5-7 0
    8 samples                                       the same as 4

**XNA keeps integer pixel centres on a multisampled target.** The first column and row lose half
their samples, the corner three quarters, and the interior seam at x = 4 is half covered, at 4 and
at 8 samples alike: the whole multisampled picture is displaced by half a pixel against the
half-integer convention, exactly as the single-sampled one is. Skipping the correction for a
multisampled destination therefore moves CNA's geometry away from XNA's by ~0.49 pixel whenever
MSAA is on.

This is DXVK's Direct3D 9, which applies the half-pixel offset to multisampled draws as well (its
0.5 - 1/128 viewport bias, the same 63/128 the CNA renderers use). Direct3D 9 places a pixel's
samples around its integer centre, so the derivation predicts the same picture on native D3D9; no
native D3D9 stack was available to confirm it.

`*.exe` and `probe-output.txt` are gitignored; the `.cs` source and this file are the record.
