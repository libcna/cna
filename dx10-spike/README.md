# `dx10-spike` — `DX10-0` existence-gate spike findings (2026-07-21)

Run under DXVK 2.6.0, `DISPLAY=:0` (the REAL desktop — see "Present() crashes under Xvfb" below for
why), `WAYLAND_DISPLAY` unset. Wine prefix `~/.wine-cna-dx10` (a copy of the already-Vulkan/DXVK-
proven `~/.wine-cna-d3d11`, with a real fix applied — see "Broken d3d10 symlinks" below).

## What DX10 concretely means, vs. `DX1`..`DX8`

DirectX 10 (2006) removed the fixed-function pipeline **entirely** — there is no `SetVertexShader
(rawFvfValue)`-style trick like D3D8's, no `D3DFVF_*` bitmask, no fixed-function texture-stage
states at all. **Every single draw call requires a real, compiled HLSL vertex+pixel shader pair.**
This is a fundamentally different shape of backend than `DX1`..`DX8` (all CPU-transform-and-submit)
— closer in spirit to this project's own `D3D9`/`D3D11` backends (which already compile real HLSL
via `D3DCompile` for their custom-effect paths), just targeting D3D10's own shader model ceiling
(`vs_4_0`/`ps_4_0`, not D3D9's `vs_2_0`/`ps_2_0` or D3D11's `vs_5_0`/`ps_5_0`).

## Real environment findings (found before any backend code was written)

### mingw + DXVK support is real, but split across two DLLs unlike D3D8/D3D9

- `/usr/x86_64-w64-mingw32/include/d3d10*.h` and a REAL `libd3d10.a` import library both exist
  (unlike D3D8, which needed DXVK's own `.dll.a` linked directly — D3D10 links normally via
  `-ld3d10 -ldxgi`).
- **DXVK 2.6.0 ships NO `d3d10.dll` at all** — only `/usr/lib/dxvk/wine64/d3d10core.dll.so`
  (`d3d10core` is the internal "core" API D3D11 itself is built on; DXVK does not provide a direct
  public `D3D10CreateDevice`/`ID3D10Device` implementation). The real, public `d3d10.dll`/
  `d3d10_1.dll` must come from **Wine's own builtin** (Wine 10.0 ships real `d3d10.dll`/`d3d10_1.dll`
  that thinly forward to `d3d10core.dll`) — override only `d3d10core` to DXVK, leave `d3d10`/
  `d3d10_1` as Wine's own builtin.

### Broken `d3d10`/`d3d10_1` symlinks inherited from the shared `~/.wine-cna-d3d11` prefix

Copying `~/.wine-cna-d3d11` (to reuse its already-proven Vulkan/DXVK state, same precedent as
`dx8-spike`'s own prefix setup) inherited **dangling symlinks**: `d3d10.dll`/`d3d10_1.dll` were
overridden to `native` and pointed at `/usr/lib/dxvk/wine64/d3d10.dll.so`/`d3d10_1.dll.so` — files
that do not exist in the currently-installed DXVK 2.6.0 package (leftover from an older DXVK version
that apparently did ship those directly, before the DXVK project consolidated D3D10 support into
`d3d10core` only). Fixed: removed the `d3d10`/`d3d10_1` DLL overrides entirely and restored Wine's
own real builtin files (`d3d10.dll.old`/`d3d10_1.dll.old` backups, or a fresh `wineboot`-provided
copy) in `system32`, keeping `d3d10core` as the only D3D10-related override (native, DXVK).

### `Present()` crashes under Xvfb `:99` — a DIFFERENT, further DXVK bug than DX8's

`D3D10CreateDeviceAndSwapChain`/`CreateRenderTargetView`/`CreateDepthStencilView`/`ClearRenderTargetView`
all work fine under Xvfb — the FIRST `IDXGISwapChain::Present()` call hangs the whole process
(`wine: Unhandled division by zero ... starting debugger`, then blocks waiting for `winedbg`). This
is the SAME `readMonitorEdidFromKey`/colorimetry divide-by-zero DXVK bug found in `dx8-spike`'s own
investigation, but triggered here by a BARE `IDXGISwapChain::Present()` call directly (no SDL, no
CNA code at all) rather than SDL3's own internal probe — confirming the bug is inside DXVK's dxgi
`Present()` path itself, not SDL-specific. Traced further this time via `WINEDEBUG=+reg`: the crash
happens on `NtQueryValueKey(..., L"EDID", ..., Length=12)` — a standard two-phase "probe the
required size with a small buffer" registry read — where DXVK's own calling code does not correctly
handle the expected "buffer too small" result and treats it as an unrecoverable failure. **Confirmed
this is NOT a missing/malformed EDID problem**: constructed and injected a fully valid, correctly-
checksummed 128-byte EDID into the exact registry key DXVK queries (verified via `WINEDEBUG=+reg`
that it opens the right key) — the crash still happened identically. `DXVK_FILTER_DEVICE_NAME=llvmpipe`
(which fixed a DIFFERENT, GPU-specific double-`Present()` bug for DX8) does NOT help here either,
since this crash is GPU-independent (happens identically on both RADV and llvmpipe).

**The fix, matching an ALREADY-ESTABLISHED precedent for this project's own D3D11/D3D12 Wine+DXVK
testing** (`feedback_d3d11_wine_test_run_recipe`, a separate cnaaudit-repo session): use
`DISPLAY=:0` (the real desktop), not Xvfb `:99`. Confirmed empirically (3 consecutive runs, both the
pure-device spike and the full shader+draw spike): the SAME "readMonitorEdidFromKey" warning still
appears (a real monitor's EDID isn't automatically wired through Wine's virtual-desktop registry
path either), but `Present()` no longer crashes — DXVK's own "using blank" colorimetry fallback
completes normally and the frame presents. Why Xvfb specifically triggers a FATAL divide-by-zero
while the real display's fallback path completes safely was not root-caused further (would require
patching/rebuilding DXVK to know for certain) — empirically reproducible and 100% consistent either
way, which is enough to build against.

## Spike results

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX10-0a` | `D3D10CreateDeviceAndSwapChain` via Wine's own `d3d10.dll` + DXVK's `d3d10core.dll` + DXVK's `dxgi.dll` | Whether real device+swapchain creation works through this DLL combination | ✅ Works |
| `DX10-0b` | `GetBuffer`+`CreateRenderTargetView`+`CreateDepthStencilView`+`ClearRenderTargetView` | Whether basic render-target/depth-stencil setup works | ✅ Works |
| `DX10-0c` | `D3DCompile(..., "vs_4_0", ...)`/`D3DCompile(..., "ps_4_0", ...)` — a real HLSL vertex+pixel shader pair | Whether D3D10's own shader-model ceiling compiles at all (D3D10 is SM4.0 only, no SM5.0) | ✅ Works |
| `DX10-0d` | `CreateVertexShader`/`CreatePixelShader`/`CreateInputLayout`/`CreateBuffer` (vertex buffer) + `Draw(6,0)` — a real textured, shaded full-screen quad | Whether the full real-shader draw pipeline works end-to-end | ✅ Works |
| `DX10-0e` | `CreateTexture2D`+`CreateShaderResourceView`+`CreateSamplerState`, sampled in the pixel shader | Whether real one-texture sampling works | ✅ Works — pixel-perfect readback |
| `DX10-0f` | `CreateTexture2D(D3D10_USAGE_STAGING)`+`CopyResource`+`Texture2D::Map(D3D10_MAP_READ)` | Whether D3D10's own CPU-readback mechanism (no `GetRenderTargetData`/`CopyRects` — a genuinely different API shape again) works | ✅ Works — `pixel(16,32)=(255,0,0,255)` (red), `pixel(48,32)=(0,255,0,255)` (green), exact match |

**Every readback matched its prediction exactly, on the first fully-working run** (after fixing the
broken symlinks and switching to `DISPLAY=:0`) — no spike-authoring bugs.

## Practical conclusions for the real backend

- Link normally (`-ld3d10 -ldxgi -ld3dcompiler`, all real mingw import libraries — no DXVK `.dll.a`
  linking hack needed, unlike DX8).
- Wine prefix setup: `d3d10core`+`dxgi`+`d3d9`+`d3d10_1`+`d3d10core`... — override ONLY `d3d10core`
  (and `dxgi`, needed for the real swap chain) to DXVK/native; leave `d3d10`/`d3d10_1` as Wine's own
  real builtin files (verify they are NOT dangling symlinks if the prefix was copied from an older
  one).
- **Every draw needs a real compiled shader pair** — there is no fixed-function fallback at all.
  `BasicEffect`/`SpriteBatch` equivalents must be real HLSL (`vs_4_0`/`ps_4_0`), following this
  project's own `D3D11GraphicsBackend`/`D3D9GraphicsBackend` precedent for `D3DCompile`-based
  runtime shader compilation, not `DX1`..`DX8`'s CPU-transform-and-submit model.
- **Readback is `CreateTexture2D(D3D10_USAGE_STAGING)` + `CopyResource` + `Map`/`Unmap`** — yet
  another distinct mechanism from `DX1`..`DX7`'s `Blt`, `DX8`'s `CreateImageSurface`+`CopyRects`, or
  `D3D9`'s `GetRenderTargetData`.
- **Run all CTest binaries against `DISPLAY=:0`, not Xvfb `:99`** — the ONLY way found so far to
  avoid DXVK's own dxgi `Present()`-path divide-by-zero bug. This briefly flashes small windows on
  the real desktop during test runs (same tradeoff already accepted for D3D11/D3D12 testing in this
  sandbox).

## Files

- `dx10_spike1_device.cpp` — device/swapchain/render-target-view/Present bring-up only.
- `dx10_spike2_shader_draw.cpp` — the full spike above (shader compile, input layout, vertex
  buffer, texture, sampler, draw, staging readback).
