# PixiJS graphics renderer

## Status

`PIXIJS` renders XNA `SpriteBatch`/`Texture2D`/`SpriteFont`/`RenderTarget2D` output through
[PixiJS](https://pixijs.com/) v7 — `PIXI.Sprite` objects rendered by PixiJS's own WebGL batch
renderer, rather than raw GL calls (`WEBGL2`), Canvas2D (`CANVAS`), or DOM elements
(`HTML_DOM`/`SVG_DOM`). It is **Emscripten-only** and **2D-only in its v1 scope**.

It is CNA's newest renderer and still the least broadly exercised, but it is no longer unverified:
its draw path is built, run and pixel-checked in a real browser on every change.

**Current verification, 2026-08-17.** Reproduce it with the commands in the next section.

| | Result |
|---|---|
| `emcmake` configure, `-DCNA_GRAPHICS_RENDERER=PIXIJS` | passes |
| `cna_renderer_pixijs`, `cna_test_pixijs_smoke` (Release, `-O3`) | build and link |
| Browser pixel suite, headless Chromium over local HTTP | **66/66 checks pass** |
| `cna_test_pixijs_host` (native GTest, no browser) | **28/28 pass** |
| Multi-renderer configure `PIXIJS;CANVAS;HTML_DOM;SVG_DOM` | passes; `cna_demo_renderer_selection` links |
| `scripts/check_renderer_identities.py` | passes, including the runtime-registry arm |

**Not covered by any of that**, and therefore not claimed:

- No **manual** browser pass and no `emrun`/real-device run. Every browser result above comes from
  headless Chromium on SwiftShader — a real browser and a real WebGL implementation, but one
  machine, one GPU stack, and no human looking at the screen.
- No 3D pipeline, no custom `Effect`, no MRT, no mip level > 0 — see "Important limitations".
- The shared Emscripten `CnaTests` target still cannot link (`-sASYNCIFY=1` is incompatible with
  `-fwasm-exceptions`; reproduced identically under two emsdk releases). That is a cross-renderer
  toolchain gap, not a PixiJS one, and `cna_test_pixijs_host` covers this renderer's
  browser-independent contracts without waiting on it.

`plan_pixijs.md` carries the task-by-task breakdown and the full history, including the bugs found
along the way and how each was diagnosed.

## Building and running it

```bash
source $EMSDK/emsdk_env.sh
emcmake cmake -S . -B cmake-build-pixijs -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCNA_GRAPHICS_RENDERER=PIXIJS \
  -DCNA_PIXIJS_ROOT=/absolute/path/to/pixi.min.js
cmake --build cmake-build-pixijs -j4 --target cna_renderer_pixijs cna_test_pixijs_smoke

# The pixel suite needs a real browser: PixiJS wants a DOM and a WebGL context, and the platform's
# video subsystem fails to start without one, so `node` alone cannot run it.
node scripts/run_pixijs_browser_tests.mjs cmake-build-pixijs
```

The browser-independent contracts build and run natively, with no Emscripten toolchain at all:

```bash
cmake -S . -B cmake-build-debug -G Ninja -DCNA_BUILD_TESTS=ON -DCNA_BUILD_PIXIJS_HOST_TESTS=ON
cmake --build cmake-build-debug --target cna_test_pixijs_host -j4
ctest --test-dir cmake-build-debug -R PixiJsHostContracts
```

CNA vendors a pinned **PixiJS v7.4.2** UMD build (`plan_pixijs.md` Design decisions 3–4,
`cmake/ThirdPartyPixiJS.cmake`). `CNA_PIXIJS_SHA256` is pinned to the real
`pixi.js@7.4.2` `dist/pixi.min.js`, and that pin was re-verified against a fresh `npm pack` download
on 2026-08-17. `-DCNA_PIXIJS_AUTO_DOWNLOAD=ON` (the default) fetches it from jsDelivr; pass
`CNA_PIXIJS_ROOT` instead when you already have a local copy or the CDN is unreachable.

The bundle is linked with **`--extern-pre-js`**, not `--pre-js`. `--pre-js` content is handed to
Emscripten's JS optimizer, which at `-O2`/`-O3` dead-code-eliminates parts of a third-party bundle:
a real `-O3` build died with `ReferenceError: Pp is not defined` before any renderer code ran.

## Architecture notes

### Submission model — this renderer commits at every submission point

PixiJS is a retained scene graph; XNA's `SpriteBatch` is not. After `End()` returns, that batch's
sprites are logically *in* the target, in submission order, and the `Texture2D` objects they sampled
may legally be destroyed.

So a flush is not a scene-graph update: `CNA_PixiJs_FlushSprites` fills one scratch `PIXI.Container`
from the pooled sprites of exactly one flush, renders it into the active target with `clear:false`,
and empties it again. `End()` rasterizes; in `SpriteSortMode::Immediate`, each `Draw()` rasterizes.

This is what makes the renderer obey the contract rather than approximate it: batches accumulate in
submission order, a render-target switch cannot move another target's content (PixiJS re-parents on
`addChild`), per-batch blend and sampler state cannot be rewritten after the fact, and readback sees
everything submitted before it with no "force a render first" correction. Object pooling
(`plan_pixijs.md` Design decision 7) is unaffected — the pool only has to cover the largest single
flush.

`Clear()` is the same whole-target unconditional overwrite for the back buffer and for a bound
render target, which is what makes it a real ordering boundary.

### Blend state

Every `BlendState` — preset or not — is rendered from its **literal XNA blend factors and
equations**, resolved by `XnaBlendToGlFactor`/`XnaBlendFunctionToGlEquation` and applied through a
dedicated `renderer.state.blendModes` slot.

Two details are load-bearing, and each was a real bug:

- **One slot per distinct factor tuple.** PixiJS's `StateSystem` skips `setBlendMode()` when the
  incoming id equals the current one, so a single slot mutated in place leaves a second batch
  rendering with the first batch's blend.
- **An identity `utils.premultiplyBlendMode` entry per slot.** PixiJS rewrites `BLEND_MODES.NORMAL`
  to `NORMAL_NPM` — a *different* factor tuple — whenever the sampled texture is not premultiplied.
  That is why `BlendState::AlphaBlend` used to render as `BlendState::NonPremultiplied`.

`BlendState::Opaque` takes PixiJS's `BLEND_MODES.NONE` (no GL blending at all), which is
arithmetically identical to `(One, Zero)` and the path PixiJS optimizes.

`BlendState.BlendFactor` reaches WebGL's `gl.blendColor`, so `Blend::BlendFactor` and
`Blend::InverseBlendFactor` are real. `BlendState.ColorWriteChannels` reaches `gl.colorMask`. Both
are captured per batch at `Begin()` and applied immediately before the render that consumes them.

### Alpha conventions

Textures upload with `PIXI.ALPHA_MODES.NPM`, which means two things at once: the bytes CNA supplied
reach the GPU unmodified, and PixiJS packs a **straight** vertex tint rather than premultiplying it
— which is exactly XNA's own `SpriteBatch` tint semantics.

Combined with literal blend factors, this makes both of XNA's conventions work, and the choice
belongs where XNA puts it: in the pixel data plus the `BlendState` the application selects.
`BlendState::AlphaBlend` composites premultiplied content correctly; `BlendState::NonPremultiplied`
composites straight-alpha content correctly; the same colour expressed in either convention
composites to the same result under its own preset, and visibly differently under the other. The
browser suite asserts all three.

### Sampler state

`SetSamplerFilter` maps `TextureFilter` to `PIXI.SCALE_MODES` and `SetSamplerAddressMode` maps
`TextureAddressMode` to real `PIXI.WRAP_MODES` GL values, applied to the sampled base texture
immediately before the flush that uses them. Because the flush rasterizes before returning, two
batches drawing the same texture with different sampler states each get their own.

An out-of-range `TextureFilter` or `TextureAddressMode` is rejected rather than defaulting.

### Resource ownership and lifetime

- The `<canvas>` belongs to the platform, and so does its WebGL context. The `PIXI.Application` is
  therefore scoped to the **canvas** and kept on `Module['cnaPixiApp']`.
- Everything the renderer creates — textures, render textures and their cached frame views, the
  sprite pool, the scratch container, the clear sprite, the active-target selection — is scoped to
  the **renderer** and kept on `Module['cnaPixi']`. `~PixiJsRenderer` releases exactly that.
- The application is deliberately **not** destroyed with the renderer. A canvas hands out one WebGL
  context and PixiJS's `Renderer.destroy()` loses it on purpose, so tearing the application down
  leaves the platform's canvas permanently unusable: a second `PIXI.Application` on it fails inside
  PixiJS's batch setup (`Invalid value of 0 passed to checkMaxIfStatementsInShader`). Verified by
  doing exactly that in a browser. Destroying a `GraphicsDevice` and creating another one therefore
  works, with no renderer state carried across.
- A `RenderTarget2D` destroyed while still bound restores the back buffer and logs a warning; a
  destructor cannot throw, and continuing to render into a freed framebuffer is worse than saying so.

### Initialization and resize

The `PIXI.Application` is created in the `PixiJsRenderer` constructor, and every public operation
ensures it. An operation that cannot reach PixiJS throws a real, propagated error rather than
logging and returning — so a first frame that draws without `Clear()`, or binds a render target as
its very first operation, works instead of silently losing the draw.

`OnSurfaceChanged` calls `app.renderer.resize()`, which re-establishes PixiJS's projection and GL
viewport. Without it a resized canvas keeps rendering at the size the application was created with.

### JS interop

Exclusively `EM_JS`, `CNA_PixiJs_*`-prefixed, mirroring `CANVAS`/`HTML_DOM`'s convention — no
`embind`, no `emscripten::val`. Every entry point returns a status the C++ side turns into an
exception.

`SpriteBatch::Draw()` appends a 14-word (56-byte) POD `DrawCommand` to a C++ `std::vector`; `End()`
hands the whole array to one `EM_JS` call that walks it via `HEAP32`/`HEAPF32`/`HEAPU32`. A
2000-sprite batch costs one wasm→JS crossing, not 2000 — the same optimization
`plan_html_dom.md` Design decision 5 established for its own pooled `<div>` elements. Per-draw
`PIXI.Texture` frame views are cached on their registry entry rather than allocated per draw.

### Why PixiJS rather than `WEBGL2` directly

`PIXIJS` sits much closer to `WEBGL2` in raw capability than to `CANVAS`/`HTML_DOM` — it *is* a
WebGL renderer. The value is a higher-level retained `Sprite`/`Container`/`RenderTexture` API this
renderer drives directly instead of hand-rolling vertex-buffer batching the way
`EasyGLSpriteBatchRenderer.cpp` does, plus the full `BlendState`/wrap-mode fidelity
`CANVAS`/`HTML_DOM` structurally cannot reach. It is **not** a lower-cost alternative to `WEBGL2`:
PixiJS re-renders what it is asked to render, so a static frame is not free the way `HTML_DOM`'s is.
See `plan_pixijs.md`'s comparison table.

## Important limitations

Each is tracked by a `PIXIJS-N` task in `plan_pixijs.md`.

- **No 3D pipeline** (`PIXIJS-70`) — a deliberate v1 scope line, not (unlike `CANVAS`/`HTML_DOM`) a
  structural ceiling: PixiJS's `Mesh`/`Geometry` API could carry arbitrary vertex data. Reaching XNA
  3D parity through it is a `WEBGL2`/`VULKAN`-scale project of its own. Every 3D entry point routes
  through the shared `ThrowNo3D` convention.
- **Custom `Effect` throws** (`PIXIJS-47`) — also not a structural boundary; PixiJS has a real GLSL
  shader stage (`PIXI.Filter`/`PIXI.Shader`). Out of v1 scope.
- **MRT throws** (`PIXIJS-35`) — `SetRenderTargets` rejects any count above one. Consequently
  `BlendState.ColorWriteChannels1/2/3` describe outputs this renderer never binds: they are
  inapplicable rather than silently dropped, and slot 0 is honoured for real.
- **`BlendState.MultiSampleMask` is accepted only while coverage sample 0 is enabled** (`PIXIJS-89`)
  — targets here are single-sample, so any such mask is exactly equivalent to the all-ones default.
  A mask that disables sample 0 has no single-sample meaning and is rejected.
- **Mixed `AddressU`/`AddressV` is rejected** (`PIXIJS-90`) — a `PIXI.BaseTexture` carries one
  `wrapMode` for both axes. Rejecting is deliberate: it previously stored both and applied only
  `AddressU`, rendering a sampler state the caller never asked for.
- **`TextureAddressMode::Wrap` cannot tile within one `Draw()`** (`PIXIJS-46`) — the wrap mode
  genuinely reaches the WebGL sampler, but PixiJS's `Texture` constructor rejects a per-draw frame
  rectangle larger than its base texture (`"frame does not fit inside the base Texture dimensions"`),
  which is what XNA's classic oversized-source-rect tiling needs. Wrapping therefore affects
  linear-filter edge bleed, not visible large-scale tiling. A `PIXI.TilingSprite` draw path would be
  needed, and is out of v1 scope.
- **Mip level > 0 uploads throw** (`PIXIJS-31`) — a live probe of `PIXI.BufferResource`/
  `PIXI.BaseTexture` confirmed PixiJS exposes no per-level CPU upload API; mipmaps are
  GPU-generated from level 0 only. Same structural conclusion `CANVAS-21` reached.
- **No depth or stencil** (`PIXIJS-34`) — `SupportsDepthStencil()` is `false` and render targets
  carry no depth attachment in this scope.
- **No occlusion queries, `Texture3D`, `TextureCube` or `RenderTargetCube`** (`PIXIJS-71`) — the
  shared `IGraphicsRenderer` `nullptr` defaults, same as `CANVAS`/`HTML_DOM`.
- **Emscripten only** — hard `FATAL_ERROR` gate in `cmake/RendererSelection.cmake`, and `PIXIJS` is
  in the Emscripten platform partition in `cmake/RendererCombinations.cmake`, so a cross-platform
  combination is refused at configure time with a reason.
- **No CDN-loaded PixiJS at build or run time** — vendored and checksum-pinned only.
