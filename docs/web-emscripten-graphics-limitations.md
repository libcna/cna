# Web (Emscripten/WebGL 2) Graphics Renderer — Status and Limitations

CNA has real, substantial CMake scaffolding for building against the Emscripten toolchain, targeting
the browser via the `EasyGL` renderer running over WebGL 2 (= OpenGL ES 3.0). This document is the
Task 459 status write-up for that path, scoped to the **graphics renderer specifically** — audio,
input, and device support under Emscripten are already covered elsewhere (`NEXTdevices.md`,
`noxna_devices.md`) and are out of scope here.

**This document covers only the existing EasyGL-over-WebGL2 browser path.** The project owner
activated the native `wgpu-native` renderer on 2026-07-12. A browser/Emscripten WebGPU path (the
same `WEBGPU` identity, built through Emscripten's emdawnwebgpu port) reached a running 2D renderer
on 2026-08-26: `cna_demo_2d` renders SpriteBatch frames in headless Chrome with audio and no WebGPU
validation error, and the 3D `BasicEffect` path plus every stock effect shader
(`PbrEffect`/`EnvironmentMapEffect`/`SkinnedEffect`/dual-texture/alpha-test) compile and render
in-browser too (`plans/plan_webgpu.md` `WEBGPU-119`/`120`/`121`/`122` all ✅). Nothing below should be
read as a status report for either WebGPU path.

## Status headline: verified WebGL 2 execution, including background content loading

The EasyGL/WebGL 2 path now has real browser evidence. On 2026-08-30, SAMPLE-061 (`MarbleMaze`) was
built with Emscripten pthreads and run in Chrome with a real WebGL 2 context. Browser touch events
navigated the menu and instruction screens; the original loading thread decoded the compiled XNB
models and created their textures and vertex/index buffers; gameplay rendered; and Escape reached
the pause screen. The run was cross-origin isolated and recorded no JavaScript exception, rejected
promise, or failed HTTP response.

This does not turn every desktop graphics test into a browser test: most device-backed
`examples/*_test.cpp` targets remain native-only, and Node.js still cannot supply a browser canvas.
It does establish that the application, SDL3 platform, EasyGL renderer, compiled XNB model path,
input path, and `System.Threading.Thread` can operate together in a real WebGL 2 browser build.

## Threaded Wasm mode

Thread support is deliberately opt-in because `-pthread` changes the ABI of every object in the
final Wasm module:

```bash
emcmake cmake -S . -B cmake-build-webgl2-threads \
  -DCNA_GRAPHICS_RENDERER=WEBGL2 \
  -DCNA_ENABLE_EMSCRIPTEN_THREADS=ON
cmake --build cmake-build-webgl2-threads --parallel 8
```

`CNA_ENABLE_EMSCRIPTEN_THREADS=ON` does all of the application-wide wiring:

- compiles and links CNA and final consumers with `-pthread`;
- enables `SHARP_RUNTIME_ENABLE_EMSCRIPTEN_THREADS` for `System.Threading`;
- enables Emscripten's `OFFSCREEN_FRAMEBUFFER` GL proxy so resource creation from a loading pthread
  reaches the browser thread that owns the WebGL context; and

`OFFSCREEN_FRAMEBUFFER` proxies **each individual GL call** to the browser thread, where every
thread's calls execute against the same context. What keeps a loading pthread from interleaving
with the frame is therefore not the proxy but the renderer's thread-context lease
(`IGraphicsRenderer::AcquireThreadContextLeaseEXT`), which `GraphicsDeviceManager::BeginDraw` holds
for a whole frame and `ContentReader` for a whole asset. Until 2026-09-06 EasyGL returned a null
lease on Emscripten, on the grounds that the web has no thread-affine context ownership to hand
over; that is true of the binding half of the contract but not of the exclusion half, and without
it a loader's `glBindTexture`/`glTexImage2D` pair could be split by the frame's own binds. The
symptom was a texture that came out empty with no GL error anywhere -- a different subset of
background-loaded textures rendered black in 2 of 5 Firefox and 1 of 5 Chrome runs of SAMPLE-067
CatapultWars, and in none of 10 runs after the lease was restored. A background load and a frame
therefore now exclude each other on the web exactly as they do natively: the frame waits for a
whole `Content.Load<T>()` to finish, and the loading screen it draws is frozen while that happens,
which is the native behaviour too.

The main thread waits by blocking on a mutex, which is safe here specifically because Emscripten's
main-thread `emscripten_futex_wait` keeps servicing the proxy queue while it spins -- that is what
lets the loader's proxied GL calls complete and release the lease.
- uses `.sdl-prebuilt-emscripten-pthreads`, separate from the incompatible single-threaded
  `.sdl-prebuilt-emscripten` SDL archive cache.

The default preallocated worker count is one. Override it at configure time with
`-DSHARP_RUNTIME_EMSCRIPTEN_PTHREAD_POOL_SIZE=<positive integer>` when an application needs more
simultaneously active threads.

Browsers expose `SharedArrayBuffer` only in a cross-origin-isolated page. The HTTP server must send
at least these response headers for the HTML, JavaScript, Wasm, worker, and preloaded content:

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Resource-Policy: cross-origin
```

Verify `window.crossOriginIsolated === true` before treating a browser failure as a CNA threading
bug. Opening the generated HTML directly from disk is not a valid threaded-Wasm test.

## What already exists (real CMake/build-system work)

- **`EasyGL` is the default renderer on Emscripten** (and Linux) — `CMakeLists.txt`: "Emscripten uses
  WebGL 2 (= OpenGL ES 3.0), which the EasyGL renderer targets." No other renderer (`SDL_RENDERER`,
  `VULKAN`, `BGFX`) has any Emscripten-specific wiring at all; selecting one of those for an
  Emscripten build is untested and not a supported configuration today.
- **C++ exceptions are force-enabled globally for Emscripten** (`-fexceptions
  -sNO_DISABLE_EXCEPTION_CATCHING=1`, applied before `sharp-runtime` is added) — Emscripten disables
  exception unwinding by default, but CNA's `System::Exception` hierarchy and every `EXPECT_THROW`
  test in this codebase depend on real unwinding working end-to-end.
- **Final graphics targets share one renderer-aware WebGL contract.** `cna_demo_2d`,
  `cna_house3d_demo`, the graphics benchmark and `cna_c_api_wasm` all use the same helper:
  `WEBGL1` pins `MIN/MAX_WEBGL_VERSION=1/1`, `WEBGL2` pins `2/2`, and non-WebGL renderers receive no
  irrelevant WebGL requirement. Both C-API variants were clean-built and exercised in Chrome with
  the expected real context version.
- **`cna_demo_xact` is excluded on Emscripten** (and Android) — XACT audio is a Windows/Xbox-specific
  content pipeline with no web equivalent, unrelated to the graphics renderer itself.

## Real, previously-undocumented WebGL-aware graphics code

`EasyGLRenderer.cpp` has genuine, non-trivial `#if defined(__EMSCRIPTEN__)` handling for
**WebGL context loss** — a real browser behavior (the GPU driver/compositor can invalidate a page's
WebGL context at any time, e.g. after a tab is backgrounded for too long or the GPU process
crashes) that has no equivalent on desktop GL:

- `CNA_DebugLoseWebGLContext()`/`CNA_DebugRestoreWebGLContext()` (`EM_JS` — real inline JavaScript)
  call the actual `WEBGL_lose_context` browser extension to simulate a real context loss/restore for
  testing purposes.
- `metagl::InstallEmscriptenContextLossCallbacks()` is installed once at renderer construction,
  wiring the browser's asynchronous `webglcontextlost`/`webglcontextrestored` canvas events to
  `metagl::NotifyContextLost()`/`NotifyContextRestored()`.
- `DebugSimulateContextLoss()`/`DebugRestoreContext()` branch into two structurally different code
  paths: on Emscripten, loss and restore are **two separate asynchronous events** (the browser
  fires `webglcontextlost` immediately but `webglcontextrestored` only later, on its own schedule);
  on desktop, `DebugSimulateContextLoss()` performs a **synchronous, immediate** destroy-and-recreate
  of the real SDL GL context (there's no equivalent async browser event to wait for), and
  `DebugRestoreContext()` just calls it again since desktop loss+restore is atomic.

This path is browser-qualified as of 2026-09-04. Racing Game called the real
`WEBGL_lose_context` extension during progressive loading, in its main menu and during a 3D race.
All three transitions produced `lost,restored`, resumed animation and a substantial resource-backed
PNG. The run also exposed and fixed two omissions hidden by the original callback plumbing: live
MojoShader effects and cube maps now rebuild from retained CPU state, and the XNA frame boundary
skips Draw/Present (while continuing Update) during the asynchronous lost/restored interval.

## WebGL2/GLES3 capability gaps

These are expectations based on the WebGL 2 / OpenGL ES 3.0 specification versus the desktop OpenGL
`EasyGL` otherwise targets. The MarbleMaze run confirms the subset it exercises, not every optional
format, extension, shader path, or context-loss transition:

- **No geometry or tessellation shaders.** GLES 3.0/WebGL 2 has neither stage. CNA's `Effect`/shader
  pipeline does not currently use either, so this is likely a non-issue in practice, but any future
  shader work must not assume desktop-GL-only stages are available when the EasyGL implementation is targeting
  Emscripten.
- **Anisotropic filtering requires the `EXT_texture_filter_anisotropic` WebGL extension**, which is
  not guaranteed present on every browser/GPU combination (unlike desktop GL, where it is
  near-universal). **Task 918 (fixed, 2026-07-09)** added real `EasyGL` anisotropic filtering,
  gated on `HasExtension("GL_EXT_texture_filter_anisotropic")` and clamped to the live driver's
  reported cap — so on Emscripten specifically, whether anisotropic filtering actually does anything
  now genuinely depends on whether the browser/GPU exposes the WebGL variant of that extension. The
  2026-08-30 Chrome/SwiftShader gate exposed it with a reported 16x limit, but other browsers and
  GPUs may differ. If the extension is absent, `TextureFilter::Anisotropic`
  correctly falls back to the plain trilinear filter set already in place, it just won't be a
  currently-untracked bug if that happens on Web the way it briefly was on desktop EasyGL pre-918.
- **Texture format support is narrower** than desktop GL's — WebGL 2 guarantees a smaller baseline
  set of internal formats and compressed-texture extensions vary significantly by browser/GPU. CNA's
  own `SurfaceFormat::Color`-only constraint (Task 176, already enforced identically on every
  renderer) means this is currently a non-issue in practice for the same reason as above.
- **`WEBGL2` deliberately has no implicit WebGL 1 fallback.** Its exact 2/2 link contract prevents
  Emscripten from selecting a context that cannot compile its GLSL ES 3.00 shaders. Applications
  targeting WebGL-1-class browsers must build the distinct `WEBGL1` renderer, whose exact 1/1
  contract and five-frame C-API browser probe are verified separately.

## Canvas-as-display model

The browser sandbox has no OS-level display-mode list to enumerate or switch — "the display" is
effectively the `<canvas>` element, sized by CSS/JS rather than a hardware `DisplayMode`. This is
already documented in `docs/viewport-displaymode-adapter-support.md` (`GraphicsAdapter`/`Viewport`
behavior on Web/Emscripten) — cross-referenced here rather than duplicated, since it's a
device/adapter-model concern rather than a rendering-renderer one.

## Summary

| Area | Status |
|---|---|
| CMake/link-flag scaffolding (renderer selection, exceptions, preload, optional pthread ABI) | Built and exercised end-to-end in Chrome |
| `CnaTests` Emscripten build | Links (with real Asyncify tuning for the networking suite); cannot meaningfully run graphics-touching tests without a real browser/WebGL context |
| SAMPLE-061 browser integration | Menu, touch, background XNB model/resource loading, gameplay and pause verified in real Chrome/WebGL 2 |
| Graphics integration/pixel tests (`examples/*_test.cpp`) | Mostly native-only; SAMPLE-061 now supplies a real browser integration gate, not exhaustive pixel coverage |
| WebGL context-loss handling (`EasyGLRenderer.cpp`) | Three real loss/restore transitions qualified in Racing Game: loading, menu and 3D race |
| GLES3/WebGL2 capability gaps vs. desktop GL | Core SAMPLE-061 paths verified; optional capabilities remain browser/GPU dependent |
| WebGPU | Native `wgpu-native` renderer is active and experimental; the browser/Emscripten WebGPU path (same `WEBGPU` identity, via the emdawnwebgpu port) runs the 2D AND 3D paths in headless Chrome as of 2026-08-26 -- 2D SpriteBatch, 3D `BasicEffect`, and every stock effect shader all render in-browser (`WEBGPU-121`/`122` ✅). Run it with `scripts/run-webgpu-browser-test.sh` |

The next high-value browser gates are lifecycle coverage outside an active foreground canvas
(background/resume and browser-driven GPU-process loss) and the multi-browser/device matrix. The
deliberate foreground `WEBGL_lose_context` resource-recovery gate is now covered directly rather
than inferred from SAMPLE-061's threaded loading run.
