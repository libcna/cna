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
validation error (`plans/plan_webgpu.md` `WEBGPU-119`/`120`/`122` ✅). Its 3D/effect shaders are not
yet exercised in a browser (`WEBGPU-121` 🟨). Nothing below should be read as a status report for
either WebGPU path.

> **2026-07-15 update, scoped to the new `CANVAS` renderer (`plans/plan_canvas.md`), not EasyGL**: this
> document's own "no `.sdl-prebuilt-emscripten` directory... no `cmake-build-*emscripten*` directory
> has ever existed" claim below is now out of date in one narrow respect — bringing up `CANVAS` on
> `feature/canvas` required a real `emcmake`/`emcc` 6.0.2 configure+build, which succeeded and
> produced exactly such a directory (`cmake-build-canvas`) for the first time in this project's
> history, confirming `emsdk` is genuinely available in this dev environment. `CnaTests` links and a
> real, renderer-agnostic GTest suite genuinely passes under `node CnaTests.js` for `CANVAS`. This
> does **not** extend to `EasyGL`/WebGL2 specifically — nothing below about that path has been
> re-verified — but the *tooling* premise ("no one has ever actually run `emcc` here") no longer
> holds project-wide. See `docs/canvas-renderer.md` and `plans/plan_canvas.md` for what was actually
> verified.

## Status headline: real build scaffolding, zero verified execution

The CMake/link-flag infrastructure below is real, non-trivial engineering — not a stub. However:

- **No `.sdl-prebuilt-emscripten` directory exists on disk** (the cache convention `cmake/ThirdPartySDL.cmake`
  defines for it), and **no `cmake-build-*emscripten*` directory has ever existed in this project's
  history**. No `.github/workflows/*.yml` file configures or builds an Emscripten target at all.
- The graphics-specific integration/pixel-readback test suite (`examples/*_test.cpp`, registered via
  the `cna_easygl_test()`/`cna_vulkan_test()`/etc. CMake macros) is **explicitly excluded on
  Emscripten** (`CMakeLists.txt`: `if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32 ...)`, 4 call
  sites). None of this project's hundreds of pixel-correctness tests — the actual mechanism this
  whole session's graphics audit has relied on to verify every renderer — have ever run, or could
  currently run, under Emscripten.
- `CnaTests` (the shared gtest unit-test binary, containing e.g. `TextureCubeTests.cpp`) **is**
  Emscripten-buildable and has real, evidently-exercised link-time tuning (`-sASYNCIFY=1`,
  `-sEXIT_RUNTIME=1`, with a comment noting the Asyncify requirement was "confirmed empirically" —
  see `CMakeLists.txt` around the `CnaTests` `add_executable` call). That empirical confirmation is
  documented in the context of the `SystemLink`/networking test suite, not graphics — Node.js (the
  usual local Emscripten test runner) has no WebGL/canvas implementation at all, so even if
  `CnaTests` links successfully against the `EasyGL` renderer for Emscripten, any test that
  default-constructs a real `GraphicsDevice` would need an actual browser (or a headless-browser
  harness this project does not have) to run at all.
- **Conclusion**: every claim below about WebGL2/GLES3 behavior is a **design-time expectation**,
  not something empirically verified in this project. Treat this document as "what to expect and
  check first" when someone eventually does the first real `emcc` build and opens it in a browser —
  not as a completed audit.

## What already exists (real CMake/build-system work)

- **`EasyGL` is the default renderer on Emscripten** (and Linux) — `CMakeLists.txt`: "Emscripten uses
  WebGL 2 (= OpenGL ES 3.0), which the EasyGL renderer targets." No other renderer (`SDL_RENDERER`,
  `VULKAN`, `BGFX`) has any Emscripten-specific wiring at all; selecting one of those for an
  Emscripten build is untested and not a supported configuration today.
- **C++ exceptions are force-enabled globally for Emscripten** (`-fexceptions
  -sNO_DISABLE_EXCEPTION_CATCHING=1`, applied before `sharp-runtime` is added) — Emscripten disables
  exception unwinding by default, but CNA's `System::Exception` hierarchy and every `EXPECT_THROW`
  test in this codebase depend on real unwinding working end-to-end.
- **`cna_house3d_demo`** (the one real 3D EasyGL/Vulkan demo, gated to `OPENGLES3 OR OPENGL33 OR WEBGL1 OR WEBGL2 OR VULKAN`) pins
  `-sMIN_WEBGL_VERSION=2`/`-sMAX_WEBGL_VERSION=2` explicitly — the only target in the whole build
  that does. `cna_demo_2d`/`cna_demo_sound` have Emscripten link options (`.html` output suffix,
  `-sALLOW_MEMORY_GROWTH=1`, `--preload-file` for their `Content` directories, per-demo memory
  sizing) but do **not** pin a WebGL version — an inconsistency worth resolving before any real
  in-browser testing begins, since a 2D-only demo unintentionally negotiating WebGL 1 instead of 2
  would silently run against a different (and lesser) capability set than `EasyGL`'s desktop-GL
  code assumes.
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

This is real, thought-through code addressing a genuine WebGL-specific concern — not a stub — but,
per the status headline above, it has never been exercised against a real browser's WebGL
implementation; only the desktop `else` branch has ever actually run (via the ordinary desktop GL
context-loss tests already covered elsewhere in this project's test suite).

## Anticipated WebGL2/GLES3 capability gaps (design-time, unverified)

These are expectations based on the WebGL 2 / OpenGL ES 3.0 specification versus the desktop OpenGL
`EasyGL` otherwise targets — **none of these have been empirically confirmed against a real browser
in this project**:

- **No geometry or tessellation shaders.** GLES 3.0/WebGL 2 has neither stage. CNA's `Effect`/shader
  pipeline does not currently use either, so this is likely a non-issue in practice, but any future
  shader work must not assume desktop-GL-only stages are available when the EasyGL implementation is targeting
  Emscripten.
- **Anisotropic filtering requires the `EXT_texture_filter_anisotropic` WebGL extension**, which is
  not guaranteed present on every browser/GPU combination (unlike desktop GL, where it is
  near-universal). **Task 918 (fixed, 2026-07-09)** added real `EasyGL` anisotropic filtering,
  gated on `HasExtension("GL_EXT_texture_filter_anisotropic")` and clamped to the live driver's
  reported cap — so on Emscripten specifically, whether anisotropic filtering actually does anything
  now genuinely depends on whether the browser/GPU exposes the WebGL variant of that extension; this
  has **not been empirically confirmed against a real browser** (matching this whole section's own
  "design-time, unverified" scope) — if the extension is absent, `TextureFilter::Anisotropic`
  correctly falls back to the plain trilinear filter set already in place, it just won't be a
  currently-untracked bug if that happens on Web the way it briefly was on desktop EasyGL pre-918.
- **Texture format support is narrower** than desktop GL's — WebGL 2 guarantees a smaller baseline
  set of internal formats and compressed-texture extensions vary significantly by browser/GPU. CNA's
  own `SurfaceFormat::Color`-only constraint (Task 176, already enforced identically on every
  renderer) means this is currently a non-issue in practice for the same reason as above.
- **A browser that only supports WebGL 1 (not WebGL 2) has no fallback path today.** `-sMIN_WEBGL_VERSION=2
  -sMAX_WEBGL_VERSION=2` (where set, see above) makes Emscripten refuse to negotiate WebGL 1 at all,
  rather than degrading — the correct choice for `cna_house3d_demo` given GLES 3.0 is baked into
  `EasyGL`'s assumptions, but it does mean CNA has zero WebGL-1-class browser support by design, not
  by oversight.

## Canvas-as-display model

The browser sandbox has no OS-level display-mode list to enumerate or switch — "the display" is
effectively the `<canvas>` element, sized by CSS/JS rather than a hardware `DisplayMode`. This is
already documented in `docs/viewport-displaymode-adapter-support.md` (`GraphicsAdapter`/`Viewport`
behavior on Web/Emscripten) — cross-referenced here rather than duplicated, since it's a
device/adapter-model concern rather than a rendering-renderer one.

## Summary

| Area | Status |
|---|---|
| CMake/link-flag scaffolding (renderer selection, exception handling, memory/preload flags, WebGL version pin on the 3D demo) | Real, present, never exercised end-to-end |
| `CnaTests` Emscripten build | Links (with real Asyncify tuning for the networking suite); cannot meaningfully run graphics-touching tests without a real browser/WebGL context |
| Graphics integration/pixel tests (`examples/*_test.cpp`) | Explicitly excluded on Emscripten — zero coverage |
| WebGL context-loss handling (`EasyGLRenderer.cpp`) | Real, non-trivial code; never run against a real browser |
| GLES3/WebGL2 capability gaps vs. desktop GL | Anticipated only, not verified; current `SurfaceFormat`/anisotropy constraints happen to sidestep most of them today |
| WebGPU | Native `wgpu-native` renderer is active and experimental; the browser/Emscripten WebGPU path (same `WEBGPU` identity, via the emdawnwebgpu port) runs the 2D renderer in headless Chrome as of 2026-08-26 (`WEBGPU-122` ✅). 3D/effect shaders not yet exercised in a browser (`WEBGPU-121`); run it with `scripts/run-webgpu-browser-test.sh` |

**Recommendation for whoever eventually does the first real Emscripten build**: start by getting
`cna_house3d_demo` (the one target with a WebGL version pin already) running in an actual browser
and confirming the WebGL context-loss debug hooks fire correctly, before attempting to get any part
of `CnaTests` running there — the graphics pixel-test suite's Emscripten exclusion means that would
be genuinely new coverage, not a re-run of already-proven tests.
