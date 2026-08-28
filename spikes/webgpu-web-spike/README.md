# WebGPU web (Emscripten) existence-gate spike — WEBGPU-119

Proves, before trusting the renderer wiring, that CNA's `WEBGPU` identity can build for the
browser through Emscripten's **emdawnwebgpu** port and that the canvas-selector surface path the
renderer uses actually compiles **and links** into a real WebAssembly module.

## What it proved (2026-08-26)

- The active emsdk (`~/emsdk`, emscripten 6.0.3) ships the `emdawnwebgpu` port
  (`v20260423.175430`), whose `webgpu/webgpu.h` is the same unified header wgpu-native v29 exposes
  natively: `WGPUStringView`, `WGPURequestAdapterCallbackInfo`, `wgpuInstanceProcessEvents`,
  `wgpuCreateInstance(nullptr)` and `wgpuInstanceCreateSurface` all match.
- The browser surface source is `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector`
  (`sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector`, `chain` + `selector` fields),
  targeting a CSS selector (`"#canvas"`) rather than a native window handle. `main.cpp` mirrors
  `WebGPURenderer::CreateSurface()`'s Emscripten branch exactly.
- `cmake/ThirdPartyWebGPU.cmake`'s `if(EMSCRIPTEN)` branch builds a `WebGPU::WebGPU` INTERFACE
  target carrying `--use-port=emdawnwebgpu` on compile and link, with no wgpu-native download and no
  runtime library to copy. Configure printed
  `CNA WebGPU: using Emscripten emdawnwebgpu port (browser navigator.gpu)` and the link produced
  `spike.wasm` + `spike.js`.

## Reproduce

```bash
source ~/emsdk/emsdk_env.sh
cd spikes/webgpu-web-spike
emcmake cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build            # -> build/spike.js + build/spike.wasm
```

## Not covered here

A full in-browser bring-up — SDL3 canvas sizing, the `requestAnimationFrame` present loop, WGSL
shader compilation via `createShaderModule`, and a headless-Chrome smoke test — is the remaining
browser work tracked as WEBGPU-120/121/122 in `plans/plan_webgpu.md`.
