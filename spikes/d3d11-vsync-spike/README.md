# `d3d11-vsync-spike` — is choppy animation in the VM CNA's, or the VM's?

`vsync_probe.cpp` is a Direct3D 11 frame-pacing probe **with no CNA in it**: one Win32 window, one
swap chain, a clear and a `Present` per frame, timed. It exists to settle one question by
measurement instead of by assertion.

## The question

After WINCLOSE-0011, `cna_demo_2d` on the `win10_local` VirtualBox guest draws its sprites, but the
animation is visibly choppy. Instrumenting the demo showed:

| `cna_demo_2d` | vsync on (default) | vsync off |
|---|---|---|
| draws per second | 10–15 | ~270 |
| cost of `Draw` itself | ~94 ms (blocked) | **0.07 ms** |

So CNA's own frame is cheap and the time goes to waiting for vblank — which points at the guest,
but only indirectly. A CNA-side pacing defect would look the same from inside the demo.

## Build and run (on the Windows machine, from an MSVC developer environment)

```
cl /nologo /EHsc /O2 /std:c++17 vsync_probe.cpp /link d3d11.lib dxgi.lib user32.lib
vsync_probe.exe 180
```

Run it on the **interactive desktop** (on the VM: through `tools/platform/win32_run_interactive.ps1`),
not from an SSH session, which is session 0 with no display.

## Result on `win10_local`, 2026-09-16

VBoxSVGA with 3D acceleration, headless session, guest reporting 1536×845 @ 60 Hz:

```
flip-vsync     frames=180 wall_s=21.59 fps=8.3   interval_ms mean=120.59 p50=101.15 p95=247.75 max=319.37
flip-novsync   frames=180 wall_s=1.21  fps=148.9 interval_ms mean=6.73   p50=0.17   p95=0.73   max=377.42
blt-vsync      frames=180 wall_s=26.04 fps=6.9   interval_ms mean=144.54 p50=152.67 p95=238.09 max=460.45
```

**A plain D3D11 program with vsync runs at 7–8 fps here, under both swap models.** CNA's demo does
slightly better than that. The choppiness belongs to the virtual display's vblank in this VM, not to
CNA, and switching swap model does not help.

## What it does not settle

Frame pacing on a **physical** Windows GPU and display. That belongs to the Windows phase of CNA
testing on real hardware: run this probe first, so there is a CNA-free baseline to compare
`cna_demo_2d` against, then the demo.

The executable and object files are gitignored; only the source and this record are kept.
