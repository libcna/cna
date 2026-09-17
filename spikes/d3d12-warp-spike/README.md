# `d3d12-warp-spike` — does Direct3D 12 run on WARP in the validation VM, without CNA?

`warp_probe.cpp` is a Direct3D 12 probe **with no CNA in it**
(plans/plan_directx12_parity.md DX12-0003). It exists so that every later question of the form
"is this CNA, WARP, DXGI or the harness?" has a reference that is certainly not CNA.

## What it checks

1. every DXGI adapter, with its identifiers, memory, software flag and whether D3D12 accepts it;
2. the WARP adapter, obtained with `IDXGIFactory4::EnumWarpAdapter` — never by matching a name;
3. the objects `DirectX12Renderer` creates at construction (queue, fence, the four descriptor heap
   types, allocator, command list), and the feature facts a renderer asks;
4. that the device rasterizes: a 5×3 clear read back through `GetCopyableFootprints`;
5. optionally, a flip-model HWND swap chain with a clear and `Present` per frame and a `ResizeBuffers`
   every 50 frames;
6. what the D3D12 debug layer, the DXGI debug factory, DRED and `IDXGIDebug::ReportLiveObjects` say.

## Build and run (on Windows, MSVC developer environment)

```
cl /nologo /EHsc /O2 /std:c++17 /W4 warp_probe.cpp /link d3d12.lib dxgi.lib dxguid.lib user32.lib
warp_probe.exe --debug --dxgi-debug --dred --swapchain 3000
```

Run `--swapchain` on the **interactive desktop** (on the VM: `tools/platform/win32_run_interactive.ps1`).
From an SSH session (session 0) `CreateSwapChainForHwnd` answers `0x887A0022`, which is the session,
not the adapter. `--hardware` runs steps 3–5 on the first hardware adapter instead, as the control.

The debug layer needs the Windows optional feature `Tools.Graphics.DirectX~~~~0.0.1.0`
(`d3d12SDKLayers.dll`).

## Result on `win10_local`, 2026-09-17

Windows 10 Home 22H2 (19045.6466), `D3D12Core.dll` 10.0.19041.5794, MSVC 19.44.35229.

| | |
|---|---|
| adapter 0 | VirtualBox Graphics Adapter (WDDM), 0x80EE/0xBEEF — D3D12 **unsupported, `0x887A0004`** |
| adapter 1 / WARP | Microsoft Basic Render Driver, 0x1414/0x008C, software — D3D12 supported |
| WARP device | feature level 12_1, shader model 6.2, root signature 1.1, binding tier 3, MSAA 2/4/8 |
| clear + readback | every pixel `64,128,191,255`, row pitch 256 |
| 3 000 frames, 60 resizes, debug layer + DXGI debug + DRED | **0 debug messages, 0 live objects, PASS** |
| 300 frames under GPU-based validation | PASS |

## What it found about itself

The first version released its back buffers for `ResizeBuffers` straight after `Present`, having waited
only for the fence of the command list *before* that `Present`. The flip-model `Present` is queued work
that still referenced the buffers, and the debug layer answered with ID 921
(`OBJECT_DELETED_WHILE_STILL_IN_USE`) by raising exception `0x87D` — the process exited with code 2173
and printed nothing, whatever the info queue's break-on-severity settings (all off by default). The
vectored exception handler that drains the info queue at the raise is what named it; the fix is to
signal a fresh fence after `Present` and wait before releasing the buffers.

That is the invariant CNA's own resize has to keep, and the reason CNA's DirectX 12 test listener
installs the same kind of handler.

## `minlod_probe.cpp` — a very large `MinLOD` on WARP (DX12-0022)

XNA writes `SamplerState.MaxMipLevel` into Direct3D 9's unsigned `D3DSAMP_MAXMIPLEVEL`, so a negative value
selects the last stored level. CNA maps it to `MinLOD = FLOAT32_MAX`. `texture_filter_mip_contract_test`
L3/L9 then passed on DirectX11 (VirtualBox adapter) and failed on DirectX12 WARP, with identical sampler
code. This probe (Direct3D 11, no CNA) draws an 8×8 texture of four flat levels 1:1 with a point sampler
and reads which level `MinLOD` selected, on both driver types:

```
cl /nologo /EHsc /O2 /std:c++17 /W4 minlod_probe.cpp /link d3d11.lib d3dcompiler.lib dxgi.lib
minlod_probe.exe
```

| `MinLOD` | VirtualBox Graphics Adapter | WARP (Microsoft Basic Render Driver) |
|---|---|---|
| 0 / 1 / 3 | level 0 / 1 / 3 | level 0 / 1 / 3 |
| 14, 15, 99, 1e6 | level 3 (last) | level 3 (last) |
| 1e30, `FLT_MAX` | level 3 (last) | **level 0** |

WARP's rasterizer (shared by its Direct3D 11 and 12 front ends) loses the value once it no longer fits the
fixed-point LOD it clamps in; a representable value behaves. The spec states the clamp without such a
limit, so CNA keeps `FLOAT32_MAX` rather than adapting to WARP, and L3/L9 stay on the physical-GPU
checklist. The probe's first run read the clear colour at every value — its full-screen triangle was
culled — which is why it now clears to magenta and names "nothing drawn".

## `pixelcenter_probe.cpp` — point sampling under CNA's pixel-centre shift (DX12-0024)

`DescriptorCapacityContract` B1/C1 failed on DirectX12 WARP (every point-filter state misread its 2×2
identity texture) and passed on DirectX11. The first hypothesis was WARP precision: CNA moves clip-space
geometry 63/64 of a pixel's half width (`D3DCommon::ApplyXnaPixelCenter`), so pixel centres sample a
hair inside each texel, and Wrap or Mirror could turn a slightly negative coordinate into the far texel.
This probe (raw Direct3D 11, no CNA) draws a 2×2 texture one-to-one into a 2×2 target with that shift and
without it, with a point sampler, for every Clamp/Wrap/Mirror pair:

```
cl /nologo /EHsc /O2 /std:c++17 /W4 pixelcenter_probe.cpp /link d3d11.lib d3dcompiler.lib dxgi.lib
pixelcenter_probe.exe
```

Result on `win10_local`: **all 18 cases exact on both the VirtualBox adapter and WARP.** The hypothesis is
refuted — WARP samples exactly.

B1 was then made to print each wrong state. Every one was a *mixed* filter: `MinLinearMagPoint*` read
interpolated values (126/129) and `MinPointMagLinear*` read the exact texels — the opposite halves of what
the fixture expects for a magnification. The probe's second part repeats the 1:1 draw with the mixed
filters:

| filter, CNA shift | VirtualBox adapter | WARP |
|---|---|---|
| `MIN_MAG_MIP_POINT` | exact | exact |
| `MIN_MAG_MIP_LINEAR` | blended (126,129) | blended (125,129) |
| `MIN_LINEAR_MAG_POINT_MIP_LINEAR`, `MIN_LINEAR_MAG_MIP_POINT` | **exact** (magnification half) | **blended** (minification half) |
| `MIN_POINT_MAG_MIP_LINEAR`, `MIN_POINT_MAG_LINEAR_MIP_POINT` | **blended** (magnification half) | **exact** (minification half) |

A one-to-one footprint puts the LOD at zero, and which half of a mixed filter applies there is decided at
that boundary: the VirtualBox driver treats it as magnification (as the fixture, measured against XNA 4.0,
expects), WARP as minification. CNA's DirectX12 readings on WARP are exactly WARP's own. Not a CNA defect;
CNA is not adapted to it; B1/C1 are on the physical-GPU checklist.
