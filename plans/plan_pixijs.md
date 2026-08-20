# PixiJS Graphics Renderer — Implementation Plan

> **Status legend** (this project's own convention): ✅ implemented *and verified against its stated
> acceptance criteria*; 🟨 code or documentation exists but has not met those criteria; ⬜ not
> implemented.

## Current status — 2026-08-17

`PIXIJS` is CNA's 2D-only, Emscripten-only PixiJS renderer. Its draw path builds under a real
Emscripten toolchain and is pixel-verified in a real browser on every change.

| Evidence | Result |
|---|---|
| `emcmake` configure, `-DCNA_GRAPHICS_RENDERER=PIXIJS` | passes |
| `cna_renderer_pixijs`, `cna_test_pixijs_smoke` (Release, `-O3`) | build and link |
| Browser pixel suite (`scripts/run_pixijs_browser_tests.mjs`, headless Chromium over local HTTP) | **70/70 checks pass** |
| `cna_test_pixijs_host` (native GTest, `CNA_BUILD_PIXIJS_HOST_TESTS=ON`, ctest `PixiJsHostContracts`) | **28/28 pass** |
| Multi-renderer `PIXIJS;CANVAS;HTML_DOM;SVG_DOM` (Emscripten) | configures; `cna_demo_renderer_selection` links |
| `PIXIJS` non-default in `CANVAS;PIXIJS` | configures, and `--extern-pre-js` still reaches the link line |
| `PIXIJS + DIRECTX11` / `PIXIJS + METAL` | rejected at configure time with a complete reason |
| `scripts/check_renderer_identities.py`, `check_renderer_combinations.py`, the five platform gates | pass |
| `cna_demo_2d` under `PIXIJS` in a browser | renders and **displays** a real animated scene -- hundreds of rotated, scaled, alpha-blended sprites over a cleared background |

**What is still not verified**, and is therefore not claimed anywhere in this plan:

1. **No manual browser pass, and no `emrun`/real-device run** (`PIXIJS-82`). Every browser result
   above comes from headless Chromium on SwiftShader — a real browser and a real WebGL
   implementation, but one machine, one GPU stack, and no human looking at the screen. The
   10-item-equivalent manual checklist `docs/canvas-backend.md` describes has not been written or run.
2. **The shared Emscripten `CnaTests` target still cannot link** — `-sASYNCIFY=1` is incompatible
   with `-fwasm-exceptions`, and `wasm-opt --asyncify` crashes (`UNREACHABLE executed at
   .../Flatten.cpp:231`). Reproduced identically under two different emsdk releases, so it is a real
   flag-combination incompatibility rather than toolchain drift. It affects every renderer, not this
   one; `cna_test_pixijs_host` covers this family's browser-independent contracts without it.
3. **The `CNA_PIXIJS_SHA256` pin has never been checked against jsDelivr itself.** It is computed
   from, and reproduced against, the real `pixi.js@7.4.2` npm package; both sessions that did so had
   `cdn.jsdelivr.net` blocked by an outbound proxy. See `cmake/ThirdPartyPixiJS.cmake`'s own note.

### What this session changed, and why

The renderer had a working-looking draw path built on one unsound premise: that PixiJS's retained
scene graph could stand in for `SpriteBatch`. Pooled sprites were parented to the active container
and only painted by `Present()`. Every state, ordering and lifetime defect below followed from that
single decision, and the 23-check suite that existed could not have caught any of them, because it
never left one `Begin`/`End` pair per frame.

**PIXIJS-87 — commit at every submission point.** One scratch `PIXI.Container` is filled from the
sprite pool for exactly one flush, rendered into the active target with `clear:false`, and emptied.
`End()` rasterizes; in `SpriteSortMode::Immediate`, each `Draw()` does. Confirmed in a browser
first that separate `render(..., {clear:false})` calls genuinely accumulate. This fixed, together:

- a second Deferred `Begin`/`End` in one frame restarting the pool at index 0 and overwriting the
  first batch;
- `SpriteSortMode::Immediate` keeping only the last sprite of a batch;
- binding another render target moving pooled nodes out of the previous target (PixiJS re-parents on
  `addChild`);
- blend and sampler state from a later batch applying to earlier, still-unpainted sprites;
- a `Texture2D` destroyed after `End()` — which `SpriteBatch` explicitly permits, since it releases
  its texture references there — taking its GPU resource with it before anything sampled it;
- `Clear()` needing to be two different operations for the back buffer and a render target, and
  dropping the active container's children as a "reset the frame" step that could discard another
  batch's sprites. It is now one whole-target overwrite, and a real ordering boundary.

Object pooling (Design decision 7) survives intact: the pool only has to cover the largest single
flush. The `REMED-PIXIJS-1` and `REMED-PIXIJS-5` readback corrections are gone with the problem they
worked around — nothing is ever left unpainted, so a read is just a read.

**PIXIJS-51 — the premultiplied-alpha gap, resolved.** The root cause was not `ALPHA_MODES` at all.
Presets were mapped onto `PIXI.BLEND_MODES`, and PixiJS rewrites `NORMAL` to `NORMAL_NPM` — a
*different* factor tuple — through `utils.premultiplyBlendMode` whenever the sampled texture is not
premultiplied. So `BlendState::AlphaBlend` was silently rendered with `BlendState::NonPremultiplied`'s
factors, and the two presets were indistinguishable by construction.

Every blend state now renders from its literal `XnaBlendToGlFactor`/`XnaBlendFunctionToGlEquation`
factors through its own `blendModes` slot, with an identity `premultiplyBlendMode` entry so PixiJS
cannot rewrite it. Combined with `ALPHA_MODES.NPM` uploads — which also make PixiJS pack a
*straight* vertex tint, exactly XNA's tint semantics — both of XNA's conventions now work, and the
convention lives where XNA puts it: in the pixel data plus the `BlendState` the application picks.
The suite asserts the same colour in either convention compositing to the same result under its own
preset, and visibly differently under the other. `AlphaBlend` on straight-alpha content now produces
the literal `(One, InverseSourceAlpha)` answer every other CNA renderer gives, rather than the
`NonPremultiplied` answer it used to.

**PIXIJS-86 — `--pre-js` was corrupting the vendored bundle.** `--pre-js` content is handed to
Emscripten's own JS optimizer, which at `-O3` dead-code-eliminated part of `pixi.min.js`: a real
optimized build died with `ReferenceError: Pp is not defined` before any renderer code ran. This was
invisible before because the only previous Emscripten build of this renderer was unoptimized.
`--extern-pre-js` is emitted verbatim, which is the contract a third-party UMD bundle needs.

**PIXIJS-88/89 — state that was silently discarded.** `SetBlendFactor` now reaches `gl.blendColor`,
so `Blend::BlendFactor`/`InverseBlendFactor` are real rather than a partial mapping.
`BlendState.ColorWriteChannels` reaches `gl.colorMask` instead of being ignored; a
`MultiSampleMask` that disables coverage sample 0 is rejected, since single-sample targets have no
equivalent for it, while every mask that leaves sample 0 enabled is genuinely equivalent to the
default. Both are captured per batch at `Begin()` and applied immediately before the render that
consumes them.

**PIXIJS-87 — one blend slot per distinct tuple.** PixiJS's `StateSystem` skips `setBlendMode()`
when the incoming id is unchanged, so the previous single reserved slot, mutated per flush, left a
second batch rendering with the first batch's blend. Confirmed empirically before the fix.

**PIXIJS-90 — sampler state that lied.** `SetSamplerAddressMode` stored both axes and applied only
`AddressU`; a mixed pair is now rejected, because a `PIXI.BaseTexture` carries one `wrapMode` and
approximating silently hands back a state the caller never asked for. Out-of-range `TextureFilter`
and `TextureAddressMode` values are rejected rather than defaulting to Point/Clamp.

**PIXIJS-91 — initialization no longer depends on `Clear()`.** The application is created in the
constructor and ensured by every entry point, and every `EM_JS` call returns a status the C++ side
turns into a real exception. A first frame that draws without `Clear()`, or binds a render target as
its first operation, used to be discarded silently by `console.error(...); return;`.

**PIXIJS-92 — ownership, corrected by a test that failed.** The first teardown implementation
destroyed the `PIXI.Application`. Creating a second one then failed inside PixiJS's batch setup
(`Invalid value of 0 passed to checkMaxIfStatementsInShader`), because a canvas hands out exactly
one WebGL context and PixiJS's `Renderer.destroy()` loses it on purpose. The application is
therefore scoped to the platform's canvas (`Module['cnaPixiApp']`) and everything the renderer
creates is scoped to the renderer (`Module['cnaPixi']`); teardown releases the second and keeps the
first, which is what makes destroy/recreate work with no state carried across. A `RenderTarget2D`
destroyed while still bound restores the back buffer and warns.

**PIXIJS-93/94.** `OnSurfaceChanged` resizes PixiJS's renderer, which was never told about a
drawable-size change. Per-draw `PIXI.Texture` frame views are cached on their registry entry rather
than allocated per draw — the churn the sprite pooling existed to avoid.

**Outside this renderer.** `scripts/check_renderer_identities.py` compared only the enum and the
cmake `STRINGS` list, which is why it reported all 49 identities as fine while
`cna_renderer_identity_to_namespace("PIXIJS")` was a hard configure error. It now follows every
identity through `cmake/RendererRegistry.cmake` to the C++ accessor and family-scoped factory that
must back it, and was negative-tested against both failure shapes.
`_cna_reject_combination` took its reason as a named parameter while every call site writes it as
several adjacent string literals — which CMake passes separately — so every combination rejection
was truncated to its first fragment.

<details>
<summary>How this renderer got here — the earlier sessions, in order (click to expand)</summary>

1. **Authored 2026-08-16** per direct task instruction, with no live owner design conversation; the
   design decisions below are this plan's own proposal. The authoring session had **no Emscripten
   SDK at all**, so nothing was compiled or run.
2. **PIXIJS-1** — pinned the real PixiJS v7.4.2 SHA256 via `npm pack` (`cdn.jsdelivr.net` was
   blocked by the sandbox proxy; `registry.npmjs.org` was not).
3. **PIXIJS-84** — a later session installed `emsdk` and built `cna_renderer_pixijs` and
   `cna_test_pixijs_smoke` for real. The first build found a real bug:
   `Vector2::getZeroProperty()` does not exist (`Vector2::Zero` does).
4. Running the result under plain `node` reproduced `CANVAS-15`'s finding exactly — the platform's
   video subsystem fails before any renderer code runs, because Node has no DOM. Confirms the
   "needs a real browser" boundary for `PIXIJS` empirically rather than by analogy.
5. **First real browser run: 3/5.** Both pixel checks failed. Diagnosed live in the page: the
   upload, sprite math and readback mechanism were each already correct, but nothing painted until
   `Present()`. `REMED-PIXIJS-1` made both readbacks force a render first — the first sign of the
   retained-mode mismatch PIXIJS-87 later removed at the root.
6. Grown to **23/23** across several rounds, each finding a real bug via live-browser probing
   before writing the corresponding assertion: `REMED-PIXIJS-2` (flip via GroupD8 `rotate`, not
   negative scale, which shifted the destination rectangle), `REMED-PIXIJS-3` (`Opaque` collapsed
   onto `NORMAL`), `REMED-PIXIJS-4` (`ALPHA_MODES.UNPACK` is `PREMULTIPLY_ON_UPLOAD`),
   `REMED-PIXIJS-5` (render-target `Clear`/readback). Also implemented `SetTransformMatrix`
   (PIXIJS-45), `Texture2D::SetData` on a render target (PIXIJS-32), real sampler state
   (PIXIJS-46/53), and generic `BlendState` (PIXIJS-52); confirmed `SpriteFont` needs no
   renderer-specific code (PIXIJS-60); and established that PixiJS has no per-level CPU mip upload
   API (PIXIJS-31).
7. **Post-merge platform migration.** The renderer was authored on a branch predating `next`'s
   platform-abstraction and runtime-renderer work and did not compile in the merged tree.
   `PixiJsRenderer` took `GraphicsRendererCreateArgs` and the platform-neutral
   `RendererSurfaceInfo`; `<SDL3/SDL.h>`, the `SDL_Window*` member and the `GetWindowInternal`/
   `GetRendererInternal`/`GetNativeTexture` overrides of already-deleted interface methods were
   removed; `PixiJsRendererDescriptor.cpp` and the `cmake/RendererRegistry.cmake` entry were added;
   the SDL ratchet returned to its 0/0 floor.
8. **`cna_test_pixijs_host`** was added so the pure-C++ contracts run without a browser — before it,
   `PixiJsRendererTests.cpp` had never compiled in *any* configuration, because the `pixijs` module
   only configured under `if(EMSCRIPTEN AND CNA_PIXIJS_JS_FILE)`.

</details>

Every ✅ below is tied to a specific, reproduced browser (or, for browser-independent logic, native
GTest) result, not to source existing.

---

## What this renderer is

`PIXIJS` renders XNA `SpriteBatch`/`Texture2D`/`RenderTarget2D` output through
[PixiJS](https://pixijs.com/) (`PIXI.Application`/`PIXI.Sprite`/`PIXI.RenderTexture`), a JavaScript
2D scene-graph library that itself renders via WebGL. It is CNA's **third browser-native 2D-only**
renderer, alongside `CANVAS` and `HTML_DOM`, and its fourth Emscripten-only one counting `SVG_DOM`.

The honest comparison to its three JS-interop siblings and to `WEBGL2` (EasyGL's WebGL2 profile,
the existing default Emscripten renderer):

| | `WEBGL2` (EasyGL) | `CANVAS` | `HTML_DOM` | `PIXIJS` (this) |
|---|---|---|---|---|
| Draw primitive | raw `glDrawArrays`, batching hand-rolled in `EasyGLSpriteBatchRenderer.cpp` | `ctx.drawImage()` into one `<canvas>` | one styled `<div>` per sprite | `PIXI.Sprite` in a retained scene graph; PixiJS's own internal batch renderer issues the GL calls |
| Who rasterizes | the GPU, via CNA's own batching code | Canvas2D, per frame, every sprite | the browser compositor, only what changed | the GPU, via PixiJS's own internal WebGL batch renderer |
| Cost of a static frame | full redraw (ordinary immediate-mode GL) | full redraw | **zero** — no JS, no repaint | full redraw — PixiJS re-renders every ticker frame; a retained scene graph does **not** imply free repaints the way DOM/CSS compositing does |
| Texture upload | direct `glTexImage2D` | `putImageData` (cheap) | PNG re-encode to a data URL (expensive) | direct GPU upload via a buffer-backed texture source (cheap, comparable to `WEBGL2`) |
| `TextureAddressMode::Wrap`/`Mirror` | native `gl.REPEAT`/`gl.MIRRORED_REPEAT` | emulated via `createPattern` (`CANVAS-44`) | not applicable (DOM has no native tiling) | **native** `gl.REPEAT`/`gl.MIRRORED_REPEAT`, same as `WEBGL2` |
| Custom `BlendState` | full `gl.blendFuncSeparate` generality | 4 fixed presets only, else throw | 4 fixed `mix-blend-mode` presets only, else throw | closer to `WEBGL2`'s full generality — see Design decision 6 |
| Custom `Effect` (shader) | real GLSL pipeline | none — throws unconditionally | none — throws unconditionally | PixiJS **has** a real shader stage (`PIXI.Filter`/`PIXI.Shader`) but mapping XNA's Effect model onto it is out of this plan's v1 scope — see Design decision 9 |

So `PIXIJS` is not "a fourth flavor of 2D-only browser drawing API" the way `CANVAS`/`HTML_DOM` are
relative to each other — it sits much closer to `WEBGL2` in raw underlying capability (it *is* a
WebGL renderer) but trades `WEBGL2`'s hand-rolled batching/vertex-buffer bookkeeping for a
higher-level retained-mode `Sprite`/`Container`/`RenderTexture` API that this renderer's
`ISpriteBatchRenderer`/`ITextureRenderer`/`IRenderTargetRenderer` implementations can drive far more
directly. The value proposition is developer/implementation leverage on top of the same underlying
GPU path `WEBGL2` already has, plus (per Design decision 6) a real shot at full `BlendState`
fidelity that `CANVAS`/`HTML_DOM` structurally cannot reach. It is **not** a lower-power/lower-cost
alternative to `WEBGL2` the way `CANVAS`/`HTML_DOM` are — regard it as a sibling implementation
strategy on the same WebGL foundation, not a strictly smaller one.

**Scope: 2D only**, matching `CANVAS`/`HTML_DOM`/`SVG_DOM`'s own scope decision (`SpriteBatch`,
`Texture2D`, `SpriteFont`, `RenderTarget2D`) — **not** because PixiJS is structurally incapable of
more (unlike Canvas2D/DOM, PixiJS's `Mesh`/`Geometry` API genuinely could carry arbitrary vertex
data), but because reaching XNA 3D parity (`GpuDrawParams`, stock effects, depth/stencil, the full
`IGraphicsRenderer` 3D draw surface) through PixiJS's mesh API is a project of `WEBGL2`/`VULKAN`
scale in its own right and is deliberately out of scope for this plan. This is recorded explicitly
so it is not silently reopened as "PixiJS could do 3D" without a real new plan and owner sign-off,
and so it is not confused with `CANVAS`/`HTML_DOM`'s **permanent** 2D-only ceiling (theirs is a
structural limit of the underlying browser API; this renderer's is a deliberate v1 scope line drawn
on top of a capability that is technically there).

**Emscripten-only**, hard-gated in CMake exactly like `CANVAS`/`HTML_DOM`/`SVG_DOM`: PixiJS is a
JavaScript library that expects a `document`/`HTMLCanvasElement`/WebGL context, none of which exist
in a native desktop build.

---

## Design decisions

**1. Emscripten-only, hard `FATAL_ERROR` gate.** Same shape as `CANVAS`/`HTML_DOM`/`SVG_DOM`'s own
gates in `cmake/RendererSelection.cmake`. Every `.cpp` file still compiles natively (`EM_JS` blocks
sit behind `#if defined(__EMSCRIPTEN__)`), so the pure-C++ logic (blend-mode mapping, address-mode
validation, transform/pivot math extracted as standalone functions per `CANVAS-80`'s own precedent)
stays unit-testable under plain GTest.

**2. Reuse the platform's existing `<canvas>` element — PixiJS does not create a second one.** Same
lookup `EasyGLGraphicsBackend.cpp`/`CanvasRenderer.cpp` already use
(`Module['canvas'] || document.querySelector('canvas')`). PixiJS's `Application` is created with
that element passed explicitly (`new PIXI.Application({ view: existingCanvas, ... })`) rather than
letting PixiJS create its own canvas, so the platform keeps owning window sizing, input and the
event pump exactly as it does for `WEBGL2`/`CANVAS`/`HTML_DOM`. The renderer itself sees only the
platform-neutral `RendererSurfaceInfo` snapshot and links no windowing library at all.

*Consequence, discovered the hard way (PIXIJS-92):* because the canvas is the platform's, so is its
WebGL context — a canvas hands out exactly one, and PixiJS's `Renderer.destroy()` loses it. The
`PIXI.Application` is therefore scoped to the canvas, not to the renderer, and renderer teardown
releases only what the renderer created. In a multi-renderer build several browser renderers can be
linked in together, so this also means the application must not be something one of them tears down
under the others.

**3. Pin PixiJS v7.x, vendored as a single UMD build, not fetched from a CDN at build or run time.**
Two sub-decisions, both existing-precedent-driven:
   - *Why vendor rather than CDN-`<script>`*: every other third-party dependency in this repo
     (`third_party/`, `vendor/`, `cmake/ThirdParty*.cmake`) is either a git submodule or a pinned,
     checksum-verified download performed by CMake at configure time (see `ThirdPartyWebGPU.cmake`'s
     `CNA_WEBGPU_ROOT`-or-auto-download shape) — never a runtime `<script src="https://...">` that
     would make the built game's behavior depend on a third-party CDN being reachable at play time.
     `cmake/ThirdPartyPixiJS.cmake` (Design decision 4) follows the identical root-path-or-
     auto-download pattern.
   - *Why v7, not the newer v8*: PixiJS v7's texture/resource model
     (`PIXI.BaseTexture`/`PIXI.BufferResource`/`PIXI.Texture.fromBuffer`-style construction) offers a
     direct, synchronous, buffer-in-GPU-texture-out upload path. v8 restructured this around an
     async-first `Texture.from()`/`Source` pipeline oriented at asset loading. XNA's
     `Texture2D.SetData`/`FromStream`/`RenderTarget2D` binding are all synchronous APIs (the same
     constraint `plan_canvas.md` Design decision 3 already had to solve for Canvas2D), and v7's
     resource model avoids re-solving that async-vs-synchronous problem a second time. Revisit only
     with a dedicated migration plan if a v8-only PixiJS feature becomes genuinely necessary.

**4. `cmake/ThirdPartyPixiJS.cmake`: `CNA_PIXIJS_ROOT` (a local pinned `pixi.min.js`) or
`CNA_PIXIJS_AUTO_DOWNLOAD` (a checksum-pinned download of the official `pixi.js@7` UMD release),
linked via Emscripten's `--pre-js`.** `--pre-js` (an `emcc`/`em++` linker flag, not a new build
step) prepends the given JS file verbatim into the generated glue code, ahead of every `EM_JS`
function this renderer declares — so `PIXI.*` is a real global by the time any
`CNA_PixiJs_*` function runs, with no dynamic `<script>`-tag injection or async load race. Mirrors
`ThirdPartyWebGPU.cmake`'s "prefer a pinned local root for reproducible/offline builds, fall back to
a verified auto-download" shape exactly.

**5. All JS interop via `EM_JS`, `CNA_PixiJs_*`-prefixed, following `CANVAS`/`HTML_DOM`'s exact
convention — no `embind`, no `emscripten::val`.** Every entry point returns a status the C++ side
turns into a real exception; a CNA API call must never be lost to a `console.error` and an early
return (PIXIJS-91). State lives on `Module`, split by lifetime (PIXIJS-92):
`Module['cnaPixiApp']` is the one `PIXI.Application`, scoped to the platform's canvas;
`Module['cnaPixi']` holds everything the renderer creates — the integer-id → texture registry
(mirroring `CanvasTextureRenderer`'s `Module['cnaTextures']`, and shared by plain textures and
render targets), the pooled `PIXI.Sprite` array, the scratch container every flush renders through,
the reusable clear sprite, the blend-mode slot map and the active-target selection.

**6. Blend state: start with the same 4 standard presets `CANVAS`/`HTML_DOM` support, but the
underlying capability genuinely supports more — real generic `BlendState` mapping is a tracked
stretch goal, not a permanent ceiling.** PixiJS ≥6.5 exposes `renderer.state.blendModes`, an
extensible table mapping a `PIXI.BLEND_MODES` value to a `[srcRGB, dstRGB, srcAlpha, dstAlpha]`
WebGL blend-factor tuple (registered via `renderer.state.blendModes[CUSTOM_ID] = [...]` using real
`gl.ONE`/`gl.SRC_ALPHA`/etc. constants) — the same blend-factor vocabulary
`SdlRenderer::ToSdlBlendFactor`/`EasyGLRenderer`'s own blend mapping already use. `Opaque`
(`One`/`Zero`), `AlphaBlend` (`One`/`InverseSourceAlpha`, assumes premultiplied source — same
un-premultiply story `CANVAS`/`HTML_DOM` already solved, controlled here via each texture's
`PIXI.ALPHA_MODES`), `NonPremultiplied` (`SourceAlpha`/`InverseSourceAlpha`) and `Additive`
(`SourceAlpha`/`One`) were the Phase P5 v1 scope, same boundary as the siblings.

*How this actually landed (PIXIJS-52/87/51), and it is not what this decision assumed.* The generic
path was implemented — and then became the ONLY path. Mapping presets onto `PIXI.BLEND_MODES` is
unsound, because PixiJS rewrites `NORMAL` to `NORMAL_NPM` (a different factor tuple) through
`utils.premultiplyBlendMode` for any non-premultiplied texture, which silently rendered
`BlendState::AlphaBlend` with `NonPremultiplied`'s factors. Every blend state now resolves to its
literal XNA factors and gets its own `blendModes` slot with an identity `premultiplyBlendMode`
entry. Two further details are load-bearing and were each a real defect: a **distinct** slot per
distinct tuple (PixiJS's `StateSystem` skips `setBlendMode()` when the id is unchanged, so one
mutated slot leaks the previous batch's blend), and `gl.blendColor` for the
`BlendFactor`/`InverseBlendFactor` factors (PIXIJS-88). `Opaque` still takes
`PIXI.BLEND_MODES.NONE`, which is arithmetically identical to `(One, Zero)` and the path PixiJS
optimizes.

**7. Pooled `PIXI.Sprite` objects, recycled across frames; `Draw()` batches into one `EM_JS` flush
per `SpriteBatch::End()`, not one call per sprite.** Two independent reasons, kept distinct on
purpose:
   - *Wasm/JS boundary cost* — identical reasoning to `plan_html_dom.md` Design decision 5: `Draw()`
     appends a fixed-stride POD command to a C++ `std::vector`; `End()` hands the whole array to one
     `EM_JS` call that walks it via `HEAP32`/`HEAPF32` and updates the sprite pool. A 2000-sprite
     frame costs one wasm→JS crossing, not 2000 — this is not optional, it is the same
     already-proven-necessary optimization, not a new idea.
   - *Object churn* — creating/destroying a `PIXI.Sprite` (and its underlying GPU state) every frame
     is real allocator/GC pressure PixiJS's own docs warn against; a pooled array indexed by
     "sprite *n* of this frame" (same prefix-activation shape `plan_html_dom.md` Design decision 3
     uses for its `<div>` pool) sidesteps it. Unlike `HTML_DOM`, this does **not** buy "zero cost for
     an unchanged frame" — PixiJS's ticker re-renders the whole stage regardless of whether any
     sprite property changed, so pooling here is purely an allocation-cost optimization, not a
     repaint-avoidance one. Do not describe it as the same win `HTML_DOM` gets.

**8. Textures: synchronous GPU upload via a buffer-backed `PIXI.BaseTexture`/`PIXI.Resource`, no
`Image`/`ImageBitmap`/async decode anywhere in this renderer's own code.** Same synchronous-API
requirement `plan_canvas.md` Design decision 3 solved for Canvas2D, solved here for PixiJS instead:
construct a `PIXI.BufferResource`-equivalent directly from the already-CPU-decoded RGBA8 bytes
(`Texture2D::FromStream`'s decode is shared, backend-agnostic CPU-side work, unaffected by this
renderer — confirmed by reading `Texture2D.cpp`, same finding `CANVAS-27` already made) and hand it
to `PIXI.BaseTexture`'s constructor synchronously; `UpdatePixels` mutates that buffer in place and
calls `baseTexture.update()`, also synchronous. `RenderTarget2D` uses `PIXI.RenderTexture.create()`
plus `app.renderer.render(container, { renderTexture })`.

**9. Readback via PixiJS's own `renderer.extract` API — genuinely synchronous, no `getImageData`
plumbing needed.** `app.renderer.extract.pixels(target)` (a `PIXI.RenderTexture`, `PIXI.Sprite`, or
omitted for the whole stage) returns a synchronous `Uint8Array`/`Uint8ClampedArray` of RGBA8 pixels
directly — PixiJS's own supported readback mechanism, not a workaround. `ReadBackbuffer`/
`IRenderTargetRenderer::GetData` route through this, mirroring the shape (not the mechanism) of
`CANVAS-25`.

**10. Custom `Effect` (arbitrary shader source): throws for v1, but this is explicitly a "not yet,"
not a permanent structural boundary — unlike `CANVAS`/`HTML_DOM`.** PixiJS has a real GLSL shader
stage (`PIXI.Filter`, custom `PIXI.Shader`/`PIXI.Program`), so a future phase mapping CNA's existing
GLSL-cross-compiled stock/custom effect sources onto it is plausible and should not be pre-judged
impossible the way it genuinely is for Canvas2D/DOM. Out of this plan's v1 scope regardless — same
`WEBGL2`/`VULKAN`-scale undertaking called out in the 2D-only scope note above.

**11. Occlusion queries: `CreateOcclusionQuery()` returns `nullptr`** (the `IGraphicsRenderer`
shared default) — PixiJS exposes no occlusion-query primitive, and there is no CPU-side
approximation worth building for a browser sprite renderer, same conclusion `CANVAS`/`HTML_DOM`
already reached for the same reason.

**12. Two test surfaces, because the renderer has two halves.** Every piece of logic that can be
isolated as a pure C++ function (blend-state classification, the `Blend`/`BlendFunction`/
`TextureFilter`/`TextureAddressMode` → GL mappings, the write-state decisions) is written as a
standalone function — same shape as `CanvasRenderer.cpp`'s `BlendStateToCompositeOp` — so
`cna_test_pixijs_host` can cover it natively, with no browser and no Emscripten toolchain. Anything
the draw path expresses is verified by reading real pixels back in a real browser
(`scripts/run_pixijs_browser_tests.mjs`).

The division is not a convenience: the defects Phase P9 fixed were **all** in the second half, and
none of them could have been caught by the first. A pure-function test cannot tell you that two
`Begin`/`End` pairs overwrite each other. Add browser checks in the same task that implements a
capability, and make them assert a value derived from XNA's own arithmetic rather than from what the
renderer currently produces.

---

## Source layout

```text
modules/renderers/pixijs/
  CMakeLists.txt
  include/CNA/Internal/Renderers/PixiJs/
    PixiJsRenderer.hpp
    PixiJsTextureRenderer.hpp
    PixiJsRenderTargetRenderer.hpp
    PixiJsSpriteBatchRenderer.hpp
  src/
    PixiJsRenderer.cpp
    PixiJsRendererDescriptor.cpp     # the family's runtime-registry descriptor + factory address
    PixiJsTextureRenderer.cpp
    PixiJsRenderTargetRenderer.cpp
    PixiJsSpriteBatchRenderer.cpp
  examples/
    CMakeLists.txt
    pixijs_smoke_test.cpp            # the browser pixel suite
  tests/CNA/Internal/Renderers/PixiJs/
    PixiJsRendererTests.cpp          # cna_test_pixijs_host, native, no browser

cmake/ThirdPartyPixiJS.cmake
scripts/run_pixijs_browser_tests.mjs
docs/pixijs-renderer.md
```

Class shape deliberately mirrors `CANVAS`'s four-file split (`Renderer`/`TextureRenderer`/
`RenderTargetRenderer`/`SpriteBatchRenderer`) — same interface surface to satisfy, same reason to
keep them separate.

---

## Original execution order — retained as a record of how the phases were sequenced

Phases P0-P8 are complete; Phase P9 below is this renderer's remaining work. The ordering
rationale is kept because it still explains why the code is shaped the way it is.

1. Phase P0 (vendoring + CMake integration) unblocks everything else, same role `plan_canvas.md`
   Phase C1 and `plan_html_dom.md`'s own CMake step played for their renderers.
2. Phase P1 (skeleton `PixiJsRenderer` implementing every `IGraphicsRenderer` pure virtual, 2D-only
   `ThrowNo3D` wiring) must land before any JS-touching phase, so every later phase is adding real
   behavior to an already-interface-complete class, not discovering missing overrides late.
3. Phase P2 (`PIXI.Application` acquisition on the existing canvas, `Clear`/`Present`, viewport) is
   the first genuinely PixiJS-specific phase — get it structurally right before building texture/
   draw machinery on top of it.
4. Phase P3 (textures + render targets: buffer-backed upload, `extract`-based readback) is the
   architectural core Design decisions 8-9 describe; Phase P4 depends on it.
5. Phase P4 (`SpriteBatch`/`Draw` path: pooled sprites, batched flush, rotation/origin/flip/tint via
   PixiJS's own native `Sprite` properties rather than hand-rolled transform math) is the actual
   point of this renderer.
6. Phase P5 (blend/sampler-state mapping, native wrap modes, the custom-blend-mode stretch goal)
   builds directly on P4.
7. Phase P6 (`SpriteFont`) should fall out of P3+P4 almost for free — confirm rather than assume,
   same discipline `CANVAS-50`/`HTMLDOM` used.
8. Phase P7 (`ThrowNo3D` completeness sweep across the full 3D surface, occlusion-query/custom-Effect
   defaults) can happen any time after P1 but must be complete before this renderer is considered
   feature-complete.
9. Phase P8 (tests + `docs/pixijs-renderer.md`) — add structural test coverage in the same task that
   implements each capability, not bolted on afterward, and do not claim any automated verification
   this plan's own status block says did not happen.

For every task: a ✅ requires a real Emscripten build, and for anything the draw path can express,
a real browser pixel result. Compiling is not verification.

---

## Phase P0 — Vendoring and CMake integration

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-1 | `cmake/ThirdPartyPixiJS.cmake`: `CNA_PIXIJS_ROOT` / `CNA_PIXIJS_AUTO_DOWNLOAD` (checksum-pinned `pixi.js@7` UMD download), following `ThirdPartyWebGPU.cmake`'s pattern (Design decisions 3-4) | 🟨 | `CNA_PIXIJS_ROOT` is exercised by every build in this plan. `CNA_PIXIJS_SHA256` is pinned and was independently reproduced from a fresh `npm pack pixi.js@7.4.2` (byte-identical). Stays 🟨 for one specific reason: the **jsDelivr auto-download path itself has still never run** -- `cdn.jsdelivr.net` was blocked by the outbound proxy in both sessions that touched this, so the pin is verified against the npm package rather than against the URL `cna_configure_pixijs()` fetches. |
| PIXIJS-2 | Add `"PIXIJS"` to `CNA_GRAPHICS_RENDERER`'s CMake `STRINGS` property and a matching `CNA_RENDERER_PIXIJS` option, following the exact existing pattern for `CANVAS`/`HTML_DOM`/`SVG_DOM` | ✅ | |
| PIXIJS-3 | Hard platform gate: `if(CNA_GRAPHICS_RENDERER STREQUAL "PIXIJS" AND NOT EMSCRIPTEN) message(FATAL_ERROR ...)`, mirroring `CANVAS`/`HTML_DOM`/`SVG_DOM`'s own gates (Design decision 1) | ✅ | |
| PIXIJS-4 | `cna_renderer_pixijs` target dispatch (`elseif(CNA_GRAPHICS_RENDERER STREQUAL "PIXIJS")` block in `cmake/RendererSelection.cmake`), including the link-flag wiring for the vendored PixiJS UMD build | ✅ | Verified end-to-end. **PIXIJS-86**: the flag is `--extern-pre-js`, not `--pre-js` -- `--pre-js` content goes through Emscripten's JS optimizer, which at `-O3` dead-code-eliminated part of `pixi.min.js` and killed the page with `ReferenceError: Pp is not defined` before any renderer code ran. Also verified in a multi-renderer build where `PIXIJS` is **not** the default (`CANVAS;PIXIJS`): the flag is `PUBLIC` on the family's own target, and per-identity dispatch runs for every selected identity, so it still reaches the link line. |
| PIXIJS-5 | `pixijs` added to `modules/CMakeLists.txt`'s `_cna_renderer_modules` physical-source-partition list | ✅ | Required or configure fails for *any* renderer selection, not just `PIXIJS` — verified via a native (`SDL_RENDERER`) configure in this session (Phase P0's only genuinely-run check). |
| PIXIJS-6 | `modules/renderers/pixijs/CMakeLists.txt`: `cna_add_renderer()` + the vendored-bundle link flag + `add_subdirectory(examples)`, mirroring `canvas`/`html-dom`'s own `CMakeLists.txt` | ✅ | Links no windowing library at all: the family consumes only the platform-neutral surface snapshot (post-merge platform migration, see the history above). |

## Phase P1 — Skeleton renderer

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-10 | `PixiJsRenderer : IGraphicsRenderer` — every pure virtual overridden; construction/destruction real, the inherently-3D-only surface wired to `HandleUnsupported3DCall`/`ShouldStubUnsupported3DResource` exactly like `CanvasRenderer`'s own Phase C1 bring-up | ✅ | Build-verified under Emscripten and natively, with the `ThrowNo3D` surface covered by `cna_test_pixijs_host`. Construction now also creates the `PIXI.Application` and destruction releases the renderer's own JS resources (PIXIJS-91/92). |
| PIXIJS-11 | Factory dispatch: `CreateGraphicsRenderer()` returns a `PixiJsRenderer` under `CNA_RENDERER_PIXIJS` | ✅ | |
| PIXIJS-12 | `SupportsDepthStencil()` → `false`; `SupportsCapability()` reports `AdditiveBlending` true (native `PIXI.BLEND_MODES.ADD`) and everything 3D-only false, matching Design decisions 1/6 | ✅ | |

## Phase P2 — `PIXI.Application`, Clear/Present, viewport

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-20 | `EM_JS` acquisition of the existing DOM `<canvas>` (Design decision 2) and `new PIXI.Application({ view, ... })` construction, cached on `Module['cnaPixiApp']` | ✅ | Verified in a real headless-Chromium browser run, 2026-08-17: `Module['cnaPixiApp']` is real, sized correctly, and renders. |
| PIXIJS-21 | `Clear(r,g,b,a)`: `app.renderer.background.color`/`app.renderer.clear()` — PixiJS's own clear-color API, no manual `fillRect` equivalent needed | ✅ | Verified in the same browser run — `CornflowerBlue` background visible at unswept pixels. |
| PIXIJS-22 | `Present()`: decided and implemented — PixiJS's own ticker is disabled (`autoStart:false`/`sharedTicker:false`); `Present()` calls `app.renderer.render(...)` explicitly, keeping CNA's `Game` loop authoritative, consistent with every other renderer's `Present()` contract | ✅ | Verified 2026-08-17, with a real correction found and fixed: `GetBackBufferData` (callable before `Present()`) needed its own force-render too — `REMED-PIXIJS-1`, see the plan's own dated update. |
| PIXIJS-23 | `GetViewportSize()`/`SetVirtualResolution()`/`SetPresentationMode()`/`TransformWindowToLogical`/`TransformLogicalToWindow`: reuse the identical backend-agnostic logical-resolution math `CanvasRenderer`/`EasyGLRenderer` already share — only the physical-size query (`SDL_GetWindowSize`) is backend-specific | ✅ | Verbatim port of `CanvasRenderer`'s `getLogicalSize`/transform math — this part needed no PixiJS-specific code at all, confirmed by reading `CanvasRenderer.cpp` directly rather than assumed. |

## Phase P3 — Textures and render targets

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-30 | `PixiJsTextureRenderer : ITextureRenderer` — buffer-backed `PIXI.BaseTexture`/`PIXI.Texture`, registered by integer id in `Module['cnaPixi'].textures` (Design decision 8) | ✅ | Verified 2026-08-17: uploaded 2x2 RGBA8 pixels sampled back byte-for-byte correct through a real WebGL draw. |
| PIXIJS-31 | `UpdatePixels`/`UpdatePixelsLevel`: in-place buffer mutation + `baseTexture.update()`; mip level>0 policy | ✅ | level=0 path verified (frames 1-11). level>0 policy investigated for real 2026-08-17 (not left provisional): a live browser probe of `PIXI.BufferResource`'s prototype (only `upload`/`dispose`) and `PIXI.BaseTexture`'s (only a `mipmap` on/off flag, no per-level hook) confirmed PixiJS has **no public API** for a custom CPU-authored mip chain — mipmaps are GPU-auto-generated from level 0 only (`gl.generateMipmap`). Throws for level>0, same behavior as before, but now for an investigated, documented structural reason matching `CANVAS-21`'s own conclusion, not a "haven't looked into it" placeholder. |
| PIXIJS-32 | `PixiJsRenderTargetRenderer : IRenderTargetRenderer` — `PIXI.RenderTexture.create()` + `Bind/UnbindAsRenderTarget` switching which target `app.renderer.render(...)` calls target (Design decision 8) | ✅ | Bind/Clear/draw/readback round-trip verified 2026-08-17 (frame 7). Direct `Texture2D::SetData` (`UpdatePixels`) implemented and verified 2026-08-17 (frame 11, 21/21): paints a throwaway buffer-backed texture over the whole target with `PIXI.BLEND_MODES.NONE` (the same unconditional-overwrite trick `REMED-PIXIJS-5` proved correct for `Clear()`), then discards it -- confirmed on an *unbound* render target, sampled back as an ordinary texture. |
| PIXIJS-33 | `ReadBackbuffer`/render-target `GetData`: `app.renderer.extract.pixels(...)` (Design decision 9) | ✅ | `ReadBackbuffer` (main stage) verified 2026-08-17, after `REMED-PIXIJS-1`'s force-render fix. `PixiJsRenderTargetRenderer::GetData` independently verified in the same session (frame 7's own `rt.GetData()` check) after its own `REMED-PIXIJS-5` clear/readback fix — both paths now proven. |
| PIXIJS-34 | `HasRealDepthBuffer()` → `false` (no depth attachment on a 2D sprite-only `RenderTexture` in this renderer's v1 scope) | ✅ | Trivial override, same confidence level as `CANVAS-23`. |
| PIXIJS-35 | `SetRenderTargets` with 2+ bindings (MRT) → throw, same conclusion `CANVAS-26`/`HTML_DOM` reached (a single `PIXI.Application`'s default render pipeline targets one `RenderTexture` at a time in this renderer's v1 scope) | ✅ | Implemented in `PixiJsRenderer::SetRenderTargets`. |

## Phase P4 — `SpriteBatch`/`Draw` path

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-40 | `PixiJsSpriteBatchRenderer : ISpriteBatchRenderer`; pooled-sprite `Begin()`/`End()` with the single-flush `EM_JS` batching (Design decisions 5/7) | ✅ | 14-word packed `DrawCommand`, one `CNA_PixiJs_FlushSprites` crossing per `End()` (or per `Draw()` in immediate mode). **PIXIJS-87** replaced the retained parenting with commit-on-submit; pooling is unchanged and still one crossing per flush. Verified: two Deferred batches in one frame with a correct overlap order, three Immediate draws all painted, and Deferred-then-Immediate ordering. |
| PIXIJS-41 | Basic `Draw(texture, x, y)` / `Draw(texture, destRect, srcRect, color)` via a per-draw `PIXI.Texture` view (shared `baseTexture`, distinct `frame`) assigned to a pooled sprite | ✅ | Both overloads pixel-verified: the scaled `Draw(destRect, srcRect, color)` form across rotation, flip, blend, sampler, transform and render-target variants, and the unscaled `Draw(texture, x, y)` form placing the whole texture 1:1 at the requested position. **PIXIJS-94**: the per-draw view is now cached per `(frame, rotate)` on the texture's own registry entry instead of allocated per draw, which is what the sprite pooling existed to avoid; the cache is owned by the entry, so a view cannot outlive its base texture. |
| PIXIJS-42 | Color tint: `sprite.tint = 0xRRGGBB` (native, RGB only — same split as `CANVAS-32`/Canvas2D, alpha is `sprite.alpha` separately) | ✅ | Verified with a real non-white tint: an opaque `Color(255,128,64)` scales RGB exactly, and a `Color(255,255,255,128)` composites its alpha separately from its RGB. No premultiply normalization is needed after all -- `ALPHA_MODES.NPM` makes PixiJS pack a **straight** vertex colour rather than premultiplying the tint, which is exactly XNA's own semantics (`CANVAS-84`'s analogous fix has no counterpart here for that reason, not because it was skipped). |
| PIXIJS-43 | Rotation around `origin`: `sprite.anchor.set(originX/width, originY/height); sprite.rotation = ...` — PixiJS's `anchor` *is* XNA's `origin` concept almost directly | ✅ | Verified 2026-08-17 (frame 2): a 180° rotation around a texture's exact center (`origin=(1,1)` on a 2x2 texture) produces the exact expected swapped-quadrant pixels while the bounding box itself does not move. |
| PIXIJS-44 | `SpriteEffects::FlipHorizontally`/`FlipVertically`: `PIXI.Texture`'s own GroupD8 `rotate` parameter (12=H-mirror, 8=V-mirror, 4=both) | ✅ | Verified 2026-08-17 (frame 3), with a real bug found and fixed first (`REMED-PIXIJS-2`): the original negative-`sprite.scale` design shifted the destination rectangle's footprint instead of only mirroring sampling. Fixed by switching to the texture-level `rotate` parameter; GroupD8 values confirmed empirically via a standalone browser probe, not from documentation alone. |
| PIXIJS-45 | `SetTransformMatrix()` (`Begin(transformMatrix)`) | ✅ | Implemented and verified 2026-08-17: the batch's 2D affine transform (`M11/M12/M21/M22/M41/M42`) is composed with each sprite's own local placement matrix and applied via `PIXI.Transform.setFromMatrix`, matching FNA's own post-local-placement `transformMatrix` contract. Composition math confirmed correct via a standalone browser probe (identity/translate/scale cases) before being written; a real `Begin(transformMatrix)` with `Matrix::CreateTranslation(16,16,0)` correctly shifts an entire scaled draw in the smoke test (frame 10, 3 checks). Identity transform keeps the exact pre-existing fast code path, so no regression risk to the other 16 already-verified checks. |
| PIXIJS-46 | `TextureAddressMode` via native `baseTexture.wrapMode = PIXI.WRAP_MODES.{CLAMP,REPEAT,MIRRORED_REPEAT}` | 🟨 | `SetSamplerAddressMode` genuinely sets `baseTexture.wrapMode` to the real WebGL GL-enum values (`REPEAT`=10497, `CLAMP`=33071, `MIRRORED_REPEAT`=33648, confirmed live), and **PIXIJS-90** made the mapping total: an out-of-range enumerator throws instead of falling back to Clamp, and a mixed `AddressU`/`AddressV` pair is rejected outright rather than half-applied, because a `PIXI.BaseTexture` carries one `wrapMode` for both axes. Both rejections and the accepted matching-pair case are covered natively and in the browser. Stays 🟨 for the one thing that is genuinely unreachable: PixiJS's `Texture` constructor rejects a per-draw frame rectangle larger than its base texture, so XNA's classic "oversized source rect tiles under Wrap" behaviour cannot be produced through the per-draw-view architecture -- only linear-filter edge bleed is affected, and that specific effect is still not pixel-asserted. A `PIXI.TilingSprite` draw path would be needed, and is out of v1 scope. |
| PIXIJS-47 | Custom `Effect` via `Begin(effect)`: throws for v1 (Design decision 10) | ✅ | Implemented in `PixiJsSpriteBatchRenderer::SetCustomEffect`, same confidence level as `CANVAS-38`. |

## Phase P5 — Blend and sampler state mapping

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-50 | `ApplyBlendState` → the 4 standard presets | ✅ | All four pixel-verified with exact compositing math: `Opaque` (an unconditional overwrite that ignores source alpha), `Additive`, `AlphaBlend` and `NonPremultiplied`. **PIXIJS-87**: they are now genuinely *distinguished* -- each renders from its own literal XNA factors rather than through a `PIXI.BLEND_MODES` preset PixiJS could rewrite. `BlendStateToPixiJsBlendMode` survives as the tested classifier and still selects `BLEND_MODES.NONE` for `Opaque`, which is what PixiJS optimizes for "no GL blending at all". |
| PIXIJS-51 | Premultiply/straight-alpha handling for `AlphaBlend` vs `NonPremultiplied` | ✅ | **Resolved, and the earlier diagnosis was wrong about the cause.** It was never an `ALPHA_MODES` problem: presets were mapped onto `PIXI.BLEND_MODES`, and PixiJS rewrites `NORMAL` to `NORMAL_NPM` -- a *different* factor tuple -- through `utils.premultiplyBlendMode` whenever the sampled texture is not premultiplied, so `AlphaBlend` was silently rendered with `NonPremultiplied`'s factors and the two presets were indistinguishable by construction. Fixed by rendering every blend state from its literal factors through its own `blendModes` slot, with an identity `premultiplyBlendMode` entry so PixiJS cannot rewrite it. `ALPHA_MODES.NPM` uploads keep CNA's bytes verbatim AND make PixiJS pack a straight vertex tint, which is XNA's own tint semantics. No CNA-wide `Texture2D` metadata was needed after all: the convention lives where XNA puts it, in the pixel data plus the `BlendState` the application selects. Verified with fixtures of the SAME colour in both conventions -- each composites correctly under its own preset, to the same result, and visibly differently under the wrong one. |
| PIXIJS-52 | Fully generic `ApplyBlendState` (arbitrary `Blend`/`BlendFunction`) via custom blend-mode registration (Design decision 6) | ✅ | `XnaBlendToGlFactor`/`XnaBlendFunctionToGlEquation` map every enumerator to real WebGL GL constants, and the resolved tuple is applied through PixiJS's own `renderer.state.blendModes`. **PIXIJS-87** corrected a real defect in the first implementation: it mutated ONE reserved slot per flush, and PixiJS's `StateSystem` skips `setBlendMode()` when the id is unchanged, so a second batch rendered with the first batch's factors. A distinct slot per distinct tuple fixes it -- verified with two different non-preset `BlendState`s in one frame, whose expected pixels are far enough apart that a mix-up cannot pass. |
| PIXIJS-53 | `SetSamplerFilter` → `PIXI.SCALE_MODES.{NEAREST,LINEAR}` | ✅ | An explicit `TextureFilter::Point` keeps a texel-boundary pixel unblended where the `LinearClamp` default measurably blends it -- so the check can only pass if the real sampler state changed. **PIXIJS-87** made this per-batch for real: sampler state lives on the shared `PIXI.BaseTexture`, so with deferred rasterization two batches drawing the same texture collapsed onto whichever filter was set last. Verified with Point and Linear batches on the same texture in one frame. **PIXIJS-90**: an out-of-range `TextureFilter` throws instead of silently rendering as Point. |

## Phase P6 — `SpriteFont`

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-60 | Confirm (don't assume) that `SpriteFont`/`DrawString` needs no renderer-specific code beyond Phase P4's `Draw()` path, same discipline `CANVAS-50` used | ✅ | Verified 2026-08-17: a one-glyph `SpriteFont` (same fixture `htmldom_smoke_test.cpp`'s own `HTMLDOM-38` uses) drawn via `DrawString` produces the glyph's exact atlas color at the requested position in a real backbuffer readback (frame 9, 16/16). `SpriteBatch::DrawString`'s `pushSprite` funnels every glyph through the same `ISpriteBatchRenderer::Draw(destRect, srcRect, color, rotation, origin, effects, layerDepth)` overload already exercised by frames 1-8 -- confirmed by direct code-path inspection of `SpriteBatch.cpp`, not assumed by analogy. |

## Phase P7 — `ThrowNo3D` completeness and remaining defaults

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-70 | Full 3D-surface sweep (`ClearColorAndDepth` and friends, vertex/index buffer creation, `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`) — landed as part of Phase P1's skeleton | ✅ | Every entry point wired to `HandleUnsupported3DCall`/`ShouldStubUnsupported3DResource`, structural GTest coverage in `PixiJsRendererTests.cpp`; not yet run under any real build (status block). |
| PIXIJS-71 | `CreateOcclusionQuery()`/`CreateTexture3D()`/`CreateTextureCube()`/`CreateRenderTargetCube()` → `nullptr` (Design decision 11, shared `IGraphicsRenderer` default, no override needed) | ✅ | No override written — confirmed via inheritance, same as `CANVAS-64`/`CANVAS-66`. |

## Phase P8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-80 | Structural GTest coverage for everything that doesn't need a real `PIXI.Application` | ✅ | `cna_test_pixijs_host` (`CNA_BUILD_PIXIJS_HOST_TESTS=ON`, ctest `PixiJsHostContracts`) runs the suite natively, with no browser and no Emscripten toolchain: **28/28 pass**. Covers the `ThrowNo3D` surface, the blend classifier, both GL-mapping functions over every enumerator plus their rejections, the sampler mappings' domains and their rejections, the blend write-state decisions, the MRT refusal, and the surface contract. |
| PIXIJS-81 | `docs/pixijs-renderer.md` | ✅ | Rewritten to state the current, verified picture: it previously claimed "no pixel-level or browser verification of any kind" in its limitations while its own status section reported 23/23 browser checks. |
| PIXIJS-82 | Manual browser verification checklist (`needs_human`), same shape as `CANVAS-82` | 🟨 | The automated suite is now 70 pixel checks in headless Chromium, but that is one machine on SwiftShader with nobody looking at the screen. The manual checklist `docs/canvas-backend.md` describes has still not been written or run, and no `emrun`/real-device pass has happened. This is the renderer's largest remaining gap. |
| PIXIJS-83 | Update `CLAUDE.md`'s renderer-identity list/count | ✅ | The count itself is owned by `scripts/check_renderer_identities.py` rather than restated here; `CLAUDE.md`'s `PIXIJS` paragraph now describes the verified state and the commit-on-submit invariant. |
| PIXIJS-84 | A real Emscripten toolchain build of `-DCNA_GRAPHICS_RENDERER=PIXIJS` | ✅ | Configures and builds (`cna_renderer_pixijs`, `cna_test_pixijs_smoke`) under emsdk, in Release `-O3` as well as unoptimized -- the optimized build is what surfaced PIXIJS-86. A native `SDL_RENDERER` configure also passes, confirming the shared identity-registry edits break no other renderer. |

## Phase P9 — Correctness under real `SpriteBatch`/`GraphicsDevice` semantics

The renderer reached "every capability implemented and pixel-checked in isolation" before anything
exercised two `Begin`/`End` pairs in one frame. These tasks are what that exposed.

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-85 | A reproducible browser runner, so the pixel suite is a command rather than a hand-driven session | ✅ | `scripts/run_pixijs_browser_tests.mjs` serves a built page over local HTTP and drives it with headless Chromium. The page's own `printf` summary is the verdict, because Emscripten's HTML shell replaces any `Module.onExit` hook injected from outside. |
| PIXIJS-86 | Stop Emscripten's JS optimizer from mangling the vendored PixiJS bundle | ✅ | `--extern-pre-js`, not `--pre-js`. A real `-O3` build lost one of `pixi.min.js`'s own top-level declarations and died with `ReferenceError: Pp is not defined` before a single check ran. Invisible until now because the only previous Emscripten build of this renderer was unoptimized. |
| PIXIJS-87 | Commit at every submission point instead of leaving pooled sprites parented for `Present()` | ✅ | The root fix. See "What this session changed" above for the six defects that fell out of it together. Verified: two Deferred batches with correct overlap order, three Immediate draws, Deferred-then-Immediate ordering, a render target actually **drawn into** and sampled back, A→B→A→back-buffer switching with independent contents, two non-preset `BlendState`s in one frame, two sampler states on one texture in one frame, and a `Texture2D` destroyed between `End()` and the readback. |
| PIXIJS-88 | `SetBlendFactor` / `Blend::BlendFactor` / `Blend::InverseBlendFactor` for real | ✅ | Reaches WebGL's `gl.blendColor`, captured per batch at `Begin()` so a later change cannot alter an already-submitted one. Verified against a real constant, both the factor and its inverse. The constant is carried on the `BlendState` (where XNA puts it), since applying a `BlendState` re-applies its own `BlendFactor`. |
| PIXIJS-89 | Honour or reject `BlendWriteState` instead of discarding it | ✅ | `ColorWriteChannels` slot 0 reaches `gl.colorMask`; verified that a red-only mask leaves G/B from the background and does not leak into the next batch. `MultiSampleMask` is accepted whenever coverage sample 0 is enabled -- on single-sample targets that is an equivalence, not an approximation -- and rejected otherwise. Slots 1..3 describe MRT outputs `SetRenderTargets` already refuses, so they are inapplicable rather than dropped. |
| PIXIJS-90 | Sampler state that does not lie | ✅ | Mixed `AddressU`/`AddressV` is rejected rather than half-applied; out-of-range `TextureFilter`/`TextureAddressMode` values throw instead of defaulting. Covered natively and in the browser, including that the batch stays usable after a rejection. |
| PIXIJS-91 | Explicit, reliable initialization | ✅ | The application is created in the constructor and ensured by every entry point; every `EM_JS` call returns a status the C++ side turns into an exception, replacing `console.error(...); return;`. Verified on the process's genuinely first frame: `SetRenderTarget` as the very first operation, and a back-buffer draw, both with no `Clear()` anywhere before them. |
| PIXIJS-92 | Renderer teardown and recreation | ✅ | Renderer-owned resources are released; the `PIXI.Application` deliberately is not. A canvas hands out one WebGL context and PixiJS's `Renderer.destroy()` loses it, so destroying the application left the platform's canvas permanently unusable -- a second one failed inside PixiJS's batch setup. Found by testing destroy/recreate for real. A still-bound `RenderTarget2D` being destroyed restores the back buffer and warns. |
| PIXIJS-93 | Resize synchronization | ✅ | `OnSurfaceChanged` calls `app.renderer.resize()`, which PixiJS was never told about. Verified by enlarging the drawable, drawing beyond the old width, reading it back, and restoring. `GetSurfaceInfo()` exists so a caller can build a resize snapshot without inventing a window identity the renderer would rightly reject. |
| PIXIJS-94 | Reduce avoidable JS object churn | ✅ | Per-draw `PIXI.Texture` frame views are cached per `(frame, rotate)` on their texture's registry entry instead of allocated per draw. The cache is owned by the entry and destroyed with it, so a view can never outlive the base texture it describes, and a shared base texture is never destroyed because a view was replaced. |
| PIXIJS-95 | A runtime-registry consistency gate, so a public identity cannot exist without being instantiable | ✅ | `scripts/check_renderer_identities.py` now follows every identity through `cmake/RendererRegistry.cmake` to the C++ accessor and family-scoped factory that must back it. Negative-tested against both shapes: a missing registry entry, and a renamed descriptor accessor. |

---

## Boundaries — explicitly out of scope for v1

- **No 3D pipeline** — a deliberate v1 scope line (see "What this renderer is"), not a structural
  impossibility the way it is for `CANVAS`/`HTML_DOM`. Revisit only with its own dedicated plan.
- **No custom `Effect`/shader execution in v1** (Design decision 10) — also not structurally
  impossible (PixiJS has a real shader stage), just out of scope for this plan.
- **No native desktop support** — Emscripten-only (Design decision 1).
- **No CDN-loaded PixiJS at build or run time** — vendored and pinned only (Design decisions 3-4).
- **No MRT, depth or stencil, and no mip level > 0 upload** — see `docs/pixijs-renderer.md`'s
  limitations list for each one's reason and its `PIXIJS-N` task.

The two open items are `PIXIJS-82` (no manual browser pass, no `emrun`/real-device run) and the
jsDelivr half of `PIXIJS-1` (the auto-download URL has never been reachable from a session that
could verify it). Everything else in this plan is marked against a reproduced result.
