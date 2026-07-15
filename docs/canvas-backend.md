# Canvas (HTML Canvas 2D) Backend — Completeness Status

`CANVAS` is CNA's HTML Canvas 2D graphics backend: Emscripten-only, 2D-only (no 3D pipeline, no
programmable shader stage, no depth/stencil buffer, no MSAA) — the same scope as `SDL_RENDERER`,
just running through `CanvasRenderingContext2D`'s `drawImage`/`putImageData`/`fillRect` API instead
of SDL3's 2D blit pipeline. See `plan_canvas.md` for the full task breakdown, design decisions, and
per-task notes this document summarizes.

**Status legend** (matches this project's own convention): ✅ implemented *and verified against its
stated acceptance criteria*; 🟨 code exists but has not (or cannot, in this dev environment) meet
those criteria; ⬜ not implemented.

**A structural caveat that applies to every ✅ below**: this dev environment has no real browser and
no real DOM — `node`, which runs `CnaTests.js`, does not provide a `CanvasRenderingContext2D` at
all, and `SDL_Init(SDL_INIT_VIDEO)` itself throws under Emscripten/`node` (confirmed empirically,
not assumed — see Phase C1). So every ✅ here means "implemented, code-reviewed, and covered by
whatever structural (non-pixel) GTest coverage is possible" — **not** "pixel-verified in a real
browser," unlike `SDL_RENDERER`'s own completeness doc, where ✅ means a real, running pixel test
passed. Real visual verification needs a human with a browser — see
[Manual browser verification checklist](#manual-browser-verification-checklist) below.

---

## 1. SpriteBatch

| Feature | Status | Notes |
|---|---|---|
| `Draw(texture, x, y)` / `Draw(texture, destRect, srcRect, color)` | ✅ | Both route through one shared `drawImage`-based JS function. |
| Rotation around `origin` | ✅ | Derived and verified algebraically against FNA's real `GenerateVertexInfo` placement — `origin` (source-pixel space) maps exactly to `(destX,destY)`, invariant under rotation. |
| Scalar / `Vector2` scale overloads | ✅ | Confirmed backend-agnostic — `SpriteBatch.cpp` converts every scale overload into the same `(destinationRectangle, sourceRectangle, ...)` call. |
| `SpriteEffects::FlipHorizontally`/`FlipVertically` | ✅ | Mirrors about the sprite's own local center (not the pivot) via `translate`/`scale(-1,1)`/`translate` — matches real XNA/FNA semantics where flip only changes which source corner maps to which unchanged destination corner. |
| Color tint | ✅ | Alpha always via `ctx.globalAlpha` (free). Non-white RGB uses a reused scratch canvas (`multiply` fill + `destination-in` to restore alpha), skipped entirely for `Color.White`. |
| `transformMatrix` in `Begin()` | ✅ | `ctx.setTransform(M11,M12,M21,M22,M41,M42)` as the baseline every `Draw()`'s own relative transform composes on top of — simpler than a hardware backend needing a separate non-Identity code path. |
| Custom `Effect` via `Begin(effect)` | ✅ throws-by-design | No programmable shader stage exists on this backend. |
| `SpriteSortMode` / `Begin`/`End` sequencing | ✅ | Confirmed backend-agnostic — `ISpriteBatchBackend::Begin()` takes no sort-mode parameter at all; sorting/buffering (if any) happens entirely in shared `SpriteBatch.cpp`. |
| `SpriteFont` (`DrawString`, kerning, `\n`, `defaultCharacter`, flip+rotation) | ✅ | Confirmed to need zero Canvas-specific code — every glyph funnels through the same backend `Draw()` overload above; font-specific math (kerning, line spacing, glyph-order mirroring) is entirely shared/backend-agnostic. |

## 2. Texture2D / RenderTarget2D

| Feature | Status | Notes |
|---|---|---|
| `Texture2D` construction / `SetData`/`UpdatePixels` | ✅ | Private off-screen canvas (`OffscreenCanvas` where available) per texture, registered by integer id (`Module['cnaTextures']`); upload via a single synchronous `putImageData()`. |
| Mip-level (`level>0`) `SetData` | ✅ throws-by-design | Canvas2D has no native mip chain either — same conclusion `SDL_RENDERER` reached (Task 681); `level=0` is unaffected. |
| `RenderTarget2D` construction / bind / unbind | ✅ | Same private-canvas mechanism; bind switches `Module['cnaCurrentCtx']`; unbind is a genuine no-op (bind is idempotent/absolute, unlike e.g. EasyGL's non-trivial unbind). |
| `HasRealDepthBuffer()` | ✅ | Always `false` — no Canvas2D target ever has a real depth/stencil buffer, same reasoning as `SDL_RENDERER`'s own Task 708 override. |
| `RenderTargetUsage::DiscardContents` vs `PreserveContents` | ✅ | Confirmed a framework-layer (`GraphicsDevice.cpp`) concern, not a per-backend one — zero backend-specific code needed. |
| `ReadBackbuffer`/`GetBackBufferData` | ✅ | Real, genuinely synchronous `ctx.getImageData(x,y,w,h)` against whichever context is currently bound. |
| `SetRenderTargets` (MRT, 2+ targets) | ✅ throws-by-design | A `CanvasRenderingContext2D` is inherently single-target — same conclusion `SDL_RENDERER`'s Task 709 reached. |
| `Texture2D::FromStream` (PNG/JPEG/DDS) decode | ✅ | Confirmed fully backend-agnostic (`DxtUtil`/SDL3_image) before ever reaching this backend's `putImageData` upload. |

## 3. BlendState

| Feature | Status | Notes |
|---|---|---|
| `Opaque` | ✅ | Maps to `'copy'` — a real hard overwrite. Plain `'source-over'` would still blend through partial alpha, which is wrong for `Opaque` (`srcBlend=One`/`dstBlend=Zero`). |
| `AlphaBlend` | ✅ | Maps to `'source-over'`. |
| `NonPremultiplied` | ✅ | Also maps to `'source-over'` — see the "Known findings" section below for why this coincides with `AlphaBlend` on this backend specifically. |
| `Additive` | ✅ | Maps to `'lighter'`. |
| Any other custom `Blend`/`BlendFunction` combination | ✅ throws-by-design | `globalCompositeOperation` has no generic blend-factor/equation model to fall back on — a narrower, more honest scope than `SDL_RENDERER`'s own `SDL_ComposeCustomBlendMode`-based approximation. |

## 4. SamplerState

| Feature | Status | Notes |
|---|---|---|
| `TextureFilter` (magnification) | ✅ | Maps the "expand" component to `ctx.imageSmoothingEnabled` — same magnification-dominant grouping `SDL_RENDERER`'s Task 701 fix used, against a coarser native primitive (Canvas2D has no separate min/mag/mip control). |
| `TextureAddressMode::Clamp` | ✅ | Implemented for real via an explicit source-rect clamp — not reliance on `drawImage`'s native out-of-bounds behavior, which this dev loop cannot verify in a real browser. |
| `TextureAddressMode::Wrap` | 🟨 | Implemented via `ctx.createPattern(source,'repeat')`, only takes effect when `sourceRectangle` exceeds the texture's own bounds (the only case Wrap can ever visibly differ from Clamp). Unverified in a real browser. |
| `TextureAddressMode::Mirror` | 🟨 | Implemented via a lazily-built, cached 2×2 pre-tiled mirrored canvas as the pattern source (no native mirror-repeat mode exists). Same bounds-exceeding trigger and same real-browser caveat as `Wrap`. |
| Mixed per-axis `Wrap`/`Mirror`/`Clamp` (`addressU != addressV`) | ⬜ throws-by-design | Narrow, deliberate gap — throws rather than guess at unverified interaction behavior. |
| Tinted draw + out-of-bounds `Wrap`/`Mirror` `sourceRectangle` | ⬜ throws-by-design | Narrow, deliberate gap — same reasoning. |

## 5. Viewport / PresentationParameters

| Feature | Status | Notes |
|---|---|---|
| `GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode` | ✅ | Verbatim port of `EasyGLGraphicsBackend`'s own `FixedHeightDynamicWidth` logical-size math against `SDL_GetWindowSize(window_,...)` — SDL3 keeps the DOM `<canvas>` element's width/height attributes in sync with the window it backs. |
| `TransformWindowToLogical`/`TransformLogicalToWindow` | ✅ | Same verbatim port, for correct mouse-coordinate mapping under letterboxing. |
| `GetWindowInternal()`/`GetRendererInternal()` | ✅ | Returns the real `SDL_Window*`; `GetRendererInternal()` is `nullptr` (no `SDL_Renderer*` exists on this backend, same as `EASYGL`). |
| `Present()` | ✅ | Genuine no-op — the browser compositor presents the canvas automatically on the next paint tick. |

## 6. 3D surface (`ThrowNo3D`) and remaining defaults

| Feature | Status | Notes |
|---|---|---|
| `ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` | ✅ throws-by-design | |
| `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` | ✅ throws-by-design | |
| `CreateVertexBuffer`/`CreateIndexBuffer16`/`CreateIndexBuffer32` | ✅ throws-by-design | `CreateIndexBuffer32` throws via inherited delegation to `CreateIndexBuffer16()` — no Canvas-local override needed. |
| `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`/`DrawInstancedPrimitivesEx` | ✅ throws-by-design | The `*Ex` variants and `DrawInstancedPrimitivesEx` throw via `IGraphicsBackend`'s own shared defaults — no Canvas-local override needed. |
| `SupportsDepthStencil()` | ✅ | `false` — the one method in the 3D-surface audit that genuinely needed a new override (the shared default is `true`). |
| `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube` | ✅ | Return `nullptr` via the shared `IGraphicsBackend` default — no 3D-only resource types on a 2D-only backend. |
| `CreateOcclusionQuery()` | ✅ | Returns `nullptr` — a deliberate, already-recorded choice (unlike `SDL_RENDERER`, which overrides this to throw instead) that happens to coincide with the shared default. |
| `CreateEffectBackend()` | ✅ | Returns `nullptr` via the shared default. |
| `DebugSimulateContextLoss`/`DebugRestoreContext` | ✅ | Confirmed (not assumed) that the shared no-op default is correct: unlike WebGL's `WEBGL_lose_context` extension, Canvas2D has no analogous API to force or listen for a context loss on demand. |

---

## Known findings that revise the original DRAFT plan

Two places where actually implementing this backend produced a different (better-grounded) answer
than `plan_canvas.md`'s pre-implementation draft assumed — recorded here for owner review, not
silently substituted:

1. **`AlphaBlend` needs no premultiplied→straight conversion on this backend.** The DRAFT assumed
   `AlphaBlend` (`srcBlend=One`) would need such a conversion before compositing via `'source-over'`.
   In practice: this backend never produces genuinely premultiplied pixel data anywhere (`putImageData`
   uploads straight RGBA8 untouched; the tint pass multiplies RGB directly, not pre-scaled by alpha),
   and Canvas2D's `'source-over'` already premultiplies straight-alpha input internally before
   compositing — which *is* the conversion the DRAFT anticipated needing to hand-roll. So `AlphaBlend`
   and `NonPremultiplied` end up mapping to the exact same `globalCompositeOperation` on this backend,
   with no extra logic. See `CanvasGraphicsBackend::BlendStateToCompositeOp`'s own comment.
2. **`Wrap`/`Mirror` are implemented, but unverified in a real browser.** This dev loop cannot check
   Canvas2D's actual rendered pixels at all (no DOM in `node`), so while the `createPattern`-based
   implementation is code-reviewed and believed correct, it has not been visually confirmed the way
   `SDL_RENDERER`'s own Wrap/Mirror investigation would have required before closing those tasks.
   See the checklist below.

## Manual browser verification checklist

This is the closest honest equivalent to `SDL_RENDERER`'s pixel-verified completeness table for
this backend — everything below needs a human with a real browser (e.g. via `emrun`), since this
dev loop's `node`-based test runner has no `CanvasRenderingContext2D` at all (confirmed empirically,
Phase C1). None of this has been checked yet.

- [ ] Basic sprite draw (`Draw(texture, x, y)`) — correct position, correct pixels.
- [ ] Rotation around a non-trivial `origin` (not the top-left corner) — pivot lands exactly where
      XNA would place it, at every rotation angle, not just axis-aligned ones.
- [ ] `SpriteEffects::FlipHorizontally`/`FlipVertically`, combined with a non-center `origin` — the
      destination quad's screen position must stay fixed; only the visible content should mirror.
- [ ] Color tint: alpha-only (fade), and non-white RGB tint (verify no dark/light fringing at
      semi-transparent edges).
- [ ] All 4 `BlendState` presets (`Opaque`/`AlphaBlend`/`NonPremultiplied`/`Additive`) against an
      overlapping semi-transparent sprite pair — confirm `AlphaBlend`/`NonPremultiplied` really do
      look identical on this backend, per the finding above.
- [ ] `RenderTarget2D` round-trip: render into it, sample it back as a `Texture2D`, `GetBackBufferData`.
- [ ] `SpriteFont` text: multi-line (`\n`), a string containing an unmapped character
      (`defaultCharacter` fallback), and a flipped+rotated `DrawString`.
- [ ] `TextureAddressMode::Wrap` and `Mirror` with a `sourceRectangle` larger than the texture (the
      one case they can ever visibly differ from `Clamp`) — **this is the least-verified area in the
      whole backend** (see the finding above).
- [ ] `Begin(transformMatrix)` with a genuinely non-Identity matrix (e.g. a camera pan/zoom) —
      confirm sprites transform correctly on top of their own placement/rotation.
- [ ] Mouse-coordinate mapping under a letterboxed/scaled window (`TransformWindowToLogical`).

## See also

- `plan_canvas.md` — full task breakdown, design decisions, per-task implementation notes.
- `docs/sdl-renderer-2d-completeness.md` — the 2D-only sibling backend this one's scope mirrors.
- `docs/web-emscripten-graphics-limitations.md` — general Emscripten/Web platform constraints.
