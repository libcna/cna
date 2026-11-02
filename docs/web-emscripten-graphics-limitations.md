# Web/Emscripten graphics platform

Status: current platform contract, reconciled with the Emscripten build policy, browser harnesses,
renderer documents and known-defect ledger on **2026-10-31**.

This page owns the browser platform layer shared by CNA graphics builds: Emscripten toolchain and
link rules, canvas/display behavior, game-loop ownership, optional pthreads, browser storage and
lifecycle qualification. Renderer-specific feature promises remain in
[`webgl2-renderer.md`](webgl2-renderer.md) and [`webgpu-renderer.md`](webgpu-renderer.md).

## Supported browser renderer identities

| Identity | Browser implementation | Current boundary |
|---|---|---|
| `WEBGL2` | EasyGL over an exact WebGL 2 / GLSL ES 3.00 context | Supported-class and the Emscripten default; current reproducible runtime evidence is Chrome |
| `WEBGPU` | WebGPU renderer through Emscripten's `emdawnwebgpu` port | Supported-class; browser and native paths share the public identity but have separately stated evidence in the WebGPU page |

The canonical identity/platform gates are in [`renderer-registry.md`](renderer-registry.md).
Other native renderer identities are not browser fallbacks. `WEBGL2` deliberately has no WebGL 1
mode.

## Build and final-link contract

Emscripten is a cross-compilation target, so use `emcmake` for configuration. A minimal WebGL 2 C
ABI artifact and its browser page are built with:

```bash
emcmake cmake -S . -B cmake-build-wasm-webgl2 -G Ninja \
  -DCNA_GRAPHICS_RENDERER=WEBGL2 \
  -DCNA_BUILD_C_API=ON \
  -DCNA_BUILD_TESTS=OFF \
  -DCNA_BUILD_EXAMPLES=OFF
cmake --build cmake-build-wasm-webgl2 --parallel 8 \
  --target cna_c_api_browser_probe
```

The toolchain contract has four parts:

- CNA and every linked consumer use the same C++ exception ABI. Emscripten otherwise disables
  behavior on which CNA's `System::Exception` paths depend.
- CNA's final WebGL application and C ABI targets call the renderer-aware link helper, which pins
  `MIN_WEBGL_VERSION=2` and `MAX_WEBGL_VERSION=2`. Non-WebGL targets receive neither option; an
  external final executable must apply the same exact WebGL 2 requirement.
- Application executables that call blocking `Game::Run()` use `CNA::EmscriptenAsyncify`; CNA's
  own application targets opt into it automatically. The C ABI Wasm library deliberately links
  with Asyncify off because JavaScript owns its `requestAnimationFrame` loop and calls
  `cna_game_run_frame_ext` synchronously.
- A browser page must serve `.mjs` as JavaScript and `.wasm` as `application/wasm`. Opening a
  generated page directly with `file:` is not a valid deployment or threading test.

[`emscripten-mainloop-game-lifetime.md`](emscripten-mainloop-game-lifetime.md) is the detailed
ownership and scheduler contract. A Node smoke can validate module instantiation and ABI calls
that do not create graphics state, but Node has no browser canvas and is not WebGL or WebGPU
qualification.

## Canvas and display model

The browser has no OS display-mode list for CNA to enumerate or switch. The display is the canvas:
its logical back-buffer size, drawing-buffer size and CSS presentation rectangle are distinct.
`GraphicsAdapter` therefore publishes a browser adapter and a synthetic current display mode;
resize and viewport behavior are specified in
[`viewport-displaymode-adapter-support.md`](viewport-displaymode-adapter-support.md).

EasyGL preserves XNA-style logical dimensions while presenting into the canvas drawable. Unlike
the native profiles' always-offscreen path, WebGL draws directly into framebuffer 0 at one sample;
multisampling uses the offscreen path. The page controls windowed CSS sizing.

In DOM fullscreen the browser owns the drawable size. CNA leaves that canvas size intact when
applying logical back-buffer preferences: SDL's Emscripten size setter would otherwise replace it
with the virtual back-buffer dimensions. `CApi_WasmFullscreenProbe` checks real presented pixels
and the public logical viewport across portrait/landscape sizes, repeated fullscreen entry/exit
and Chrome device-pixel-ratio settings of 1 and 2. The current default canvas uses SDL's standard
resolution mode; those cases do not qualify high-density canvas allocation, other browsers or a
pthread-driven fullscreen transition. Arbitrary windowed CSS stretching remains page-owned.

## Optional pthread ABI

Thread support is opt-in because `-pthread` changes the WebAssembly ABI of every object in the
final module:

```bash
emcmake cmake -S . -B cmake-build-wasm-webgl2-threads -G Ninja \
  -DCNA_GRAPHICS_RENDERER=WEBGL2 \
  -DCNA_ENABLE_EMSCRIPTEN_THREADS=ON
cmake --build cmake-build-wasm-webgl2-threads --parallel 8
```

`CNA_ENABLE_EMSCRIPTEN_THREADS=ON`:

- compiles and links CNA and final consumers with `-pthread`;
- enables Sharp Runtime's Emscripten thread implementation;
- selects a pthread-compatible SDL archive cache, separate from the single-threaded cache;
- enables Emscripten's `OFFSCREEN_FRAMEBUFFER` proxy for the WebGL route; and
- uses WasmFS by default so worker-side content reads do not proxy legacy-filesystem operations to
  the browser thread.

The default worker pool contains one worker. Applications that require more concurrently active
threads set `SHARP_RUNTIME_EMSCRIPTEN_PTHREAD_POOL_SIZE` to a positive integer at configure time.

For WebGL, `OFFSCREEN_FRAMEBUFFER` proxies individual GL calls to the browser thread; it does not
make a sequence of calls atomic. EasyGL's renderer-context lease provides that missing exclusion:
a frame and a background `Content.Load<T>()` cannot interleave their binds and uploads. The main
thread can wait because Emscripten's futex wait continues servicing the proxy queue. Waiting from a
frame for a worker that needs the same lease remains an application-level deadlock risk, just as a
native GL frame cannot retain its context while waiting for another thread to acquire it.

Pthreads require a cross-origin-isolated page. The server must send at least:

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Cross-origin scripts, Wasm or content must additionally opt in through CORS or an appropriate
`Cross-Origin-Resource-Policy`. Verify `window.crossOriginIsolated === true` before treating a
thread-start failure as a CNA defect.

### Browser persistence

Single-threaded builds use the legacy Emscripten filesystem and mount `/cna-storage` and `/save`
over IDBFS before application startup. A threaded build uses WasmFS by default, which cannot link
IDBFS; in that configuration persistent `StorageDevice`/isolated storage reports unavailable
instead of pretending an in-memory save is durable.

A threaded application that requires IDBFS can configure
`-DCNA_EMSCRIPTEN_USE_WASMFS=OFF`. That restores the persistent mounts, but legacy filesystem calls
from a worker can wait on the browser thread and are a poor fit for background content loading.
The choice is therefore explicit rather than silently mixing incompatible filesystems.

## Browser loop and C ABI embedding

An ordinary C++ application may keep its `Game` as a local object and call `Game::Run()`.
Asyncify suspends the same Wasm stack between `requestAnimationFrame` callbacks, so the caller is
not unwound and the game remains alive until `Exit()` makes `Run()` return.

An embedding host uses the separate C ABI contract: JavaScript owns the animation loop and calls
`cna_game_run_frame_ext`. Both routes use the same tick state machine, timing accumulator,
draw-suppression, exit, event and renderer-context behavior. The browser probe exercises a
C-owned Game through this host-driven route and verifies real graphics work rather than treating a
successful link as runtime evidence.

## WebGL context loss

WebGL contexts can be invalidated asynchronously by the browser. EasyGL installs the canvas
`webglcontextlost`/`webglcontextrestored` callbacks and routes them through meta-gl's recovery
registry. During the lost interval CNA continues `Update` but skips `Draw`/`Present`; after the
browser reports restoration, retained textures, buffers, render targets, cube maps, effects and
queries rebuild their native state.

`DebugSimulateContextLoss()` and `DebugRestoreContext()` use the real `WEBGL_lose_context`
extension in an Emscripten build, so loss and restoration arrive as separate browser events rather
than the synchronous destroy/recreate used by native EasyGL. Foreground loss/recovery has been
exercised during loading, menus and a 3D scene. `Texture3D` is the known exception: it is not yet a
recoverable EasyGL resource and retains a dead native handle after restoration. That defect is in
[`handoff.md` section 5.1](../handoff.md#51-defects).

Browser-driven loss while backgrounded or after a GPU-process restart is not part of the current
reproducible gate. The debug extension validates CNA's recovery path, not every browser lifecycle
policy.

## Capability and evidence boundary

Web APIs deliberately expose optional features through the live adapter/context. Code must use
`GraphicsDevice::GetRendererCapabilityProfileEXT()`, numeric limits and per-format usage queries
instead of assuming that all browsers share a format or extension set. In particular:

- compressed textures, float/normalized targets and anisotropic filtering depend on the live
  implementation;
- WebGL polygon mode is extension-dependent, and texture swizzle is unavailable;
- WebGL occlusion queries answer visibility rather than an exact sample count; and
- the browser renderer supports more than `SurfaceFormat::Color`; support is classified per
  format and use, not by a global color-only rule.

Current browser evidence consists of dedicated headless-Chrome harnesses plus real sample runs.
The C ABI harness serves the artifact over HTTP, obtains the selected browser graphics API, drives
frames, performs graphics operations/readback and requires a complete pass summary with no page
error. Application evidence additionally covers touch input, background XNB model/resource
loading and WebGL context recovery. Most device-backed GoogleTest executables remain native
tests; compiling an Emscripten archive or running a Node module smoke is not counted as browser
graphics execution.

The Emscripten multi-renderer workflow is a build/link/registry gate for a `WEBGL2;WEBGPU` bundle.
It intentionally does not launch that bundle in a browser; the renderer-specific browser harnesses
provide runtime evidence. Current qualification is Chrome-centric. Firefox/Safari and a broad
physical-GPU/device matrix are not continuous gates, so compatibility claims must stay within the
measured boundary.

Current product defects remain centralized in [`handoff.md` section 5](../handoff.md#5-known-bugs-and-limitations).
This page records deliberate browser-platform constraints; it is not a second defect ledger.
