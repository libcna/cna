# PixiJS graphics renderer

## Status

`PIXIJS` was authored on **2026-08-16** per direct task instruction and is CNA's newest, most
experimental renderer. On **2026-08-17** a real Emscripten toolchain build was performed for the
first time: `cna_renderer_pixijs` and the `cna_test_pixijs_smoke` example both compile and link
successfully under real `emcc`/`em++` (emsdk, pinned PixiJS v7.4.2 UMD build). See `plan_pixijs.md`
for the full task breakdown, design decisions, and this renderer's own honest, continuously-updated
status legend -- what follows here is the real, current verification picture, not the original
zero-verification starting point.

Select it with (Emscripten only -- see "Important limitations"):

```bash
source $EMSDK/emsdk_env.sh
emcmake cmake -S . -B cmake-build-pixijs \
  -DCNA_GRAPHICS_RENDERER=PIXIJS \
  -DCNA_PIXIJS_ROOT=/absolute/path/to/pixi.min.js \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-pixijs -j4 --target cna_renderer_pixijs cna_test_pixijs_smoke
```

CNA vendors a pinned **PixiJS v7.4.2** UMD build (`plan_pixijs.md` Design decisions 3-4,
`cmake/ThirdPartyPixiJS.cmake`). `CNA_PIXIJS_SHA256` is now populated (computed from the real
`pixi.js@7.4.2` npm package's `dist/pixi.min.js`), so `-DCNA_PIXIJS_AUTO_DOWNLOAD=ON` (the default)
should work wherever `cdn.jsdelivr.net` is reachable -- it was not reachable from the sandbox that
performed this verification (outbound proxy policy), so `CNA_PIXIJS_ROOT` pointing at a manually
obtained copy (e.g. via `npm pack pixi.js@7.4.2`) was used instead; both paths are supported and
`CNA_PIXIJS_ROOT` is recommended whenever you already have a local copy.

## What has actually been verified

- A Python identity-registry check (`scripts/check_renderer_identities.py`) passes with `PIXIJS`
  added as the 47th public identity.
- **A real `emcmake cmake -DCNA_GRAPHICS_RENDERER=PIXIJS` configure succeeds.**
- **`cna_renderer_pixijs` compiles and links cleanly under real Emscripten** (emsdk 6.0.6). One real
  bug was found and fixed in the process: `Vector2::getZeroProperty()` doesn't exist (`Vector2::Zero`
  is the real accessor) -- everything else compiled on the first attempt.
- **`cna_test_pixijs_smoke` compiles and links into a real, runnable `.js`/`.wasm`.** Running it
  under plain `node` reproduces `CANVAS-15`'s own documented finding exactly: `SDL_Init` throws
  `ReferenceError: window is not defined` before any renderer-specific code runs, because Node has
  no real DOM. This is the expected, already-known boundary (needs a real browser via `emrun`), not
  a new PixiJS-specific failure.
- **Confirmed blocked, and confirmed NOT an emsdk-version issue**: the shared `CnaTests` target
  (built with `-sASYNCIFY=1` and `-fwasm-exceptions`) fails to link under Emscripten --
  `em++` itself warns `ASYNCIFY=1 is not compatible with -fwasm-exceptions`, and `wasm-opt --asyncify`
  then crashes (`UNREACHABLE executed at .../Flatten.cpp:231`). Reproduced **identically** under both
  emsdk 6.0.6 and 6.0.2 (the exact version `plan_canvas.md`'s own session used to successfully link
  `CnaTests.js`), so this is a real flag-combination incompatibility, not toolchain drift. This
  blocks `PixiJsRendererTests.cpp`'s structural GTest coverage from running under `node` -- fixing it
  means changing `CnaTests`' own shared link flags (`cmake/UnitTests.cmake`), which affects every
  renderer, not just `PIXIJS`, and is out of this renderer's own scope. See `plan_pixijs.md`'s
  2026-08-17 update for the full account, including the (unrelated) workaround needed for
  Emscripten's own blocked `zlib` port fetch in a network-restricted sandbox.
- **Not yet verified, and still the real headline gap**: nothing has been proven to draw a single
  correct pixel. `cna_test_pixijs_smoke` has never run to completion in a real browser. Everything
  below "What has actually been verified" is still exactly as provisional as it reads.

The renderer's C++ source compiles conditionally behind `#if defined(__EMSCRIPTEN__)` for every
`EM_JS` block, so the pure-C++ logic (blend-state mapping, the 2D-only `ThrowNo3D` surface) is
*structured* to be host-buildable and unit-testable the way `CanvasRendererTests.cpp` already proves
out for its own renderer, once `CnaTests` itself can be linked under Emscripten (see above). A
native (`SDL_RENDERER`) CMake configure and full `CnaTests` build were also run and passed,
confirming the shared registry edits (`GraphicsRendererType.hpp`, `GraphicsBackendCategory.hpp`,
`GraphicsBackendMaturity.hpp`, the physical-source-partition validator in `modules/CMakeLists.txt`,
the various renderer-identity compile-definition/count tests) did not break configuration or
compilation for every *other* renderer, including `GraphicsRendererTypeTest`'s 7/7 pass covering all
47 identities.

Do not read any ✅ mark in `plan_pixijs.md` as carrying the same confidence level a ✅ in
`plan_canvas.md`/`plan_webgpu.md` does. Most of the actual PixiJS draw-path code is still "written
and reviewed against the FNA/PixiJS API surface, zero automated (and no browser) verification of any
kind performed" -- the exceptions are the compile/link results called out above.

## Implemented baseline (unverified)

- CMake integration: `CNA_RENDERER_PIXIJS` option, `PIXIJS` identity registered everywhere the
  project's own identity-pinning mechanism (`scripts/check_renderer_identities.py`) checks, hard
  Emscripten-only gate, `--pre-js` wiring for the vendored PixiJS UMD build.
- `PixiJsRenderer : IGraphicsRenderer` -- full interface surface overridden; the inherently-3D-only
  methods wired to the shared `ThrowNo3D` convention every 2D-only CNA renderer uses.
- `PixiJsTextureRenderer`/`PixiJsRenderTargetRenderer` -- synchronous, buffer-backed texture upload
  via `PIXI.BufferResource`, and a real `PIXI.RenderTexture`-backed render target with readback via
  PixiJS's own `renderer.extract.pixels()`.
- `PixiJsSpriteBatchRenderer` -- pooled, retained `PIXI.Sprite` objects driven through their native
  `anchor`/`position`/`scale`/`rotation`/`tint`/`alpha` properties, flushed in one wasm→JS crossing
  per `SpriteBatch::End()`.
- Blend-state mapping for the 4 standard `BlendState` presets, as a pure C++ function
  (`BlendStateToPixiJsBlendMode`) intended to be unit-testable without a real `PIXI.Application`.

## Important limitations

Everything below is tracked with its own `PIXIJS-N` task in `plan_pixijs.md`; none of it should be
described as complete until that task actually closes with real verification:

- **No pixel-level or browser verification of any kind** (see above) -- compile/link verification now
  exists for `cna_renderer_pixijs`/`cna_test_pixijs_smoke`, but nothing has run in a real browser,
  and the shared `CnaTests` GTest target can't yet link under Emscripten here (toolchain issue, not a
  renderer bug) -- this remains the single largest limitation, and the reason every other item on
  this list is doubly uncertain.
- **`Present()`'s frame-timing relationship with PixiJS's own ticker** was a genuinely open design
  question (`PIXIJS-22`); this implementation pass resolved it by disabling PixiJS's own ticker
  (`autoStart:false`, `sharedTicker:false`) and calling `app.renderer.render()` explicitly from
  `Present()`, keeping CNA's `Game` loop authoritative -- consistent with every other renderer, but
  unverified.
- **Mip level > 0 texture uploads throw** (`PIXIJS-31`) -- unlike `CANVAS`, PixiJS textures can
  genuinely carry mipmaps, so this is a real gap to close, not a structural boundary like Canvas2D's.
- **Direct CPU pixel upload (`Texture2D::SetData`) into a bound render target throws**
  (`PixiJsRenderTargetRenderer::UpdatePixels`) -- a `PIXI.RenderTexture` has no simple synchronous
  CPU-buffer upload path the way a buffer-backed plain texture does; needs a re-upload-via-sprite
  design that has not been written yet.
- **`SpriteEffects` flip correctness is unverified** (`PIXIJS-44`) -- implemented via negative
  `sprite.scale` composed with the same `anchor` point rotation/scale already pivot around, believed
  correct by construction but never checked against a real FNA reference render.
- **`SetTransformMatrix()` (`Begin(transformMatrix)`) is not implemented** (`PIXIJS-45`) -- a
  non-identity matrix throws rather than being silently ignored.
- **Custom `Effect` support throws** (`PIXIJS-47`) -- unlike `CANVAS`/`HTML_DOM`, this is *not* a
  structural boundary (PixiJS has a real GLSL shader stage via `PIXI.Filter`/`PIXI.Shader`); it is
  simply out of this plan's v1 scope.
- **`ApplyBlendState` only supports the 4 standard presets** (`PIXIJS-50`/`PIXIJS-51`) -- a fully
  generic mapping via on-demand custom PixiJS blend-mode registration is believed straightforward
  (PixiJS exposes `renderer.state.blendModes` as a real, extensible GL blend-factor table) and is
  tracked as a stretch goal (`PIXIJS-52`), not assumed free.
- **`TextureAddressMode`/sampler filtering are not implemented** (`PIXIJS-46`/`PIXIJS-53`) -- the
  native `PIXI.WRAP_MODES`/`PIXI.SCALE_MODES` mapping is believed simple (real WebGL wrap modes,
  unlike `CANVAS-44`'s pattern-source emulation) but has not been written yet.
- **`SpriteFont` has not been confirmed to fall out for free** (`PIXIJS-60`) -- expected, by the
  same reasoning `CANVAS-50` used, but not checked.
- **MRT (`SetRenderTargets` with 2+ bindings) throws** -- a single `PIXI.Application`'s render
  pipeline targets one `RenderTexture` at a time in this v1 scope.
- **No 3D pipeline in v1** -- a deliberate scope line, not (unlike `CANVAS`/`HTML_DOM`) a structural
  ceiling; PixiJS's `Mesh`/`Geometry` API could in principle carry arbitrary vertex data, but
  reaching XNA 3D parity through it is out of this plan's scope.
- **No CDN-loaded PixiJS at build or run time** -- vendored and checksum-pinned only, and (see
  "Status" above) the checksum itself has not actually been pinned yet.

## Architecture notes

- **Window/canvas ownership**: reuses SDL3's existing `<canvas>` element (the same
  `Module['canvas'] || document.querySelector('canvas')` lookup `CanvasRenderer.cpp`/
  `EasyGLRenderer.cpp` already use); `PIXI.Application` is constructed with that element passed in
  explicitly (`view: existingCanvas`), so PixiJS never creates a second canvas and SDL3 keeps owning
  window sizing, input, and the event pump.
- **JS interop**: exclusively `EM_JS`, `CNA_PixiJs_*`-prefixed, mirroring `CANVAS`/`HTML_DOM`'s
  established convention -- no `embind`, no `emscripten::val`. State lives on `Module`:
  `Module['cnaPixiApp']` (the one `PIXI.Application`), `Module['cnaPixiTextures']` (an integer-id →
  `{texture, ...}` registry shared by both plain buffer-backed textures and render targets),
  `Module['cnaPixiSpritePool']` (the pooled, recycled `PIXI.Sprite` array), and
  `Module['cnaPixiActiveContainer']`/`Module['cnaPixiActiveRenderTexture']` (which container/target
  the next `Clear()`/`Draw()`/`Present()` operates against).
- **Draw batching**: `SpriteBatch::Draw()` appends a 14-word (56-byte) POD `DrawCommand` to a C++
  `std::vector`; `End()` hands the whole array to one `EM_JS` call (`CNA_PixiJs_FlushSprites`) that
  walks it via `HEAP32`/`HEAPF32`/`HEAPU32` and updates the pooled sprite array's native properties --
  a 2000-sprite frame costs one wasm→JS boundary crossing, not 2000, the same optimization
  `plan_html_dom.md` Design decision 5 already established for its own pooled `<div>` elements.
- **Why PixiJS over `WEBGL2` directly**: this renderer sits much closer to `WEBGL2` in raw
  capability than to `CANVAS`/`HTML_DOM` (PixiJS *is* a WebGL renderer) -- the value proposition is
  a higher-level retained-mode `Sprite`/`Container`/`RenderTexture` API this renderer's own
  implementation can drive directly, rather than hand-rolling vertex-buffer batching the way
  `EasyGLSpriteBatchRenderer.cpp` does, plus a real shot at full `BlendState`/wrap-mode fidelity
  `CANVAS`/`HTML_DOM` structurally cannot reach. See `plan_pixijs.md`'s own comparison table for the
  full picture.
