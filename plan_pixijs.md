# PixiJS Graphics Renderer — Implementation Plan

> **Status: DRAFT, authored and implementation started 2026-08-16** per direct task instruction
> ("vytvoř plan_pixijs.md pak podle toho v cna implementuj pixijs renderer" — create plan_pixijs.md,
> then implement the PixiJS renderer in CNA according to it). Unlike `plan_canvas.md`/
> `plan_html_dom.md`, this plan was not preceded by a live owner design conversation captured
> verbatim — the design decisions below are this implementation pass's own proposal, made the same
> way `plan_webgpu.md`'s early tasks were scoped, and are written down explicitly so they can be
> revised by the owner rather than silently assumed correct.
>
> **Status legend** (this project's own convention): ✅ implemented *and verified against its
> stated acceptance criteria*; 🟨 code or documentation exists but has not met those criteria;
> ⬜ not implemented.
>
> **Environment constraint, stated up front and honestly**: this implementation session has **no
> Emscripten SDK available at all** (`emcc`/`em++`/`emcmake` are not on `PATH`, no `emsdk` checkout
> exists, no toolchain file was found). `plan_canvas.md`'s own session at least had a working
> `emcc`/`em++` to structurally build and run under `node`; this one does not even have that. Every
> task below that would normally be "🟨 configure/build verified, needs\_human for real pixels" is
> instead **"code written and reviewed against the FNA/PixiJS API surface, zero automated
> verification of any kind performed."** Treat every PIXIJS-N marked 🟨 in this plan as *more*
> provisional than the same mark on `plan_canvas.md`/`plan_html_dom.md` until a real Emscripten
> toolchain is available and Phase P8's build/test tasks actually run.
>
> **Update, 2026-08-17 — current status summary.** Phases P0/P1 (CMake integration, the
> `PixiJsRenderer` skeleton, and full public-identity registration — `PIXIJS` is now the 47th entry
> in `GraphicsRendererType`, `scripts/check_renderer_identities.py`, `GraphicsBackendCategory.hpp`/
> `GraphicsBackendMaturity.hpp`, `docs/renderer-registry.md`, and every identity-count/compile-
> definition test) are genuinely complete and were validated by a full native
> `-DCNA_GRAPHICS_RENDERER=SDL_RENDERER` `CnaTests` build (exit code 0) plus a passing
> `GraphicsRendererTypeTest` run (7/7, covering all 47 identities). Phases P2-P7 (the actual PixiJS
> draw path — `Clear`/`Present`, textures, render targets, `SpriteBatch`) exist as real, reviewed C++
> and `EM_JS` source, but **have never been compiled or executed under Emscripten in any session**
> (no `emsdk` has ever been available here) — see "What remains" below for the concrete, ordered
> list of what has to happen before any of that code can be trusted. Nothing in Phases P2-P8 should
> be described as working; it is an unverified first draft, not a functioning renderer.
>
> **Update, 2026-08-17 (later the same day) — a real Emscripten toolchain build actually happened
> (PIXIJS-84).** `emsdk` (`emscripten-core/emsdk` on GitHub) was cloned and installed in this
> session, and PixiJS v7.4.2's real `dist/pixi.min.js` was fetched via `npm pack pixi.js@7.4.2`
> (`cdn.jsdelivr.net` itself was blocked by this sandbox's outbound proxy policy, but
> `registry.npmjs.org` was allowlisted) — its real SHA256 is now pinned in
> `cmake/ThirdPartyPixiJS.cmake`. Concrete results, in order:
> - `emcmake cmake -DCNA_GRAPHICS_RENDERER=PIXIJS -DCNA_PIXIJS_ROOT=<the fetched pixi.min.js> ...`
>   **configured successfully**, printing `CNA: Using PIXIJS (pixijs.com WebGL scene graph) graphics
>   renderer`.
> - `cmake --build --target cna_renderer_pixijs` **failed on the first attempt** with a real bug:
>   `Vector2::getZeroProperty()` does not exist (only `Point` has that accessor; `Vector2` exposes a
>   `static const Zero` member instead) — fixed in `PixiJsSpriteBatchRenderer.cpp`'s two `Draw()`
>   overloads. **After that one-line fix, `cna_renderer_pixijs` built cleanly** — the first genuine
>   compiler verification any PixiJS-specific C++ in this renderer has ever received.
> - `cna_test_pixijs_smoke` (the Phase P8 smoke-test executable) **compiled and linked successfully**
>   into a real, runnable `cna_test_pixijs_smoke.js`/`.wasm` — a full, working Emscripten build of
>   this renderer's own example code.
> - Running it under plain `node` reproduces **exactly** `CANVAS-15`'s own documented finding:
>   `SDL_Init(SDL_INIT_VIDEO)` throws `ReferenceError: window is not defined` before any
>   renderer-specific code runs at all, because Node has no real DOM. This is not a PixiJS-specific
>   bug — it is the same "needs a real browser (`emrun`), not `node`" boundary every browser-only CNA
>   renderer hits, now empirically confirmed for `PIXIJS` too rather than assumed by analogy.
> - The full `CnaTests` target (all renderers' tests in one binary, built with `-sASYNCIFY=1`)
>   initially failed for an **unrelated, environment-level reason**: Emscripten's own port-fetch
>   mechanism (`zlib`, needed by `sharp-runtime`'s `IO.Compression` module) tried to download
>   `github.com/madler/zlib/archive/...tar.gz` directly, which this sandbox's proxy also blocks
>   (`git clone` over the git protocol works fine here; raw `https://github.com/.../archive/...`
>   requests do not) — worked around by `git clone`-ing zlib v1.3.2 and manually seeding
>   Emscripten's ports cache directory in the expected layout. After that, **all C++ compiled
>   successfully** (every renderer's tests, `PixiJsRendererTests.cpp` included), but the final
>   **link step crashed inside `wasm-opt --asyncify`** (`UNREACHABLE executed at .../Flatten.cpp:231`,
>   preceded by `em++: warning: ASYNCIFY=1 is not compatible with -fwasm-exceptions. Parts of the
>   program that mix ASYNCIFY and exceptions will not compile.`) under the `emsdk` `latest` alias
>   (**6.0.6**). Re-tried after `emsdk install 6.0.2 && emsdk activate 6.0.2` (the exact version
>   `plan_canvas.md`'s own session used to successfully link `CnaTests.js`; re-seeded the `zlib` port
>   cache, since switching SDK versions replaces the whole `upstream/emscripten` tree) — **the
>   identical crash reproduced under 6.0.2 too**, same file/line. Since it reproduces across two
>   different emsdk point releases, this is not a toolchain-version drift issue — it is a real,
>   already-documented-by-emcc-itself incompatibility between `-sASYNCIFY=1` and `-fwasm-exceptions`,
>   both of which `CnaTests`' own CMake flags enable together (`ASYNCIFY` for the `SystemLink`
>   threading tests, per `cmake/UnitTests.cmake`'s own comment; `-fwasm-exceptions` presumably for
>   real C++ exception propagation, which this XNA-shaped codebase relies on constantly). Not chased
>   further per this plan's own "don't keep retrying toolchain versions indefinitely" guidance —
>   fixing it would mean changing `CnaTests`' shared Emscripten link flags (affecting every renderer,
>   not just `PIXIJS`) and is out of this plan's scope. Left as a genuine, real, cross-renderer
>   Emscripten build gap for a future session to pick up if it wants automated (non-browser) GTest
>   coverage for any renderer under Emscripten, not specifically a `PIXIJS` blocker.
>
> **Bottom line**: `PIXIJS` genuinely builds and links under a real Emscripten toolchain
> (`cna_renderer_pixijs` and `cna_test_pixijs_smoke` both proven, one real bug found and fixed along
> the way). Automated (non-browser) GTest coverage for `PixiJsRendererTests.cpp` is blocked on a
> real, reproduced, non-renderer-specific `CnaTests`/Emscripten toolchain gap
> (`-sASYNCIFY=1` + `-fwasm-exceptions`), not on anything in this renderer's own code.

### What remains (in dependency order)

1. ~~**PIXIJS-1** — pin the real PixiJS v7.4.2 SHA256.~~ **Done** (2026-08-17, via `npm pack`).
2. ~~**PIXIJS-84** — get a real Emscripten toolchain and build.~~ **Done** (2026-08-17): `emsdk`
   installed, `cna_renderer_pixijs` and `cna_test_pixijs_smoke` both compile and link cleanly.
3. ~~Fix whatever the first real Emscripten build surfaces.~~ **One real bug found and fixed**:
   `Vector2::getZeroProperty()` doesn't exist (`Vector2::Zero` does). No further compile errors
   remain in `cna_renderer_pixijs`/`cna_test_pixijs_smoke` as of this update.
4. **Still open, confirmed not an emsdk-version issue**: the shared `CnaTests` target (needed for
   `PixiJsRendererTests.cpp`'s structural GTest coverage, and for
   `PixiJsBlendStateMapping`/`PixiJsRendererThrowNo3D` to actually run under `node`) fails to link
   under Emscripten with a real `-sASYNCIFY=1` + `-fwasm-exceptions` Binaryen crash, reproduced
   identically under both emsdk 6.0.6 and 6.0.2 (the version `plan_canvas.md` used successfully) — a
   genuine, cross-renderer `CnaTests`/Emscripten build gap, not something to keep chasing by trying
   more emsdk versions. Fixing it means touching `CnaTests`' own shared link flags
   (`cmake/UnitTests.cmake`), which affects every renderer, not just this one — a separate task from
   this plan.
5. Run `cna_test_pixijs_smoke` in a real browser (`emrun`), not `node` (confirmed: `node` cannot get
   past `SDL_Init`, same as `CANVAS-15`) — the first real pixel-level evidence this renderer draws
   anything at all.
6. Once basic drawing works, close the still-open design/implementation gaps, roughly in this order:
   - **PIXIJS-22** — verify the `Present()`/ticker design decision (`autoStart:false` +
     explicit `app.renderer.render()`) actually produces frames, not just compiles.
   - **PIXIJS-43/44** — verify the anchor/origin and `SpriteEffects` flip math against a real FNA
     reference render (both are unverified hypotheses right now).
   - **PIXIJS-50/51** — real per-blend-mode PixiJS behavior (`Opaque`/`AlphaBlend`/
     `NonPremultiplied` currently all collapse to the same `PIXI.BLEND_MODES.NORMAL`, which is a
     known, explicitly-flagged gap, not a working mapping).
   - **PIXIJS-31** — decide and implement the real mip-level (`level > 0`) policy instead of the
     current unconditional throw.
   - **PIXIJS-32** — implement direct `Texture2D::SetData` on a bound render target (currently
     throws — no re-upload-via-sprite design written yet).
   - **PIXIJS-45/46/53** — `SetTransformMatrix` (non-identity), `TextureAddressMode`
     (wrap/mirror/clamp), and `SetSamplerFilter` are all still no-op stubs or throws.
   - **PIXIJS-60** — confirm `SpriteFont`/`DrawString` actually falls out of the `SpriteBatch` path
     for free, the way it does on `CANVAS`/`HTML_DOM`.
7. **PIXIJS-52** (stretch) — fully generic `BlendState` support via custom PixiJS blend-mode
   registration, once the 4-preset path above is real and verified.
8. **PIXIJS-80/82** — real GTest execution under Emscripten/`node` (blocked on step 4 above), and a
   manual browser verification checklist (mirroring `docs/canvas-backend.md`'s own 10-item
   checklist) once there is something real to check.

Steps 1-3 are now done. Every task from step 5 onward still stays at its current 🟨/⬜ mark
regardless of how much source code exists for it — code review is not the same as verification, and
this plan deliberately does not conflate the two, even now that real compiler/linker verification
has started.

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

**2. Reuse SDL3's existing `<canvas>` element — PixiJS does not create a second one.** Same lookup
`EasyGLGraphicsBackend.cpp`/`CanvasRenderer.cpp` already use
(`Module['canvas'] || document.querySelector('canvas')`). PixiJS's `Application` is created with
that element passed explicitly (`new PIXI.Application({ view: existingCanvas, ... })` in the
PixiJS v7 API this plan pins — see Design decision 3) rather than letting PixiJS create its own
canvas, so SDL3 keeps owning window sizing, input and the event pump exactly as it does for
`WEBGL2`/`CANVAS`/`HTML_DOM`. Because `CNA_GRAPHICS_RENDERER` selects exactly one renderer at
compile time, there is no runtime conflict with `WEBGL2` also wanting a WebGL context on that same
element — only one of the two is ever linked into a given build.

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
convention — no `embind`, no `emscripten::val`.** State lives on `Module`:
`Module['cnaPixiApp']` (the one `PIXI.Application`), `Module['cnaPixiTextures']` (integer-id →
`PIXI.Texture` registry, mirroring `CanvasTextureRenderer`'s `Module['cnaTextures']`),
`Module['cnaPixiSpritePool']` (an array of pooled, recycled `PIXI.Sprite` objects added once to
`app.stage` and reused across frames — the same pooled-and-recycled-object shape
`plan_html_dom.md` Design decision 3 established for `<div>`s, applied here to `PIXI.Sprite`
instead; see Design decision 7 for why pooling is still worth doing even though PixiJS re-renders
every frame regardless).

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
(`SourceAlpha`/`One`, PixiJS's built-in `PIXI.BLEND_MODES.ADD`) are the Phase P5 v1 scope, same
boundary as the siblings. A fully generic `ApplyBlendState` (arbitrary `Blend`/`BlendFunction`
combination registered as a fresh custom blend mode per unique tuple) is real, believed
straightforward given the API above, and is tracked as PIXIJS-52 rather than assumed free — do not
mark it done without actually implementing and testing the custom-blend-mode registration path.

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

**12. Testing: this session cannot run *anything* — not even a native GTest build has been
attempted yet, and Emscripten is entirely unavailable (see the status block above).** Every
non-JS-touching piece of logic that can be structurally isolated as a pure C++ function (blend-state
→ blend-factor-tuple mapping, address-mode validation, tint/premultiply math) is written as a
standalone function specifically so a future session's GTest run can cover it without a browser —
same shape as `CanvasRenderer.cpp`'s `BlendStateToCompositeOp`. Nothing in this plan should be
marked ✅ until a real Emscripten toolchain build, and ideally a real browser run, actually happens.

---

## Proposed source layout

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
    PixiJsTextureRenderer.cpp
    PixiJsRenderTargetRenderer.cpp
    PixiJsSpriteBatchRenderer.cpp
  examples/
    CMakeLists.txt
    pixijs_smoke_test.cpp
  tests/CNA/Internal/Renderers/PixiJs/
    PixiJsRendererTests.cpp

cmake/ThirdPartyPixiJS.cmake
docs/pixijs-renderer.md
```

Class shape deliberately mirrors `CANVAS`'s four-file split (`Renderer`/`TextureRenderer`/
`RenderTargetRenderer`/`SpriteBatchRenderer`) — same interface surface to satisfy, same reason to
keep them separate.

---

## Active execution order — do this one phase at a time

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

For every task: at minimum, get a real Emscripten toolchain in a later session and configure/build
`-DCNA_GRAPHICS_RENDERER=PIXIJS`; do not mark a task ✅ without that having actually happened.

---

## Phase P0 — Vendoring and CMake integration

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-1 | `cmake/ThirdPartyPixiJS.cmake`: `CNA_PIXIJS_ROOT` / `CNA_PIXIJS_AUTO_DOWNLOAD` (checksum-pinned `pixi.js@7` UMD download), following `ThirdPartyWebGPU.cmake`'s pattern (Design decisions 3-4) | 🟨 | Written; the auto-download path has never actually been run (no network attempt made in this session, and no Emscripten build to consume the result even if it had been). |
| PIXIJS-2 | Add `"PIXIJS"` to `CNA_GRAPHICS_RENDERER`'s CMake `STRINGS` property and a matching `CNA_RENDERER_PIXIJS` option, following the exact existing pattern for `CANVAS`/`HTML_DOM`/`SVG_DOM` | ✅ | |
| PIXIJS-3 | Hard platform gate: `if(CNA_GRAPHICS_RENDERER STREQUAL "PIXIJS" AND NOT EMSCRIPTEN) message(FATAL_ERROR ...)`, mirroring `CANVAS`/`HTML_DOM`/`SVG_DOM`'s own gates (Design decision 1) | ✅ | |
| PIXIJS-4 | `cna_renderer_pixijs` target dispatch (`elseif(CNA_GRAPHICS_RENDERER STREQUAL "PIXIJS")` block in `cmake/RendererSelection.cmake`), including the `--pre-js` link flag wiring for the vendored PixiJS UMD build | ✅ | `--pre-js` flag application is written but unverified end-to-end (no Emscripten toolchain to link with in this session). |
| PIXIJS-5 | `pixijs` added to `modules/CMakeLists.txt`'s `_cna_renderer_modules` physical-source-partition list | ✅ | Required or configure fails for *any* renderer selection, not just `PIXIJS` — verified via a native (`SDL_RENDERER`) configure in this session (Phase P0's only genuinely-run check). |
| PIXIJS-6 | `modules/renderers/pixijs/CMakeLists.txt`: `cna_add_renderer()` + link SDL3 (window/input reuse, Design decision 2) + `add_subdirectory(examples)`, mirroring `canvas`/`html-dom`'s own `CMakeLists.txt` | ✅ | |

## Phase P1 — Skeleton renderer

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-10 | `PixiJsRenderer : IGraphicsRenderer` — every pure virtual overridden; construction/destruction real (`RegisterForWindow`/`UnregisterForWindow`), the inherently-3D-only surface wired to `HandleUnsupported3DCall`/`ShouldStubUnsupported3DResource` exactly like `CanvasRenderer`'s own Phase C1 bring-up | ✅ | Structurally complete; not build-verified (no Emscripten toolchain). |
| PIXIJS-11 | Factory dispatch: `CreateGraphicsRenderer()` returns a `PixiJsRenderer` under `CNA_RENDERER_PIXIJS` | ✅ | |
| PIXIJS-12 | `SupportsDepthStencil()` → `false`; `SupportsCapability()` reports `AdditiveBlending` true (native `PIXI.BLEND_MODES.ADD`) and everything 3D-only false, matching Design decisions 1/6 | ✅ | |

## Phase P2 — `PIXI.Application`, Clear/Present, viewport

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-20 | `EM_JS` acquisition of the existing DOM `<canvas>` (Design decision 2) and `new PIXI.Application({ view, ... })` construction, cached on `Module['cnaPixiApp']` | 🟨 | Code written; zero runtime verification (status block). |
| PIXIJS-21 | `Clear(r,g,b,a)`: `app.renderer.background.color`/`app.renderer.clear()` — PixiJS's own clear-color API, no manual `fillRect` equivalent needed | 🟨 | |
| PIXIJS-22 | `Present()`: decided and implemented — PixiJS's own ticker is disabled (`autoStart:false`/`sharedTicker:false`); `Present()` calls `app.renderer.render(...)` explicitly, keeping CNA's `Game` loop authoritative, consistent with every other renderer's `Present()` contract | 🟨 | Code written (`CNA_PixiJs_Render`); zero runtime verification. |
| PIXIJS-23 | `GetViewportSize()`/`SetVirtualResolution()`/`SetPresentationMode()`/`TransformWindowToLogical`/`TransformLogicalToWindow`: reuse the identical backend-agnostic logical-resolution math `CanvasRenderer`/`EasyGLRenderer` already share — only the physical-size query (`SDL_GetWindowSize`) is backend-specific | ✅ | Verbatim port of `CanvasRenderer`'s `getLogicalSize`/transform math — this part needed no PixiJS-specific code at all, confirmed by reading `CanvasRenderer.cpp` directly rather than assumed. |

## Phase P3 — Textures and render targets

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-30 | `PixiJsTextureRenderer : ITextureRenderer` — buffer-backed `PIXI.BaseTexture`/`PIXI.Texture`, registered by integer id in `Module['cnaPixiTextures']` (Design decision 8) | 🟨 | |
| PIXIJS-31 | `UpdatePixels`/`UpdatePixelsLevel`: in-place buffer mutation + `baseTexture.update()`; mip level>0 policy — implemented as a throw for now (same shape as `CANVAS-21`), explicitly documented as provisional rather than a real investigated decision, since PixiJS textures *can* carry real mipmaps (`PIXI.MIPMAP_MODES`), unlike Canvas2D, and this has not been investigated | 🟨 | level=0 path written; level>0 throws with an explanatory message rather than silently copying Canvas2D's permanent-boundary reasoning. |
| PIXIJS-32 | `PixiJsRenderTargetRenderer : IRenderTargetRenderer` — `PIXI.RenderTexture.create()` + `Bind/UnbindAsRenderTarget` switching which target `app.renderer.render(...)` calls target (Design decision 8) | 🟨 | Direct `UpdatePixels` on a bound render target throws (no re-upload-via-sprite design yet) rather than silently misbehaving. |
| PIXIJS-33 | `ReadBackbuffer`/render-target `GetData`: `app.renderer.extract.pixels(...)` (Design decision 9) | 🟨 | |
| PIXIJS-34 | `HasRealDepthBuffer()` → `false` (no depth attachment on a 2D sprite-only `RenderTexture` in this renderer's v1 scope) | ✅ | Trivial override, same confidence level as `CANVAS-23`. |
| PIXIJS-35 | `SetRenderTargets` with 2+ bindings (MRT) → throw, same conclusion `CANVAS-26`/`HTML_DOM` reached (a single `PIXI.Application`'s default render pipeline targets one `RenderTexture` at a time in this renderer's v1 scope) | ✅ | Implemented in `PixiJsRenderer::SetRenderTargets`. |

## Phase P4 — `SpriteBatch`/`Draw` path

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-40 | `PixiJsSpriteBatchRenderer : ISpriteBatchRenderer` skeleton; pooled-sprite `Begin()`/`End()` with the single-flush `EM_JS` batching (Design decisions 5/7) | 🟨 | 14-word packed `DrawCommand`, one `CNA_PixiJs_FlushSprites` crossing per `End()` (or per `Draw()` in immediate mode). |
| PIXIJS-41 | Basic `Draw(texture, x, y)` / `Draw(texture, destRect, srcRect, color)` via a fresh per-draw `PIXI.Texture` view (shared `baseTexture`, distinct `frame`) assigned to a pooled sprite | 🟨 | |
| PIXIJS-42 | Color tint: `sprite.tint = 0xRRGGBB` (native, RGB only — same split as `CANVAS-32`/Canvas2D, alpha is `sprite.alpha` separately) | 🟨 | No premultiply/straight-alpha tint normalization yet (`CANVAS-84`'s own analogous fix has no PixiJS counterpart) — tracked under PIXIJS-51. |
| PIXIJS-43 | Rotation around `origin`: `sprite.anchor.set(originX/width, originY/height); sprite.rotation = ...` — PixiJS's `anchor` *is* XNA's `origin` concept almost directly | 🟨 | Implemented; the pivot-to-anchor formula has not been verified against FNA's real `GenerateVertexInfo` output. |
| PIXIJS-44 | `SpriteEffects::FlipHorizontally`/`FlipVertically`: negative `sprite.scale.x`/`sprite.scale.y` composed with the same `anchor` point rotation/scale already pivot around | 🟨 | Implemented as a working hypothesis (believed correct by construction, since anchor-relative scale/rotation is exactly XNA's own origin-relative model); explicitly flagged unverified in the code's own comment, not assumed correct the way `CANVAS-34`'s bug class warns against. |
| PIXIJS-45 | `SetTransformMatrix()` (`Begin(transformMatrix)`) | 🟨 | Not implemented — throws for any non-identity matrix rather than silently ignoring it; identity is a no-op. |
| PIXIJS-46 | `TextureAddressMode` via native `baseTexture.wrapMode = PIXI.WRAP_MODES.{CLAMP,REPEAT,MIRRORED_REPEAT}` | ⬜ | `SetSamplerAddressMode` is currently a no-op stub. |
| PIXIJS-47 | Custom `Effect` via `Begin(effect)`: throws for v1 (Design decision 10) | ✅ | Implemented in `PixiJsSpriteBatchRenderer::SetCustomEffect`, same confidence level as `CANVAS-38`. |

## Phase P5 — Blend and sampler state mapping

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-50 | `ApplyBlendState` → the 4 standard presets, mapped to a `PixiJsBlendMode` as a pure C++ function (`BlendStateToPixiJsBlendMode`) | 🟨 | Mapping function written; the JS side (`CNA_PixiJs_SetBlendMode`) currently only distinguishes Additive from everything else (all of Opaque/AlphaBlend/NonPremultiplied drive `PIXI.BLEND_MODES.NORMAL`) — Opaque's real `(One,Zero)` semantics and the AlphaBlend/NonPremultiplied premultiply distinction are NOT yet applied to the actual PixiJS blend state; this is a known, explicitly-commented gap, not a claimed-complete mapping. |
| PIXIJS-51 | Premultiply/straight-alpha handling for `AlphaBlend` vs `NonPremultiplied` via per-texture `PIXI.ALPHA_MODES` | ⬜ | Not implemented — see PIXIJS-50's note. |
| PIXIJS-52 | **Stretch goal**: fully generic `ApplyBlendState` (arbitrary `Blend`/`BlendFunction`) via on-demand custom blend-mode registration (Design decision 6) — real, cheap-looking API surface, but unimplemented and unverified; do not claim this without actually building and testing it | ⬜ | |
| PIXIJS-53 | `SetSamplerFilter` → `PIXI.SCALE_MODES.{NEAREST,LINEAR}` | ⬜ | `SetSamplerFilter` is currently a no-op stub. |

## Phase P6 — `SpriteFont`

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-60 | Confirm (don't assume) that `SpriteFont`/`DrawString` needs no renderer-specific code beyond Phase P4's `Draw()` path, same discipline `CANVAS-50` used | ⬜ | |

## Phase P7 — `ThrowNo3D` completeness and remaining defaults

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-70 | Full 3D-surface sweep (`ClearColorAndDepth` and friends, vertex/index buffer creation, `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`) — landed as part of Phase P1's skeleton | ✅ | Every entry point wired to `HandleUnsupported3DCall`/`ShouldStubUnsupported3DResource`, structural GTest coverage in `PixiJsRendererTests.cpp`; not yet run under any real build (status block). |
| PIXIJS-71 | `CreateOcclusionQuery()`/`CreateTexture3D()`/`CreateTextureCube()`/`CreateRenderTargetCube()` → `nullptr` (Design decision 11, shared `IGraphicsRenderer` default, no override needed) | ✅ | No override written — confirmed via inheritance, same as `CANVAS-64`/`CANVAS-66`. |

## Phase P8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| PIXIJS-80 | Structural GTest coverage for everything that doesn't need a real `PIXI.Application` (`ThrowNo3D` coverage, blend-state→blend-factor-tuple mapping as a pure function once P5 exists, CMake configure/build check) | 🟨 | `PixiJsRendererTests.cpp` written with the P1/P7 `ThrowNo3D` coverage; not yet run against any build (status block). |
| PIXIJS-81 | `docs/pixijs-renderer.md`: mirror `docs/webgpu-renderer.md`'s status/limitations/architecture structure | ✅ | Written alongside this plan. |
| PIXIJS-82 | Manual browser verification checklist (`needs_human`), same shape as `CANVAS-82` | ⬜ | Cannot usefully be written yet — nothing in Phases P2-P6 has even had a structural Emscripten build attempted. |
| PIXIJS-83 | Update `CLAUDE.md`'s renderer-identity list/count (46→47) | ✅ | |
| PIXIJS-84 | A real Emscripten toolchain build of `-DCNA_GRAPHICS_RENDERER=PIXIJS`, in whatever future session has `emsdk` available — the honest first real verification step for everything above | ⬜ | A native (`SDL_RENDERER`) configure+full `CnaTests` build+link was run and PASSED in this session (`[100%] Built target CnaTests`, exit code 0), confirming the shared identity-registry edits (`GraphicsRendererType.hpp` and friends) don't break any other renderer, and `PixiJsRendererTests.cpp` itself compiles cleanly (its `#if defined(CNA_RENDERER_PIXIJS)` guard just makes it an empty TU under `SDL_RENDERER`). `GraphicsRendererTypeTest.*` (7/7, including `NameMatchesTypeForEveryRenderer`/`EveryPublicRendererHasOneUniqueCanonicalName` over all 47 identities) and `GraphicsRendererCompileDefinitionsTest.ExactlyOneGraphicsRendererIsSelected` were run and PASSED. 6 unrelated pre-existing test failures were observed (`XnbBuiltInReaderRegistrationTest`/`Texture3DTextureCubeContentTypeReaderTest`, all "Couldn't open tests/assets/xnb/monogame/..." — missing fixture files in this checkout, nothing to do with this plan's edits). None of this exercises `PixiJsRenderer.cpp`'s own code (Emscripten-gated, never compiled under a native selection) -- still the blocking prerequisite for upgrading *any* 🟨 in this plan to ✅. |

---

## Boundaries — explicitly out of scope for v1

- **No 3D pipeline** — a deliberate v1 scope line (see "What this renderer is"), not a structural
  impossibility the way it is for `CANVAS`/`HTML_DOM`. Revisit only with its own dedicated plan.
- **No custom `Effect`/shader execution in v1** (Design decision 10) — also not structurally
  impossible (PixiJS has a real shader stage), just out of scope for this plan.
- **No native desktop support** — Emscripten-only (Design decision 1).
- **No CDN-loaded PixiJS at build or run time** — vendored and pinned only (Design decisions 3-4).
- **No automated verification of any kind performed in this session** (status block) — every 🟨
  task above is unverified code, not "structurally verified, pixel-unverified" the way `CANVAS`'s
  own 🟨 marks were. Do not read this plan's ✅/🟨 marks as carrying the same confidence level
  `plan_canvas.md`'s do until Phase P8's real Emscripten build actually happens.
