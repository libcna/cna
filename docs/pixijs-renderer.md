# PixiJS graphics renderer

## Status

`PIXIJS` was authored on **2026-08-16** per direct task instruction and is CNA's newest, most
experimental renderer. On **2026-08-17** a real Emscripten toolchain build was performed for the
first time, and later the same day `cna_test_pixijs_smoke` was run in a **real browser** (headless
Chromium), starting at **5/5 PASS** and growing across several rounds the same day to **21/21
PASS**, covering scaled draws, rotation, `SpriteEffects` flip, all 3 currently-mapped blend presets,
full render-target bind/Clear/draw/readback round-tripping (both via `SpriteBatch` and via direct
`Texture2D::SetData`), both sampler-state entry points (`SetSamplerFilter`/`SetSamplerAddressMode`),
`SpriteFont`/`DrawString`, and a real `Begin(transformMatrix)` camera-style transform. Five real,
independent bugs (`REMED-PIXIJS-1` through `REMED-PIXIJS-5`) were found and fixed along the way, each
preceded by a standalone live-browser probe confirming the actual PixiJS/WebGL behavior before the
fix was written. See `plan_pixijs.md`
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
- **`cna_test_pixijs_smoke` compiles and links into a real, runnable `.js`/`.wasm`/`.html`.** Running
  it under plain `node` reproduces `CANVAS-15`'s own documented finding exactly: `SDL_Init` throws
  `ReferenceError: window is not defined` before any renderer-specific code runs, because Node has no
  real DOM.
- **Run in a real browser (headless Chromium, driven by Playwright, `--use-gl=swiftshader`), served
  over local HTTP: grew from `5/5 PASS` to `21/21 PASS` across several rounds on 2026-08-17.** The
  first attempt scored 3/5 -- window/renderer plumbing checks passed, but both pixel-value checks
  (`GetBackBufferData` after a scaled `Draw`) failed. Diagnosed live in the page (`page.evaluate()`
  dumping texture buffers, sprite state, and a manually-forced re-render + `extract.pixels()`), which
  showed the texture upload, sprite anchor/scale math, and readback mechanism were ALL already correct
  in isolation. The real bug: PixiJS is **retained-mode** -- `SpriteBatch::Draw()` only mutates pooled
  sprite properties, nothing paints until `renderer.render()` runs, and `Present()` was the only
  place that called it. XNA's `GetBackBufferData()` is legally callable mid-frame, before the
  framework's own end-of-frame `Present()` -- exactly what the smoke test does -- so the readback saw
  the *previous* frame's stale backbuffer. **Fixed** (`REMED-PIXIJS-1`): both readback `EM_JS`
  functions (`CNA_PixiJs_ReadCurrentPixels`, `CNA_PixiJs_ReadTexturePixels`) now force a render of
  the relevant container/target immediately before extracting pixels. After the fix: **5/5 PASS**.
  The test was then extended across several more rounds, each one finding and fixing a real bug via
  live-browser probing before writing the corresponding assertion:
  - **Rotation/flip** (frames 2-3, → 9/9): rotation-around-origin passed immediately; flip did not --
    the original negative-`sprite.scale` design visibly shifted the destination rectangle's footprint
    (confirmed via a probe against known texel data). **Fixed** (`REMED-PIXIJS-2`): flip now uses
    `PIXI.Texture`'s own GroupD8 `rotate` parameter (12=H-mirror, 8=V-mirror, 4=both, values confirmed
    empirically) instead of negative scale.
  - **Blend modes** (frames 4-6, → 12/12): `Opaque` was collapsed onto the same
    `PIXI.BLEND_MODES.NORMAL` every preset used -- **fixed** (`REMED-PIXIJS-3`) by mapping it to
    `BLEND_MODES.NONE` (unconditional overwrite). Separately, `PIXI.ALPHA_MODES.UNPACK` turned out to
    be `PREMULTIPLY_ON_UPLOAD`, silently premultiplying every uploaded texture -- **fixed**
    (`REMED-PIXIJS-4`) by uploading with `ALPHA_MODES.NPM` instead. `AlphaBlend`'s exact compositing
    math was independently confirmed via a probe before being written as an assertion.
  - **Render targets** (frame 7, → 14/14): found via live `EM_JS console.log` tracing (not
    `page.evaluate()`, since the C++-side `RenderTarget2D` is destroyed before any post-hoc inspection
    could run) that `Clear()` on a render target painted nothing (`app.renderer.background` only
    affects the main canvas, never an explicit `render(container, {renderTexture})` call), and that
    even after fixing that, `GetData()` still read back blank because the "force a render before
    reading" fix's default `clear:true` wiped out what `Clear()` had just painted. **Fixed**
    (`REMED-PIXIJS-5`): `Clear()` now paints a reusable tinted 1x1 sprite with `blendMode=NONE`, and
    all three render-texture-target `render()` calls pass `clear:false`.
  - **`SetSamplerFilter`/`SetSamplerAddressMode`** (frame 8, → 15/15): implemented for real, replacing
    the previous no-op stubs -- see "Important limitations" below for the real architectural boundary
    found while implementing wrap mode.
  - **`SpriteFont`/`DrawString`** (frame 9, → 16/16): confirmed, not assumed, to need zero
    renderer-specific code -- a one-glyph `SpriteFont` drawn via `DrawString` produced the glyph's
    exact atlas color at the requested position on the first try, since `DrawString`'s `pushSprite`
    funnels every glyph through the same `Draw()` overload frames 1-8 already exercised.
  - **`SetTransformMatrix`** (frame 10, → 19/19): implemented for real, replacing the previous
    throw -- the batch's 2D affine transform is composed with each sprite's own local placement
    matrix and applied via `PIXI.Transform.setFromMatrix`, matching FNA's own "transform applies
    after per-sprite local placement" contract. Composition math was confirmed correct via a
    standalone browser probe (identity/translate/scale cases) before the implementation was written;
    a real `Matrix::CreateTranslation(16,16,0)` in the smoke test correctly shifts an entire scaled
    draw, with the untransformed origin confirmed back to background color.
  - **`Texture2D::SetData` on a `RenderTarget2D`** (frame 11, → 21/21): implemented for real,
    replacing the previous unconditional throw -- a throwaway buffer-backed texture is painted over
    the whole target with `PIXI.BLEND_MODES.NONE` (the same unconditional-overwrite trick
    `REMED-PIXIJS-5` already proved correct for `Clear()`), then discarded. Verified on an *unbound*
    render target, sampled back afterward as an ordinary texture.
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
- **Still open**: `NonPremultiplied` blend (shares `AlphaBlend`'s code path, no distinct test yet),
  and the generic-`BlendState` stretch goal remain unverified or
  unimplemented -- see `plan_pixijs.md`'s own "What remains" list for the current, precise picture.

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
- **Mip level > 0 texture uploads throw, now for an investigated reason** (`PIXIJS-31`) -- a live
  probe of `PIXI.BufferResource`/`PIXI.BaseTexture`'s own prototypes (2026-08-17) confirmed PixiJS
  exposes no public per-level CPU upload API at all; mipmaps are GPU-auto-generated from level 0
  only. This is a real structural boundary, same conclusion `CANVAS-21` reached, not an unresearched
  placeholder.
- **Custom `Effect` support throws** (`PIXIJS-47`) -- unlike `CANVAS`/`HTML_DOM`, this is *not* a
  structural boundary (PixiJS has a real GLSL shader stage via `PIXI.Filter`/`PIXI.Shader`); it is
  simply out of this plan's v1 scope.
- **`ApplyBlendState` only supports the 4 standard presets** (`PIXIJS-50`/`PIXIJS-51`) -- the 3
  presets this renderer's own smoke test exercises (`Opaque`, `AlphaBlend`, `Additive`) are verified
  with correct compositing math; `NonPremultiplied` shares `AlphaBlend`'s code path but has no test
  distinguishing it yet. A fully generic mapping via on-demand custom PixiJS blend-mode registration
  is believed straightforward (PixiJS exposes `renderer.state.blendModes` as a real, extensible GL
  blend-factor table) and is tracked as a stretch goal (`PIXIJS-52`), not assumed free.
- **`TextureAddressMode`/sampler filtering are implemented and verified, with a real architectural
  boundary** (`PIXIJS-46`/`PIXIJS-53`) -- `SetSamplerFilter`/`SetSamplerAddressMode` genuinely set
  `PIXI.SCALE_MODES`/`PIXI.WRAP_MODES` on the sampled `baseTexture` (confirmed live). But PixiJS's
  `Texture` constructor rejects any per-draw frame rectangle larger than its base texture
  (`"frame does not fit inside the base Texture dimensions"`, confirmed via a live probe), which is
  exactly what XNA's classic "oversized source rect tiles under `TextureAddressMode.Wrap`" trick
  needs -- so wrap mode is real, but can currently only affect the subtler linear-filter edge-bleed
  case through this renderer's per-draw-Texture-view architecture, never large-scale visible tiling
  within one `Draw()` call. A `PIXI.TilingSprite`-based draw path would be needed for that, and is out
  of this v1 scope.
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
