# Canvas (HTML Canvas 2D) Renderer — Completeness Status

`CANVAS` is CNA's HTML Canvas 2D graphics renderer: Emscripten-only, 2D-only (no 3D pipeline, no
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
| Scalar / `Vector2` scale overloads | ✅ | Confirmed renderer-agnostic — `SpriteBatch.cpp` converts every scale overload into the same `(destinationRectangle, sourceRectangle, ...)` call. |
| `SpriteEffects::FlipHorizontally`/`FlipVertically` | ✅ | Mirrors about the sprite's own local center (not the pivot) via `translate`/`scale(-1,1)`/`translate` — matches real XNA/FNA semantics where flip only changes which source corner maps to which unchanged destination corner. |
| Color tint | ✅ (fixed) | An earlier `multiply` fill + `destination-in` scratch-canvas trick was mathematically wrong — verified algebraically against the CSS Compositing spec's blend-mode formula, it produced `Rt*(1-As*(1-Rs))` instead of `Rt*Rs`, a real dark/light-fringing bug at semi-transparent, non-white edge pixels (found in external review). Fixed with a direct per-pixel `getImageData`/`putImageData` RGB multiply that leaves alpha completely untouched — exact, no compositing-order pitfalls. Alpha is still applied separately via `ctx.globalAlpha` (free). |
| `transformMatrix` in `Begin()` | ✅ | `ctx.setTransform(M11,M12,M21,M22,M41,M42)` as the baseline every `Draw()`'s own relative transform composes on top of — simpler than a hardware renderer needing a separate non-Identity code path. |
| Custom `Effect` via `Begin(effect)` | ✅ throws-by-design | No programmable shader stage exists on this renderer. |
| `SpriteSortMode` / `Begin`/`End` sequencing | ✅ | Confirmed renderer-agnostic — `ISpriteBatchRenderer::Begin()` takes no sort-mode parameter at all; sorting/buffering (if any) happens entirely in shared `SpriteBatch.cpp`. |
| `SpriteFont` (`DrawString`, kerning, `\n`, `defaultCharacter`, flip+rotation) | ✅ | Confirmed to need zero Canvas-specific code — every glyph funnels through the same renderer `Draw()` overload above; font-specific math (kerning, line spacing, glyph-order mirroring) is entirely shared/renderer-agnostic. |

## 2. Texture2D / RenderTarget2D

| Feature | Status | Notes |
|---|---|---|
| `Texture2D` construction / `SetData`/`UpdatePixels` | ✅ | Private off-screen canvas (`OffscreenCanvas` where available) per texture, registered by integer id (`Module['cnaTextures']`); upload via a single synchronous `putImageData()`. |
| Mip-level (`level>0`) `SetData` | ✅ throws-by-design | Canvas2D has no native mip chain either — same conclusion `SDL_RENDERER` reached (Task 681); `level=0` is unaffected. |
| `RenderTarget2D` construction / bind / unbind | ✅ | Same private-canvas mechanism; bind switches `Module['cnaCurrentCtx']`; unbind is a genuine no-op (bind is idempotent/absolute, unlike e.g. EasyGL's non-trivial unbind). |
| `HasRealDepthBuffer()` | ✅ | Always `false` — no Canvas2D target ever has a real depth/stencil buffer, same reasoning as `SDL_RENDERER`'s own Task 708 override. |
| `RenderTargetUsage::DiscardContents` vs `PreserveContents` | ✅ | Confirmed a framework-layer (`GraphicsDevice.cpp`) concern, not a per-renderer one — zero renderer-specific code needed. |
| `ReadBackbuffer`/`GetBackBufferData` | ✅ | Real, genuinely synchronous `ctx.getImageData(x,y,w,h)` against whichever context is currently bound. |
| `SetRenderTargets` (MRT, 2+ targets) | ✅ throws-by-design | A `CanvasRenderingContext2D` is inherently single-target — same conclusion `SDL_RENDERER`'s Task 709 reached. |
| `Texture2D::FromStream` (PNG/JPEG/DDS) decode | ✅ | Confirmed fully renderer-agnostic (`DxtUtil`/SDL3_image) before ever reaching this renderer's `putImageData` upload. |

## 3. BlendState

| Feature | Status | Notes |
|---|---|---|
| `Opaque` | ✅ (fixed) | Maps to `'copy'` — a real hard overwrite, clipped to exactly the sprite's own drawn rect. An earlier version applied `'copy'` with no clip, which cleared the **entire canvas** outside the sprite's footprint to transparent (Porter-Duff `'copy'` is evaluated over the whole compositing area, not just the drawn shape — confirmed via the CSS Compositing spec's per-pixel formula, found in external review). Fixed via `ctx.clip()` to the exact drawn rect before applying `'copy'` (a no-op for `'source-over'`/`'lighter'`, which never touch pixels outside the drawn shape anyway). |
| `AlphaBlend` | ✅ (fixed) | Maps to `'source-over'`, with a real per-pixel un-premultiply pass (divide RGB by alpha) applied to the source pixels first. An earlier version treated this identically to `NonPremultiplied` (no conversion at all) on the reasoning that this renderer's own textures are never genuinely premultiplied — but `SDL_RENDERER` has a dedicated pixel test (Task 697) that constructs genuinely premultiplied source data specifically to verify `AlphaBlend`, proving that assumption doesn't hold project-wide (found in external review). Un-premultiplying before Canvas2D's own internal premultiply-before-composite step now correctly reproduces a `srcBlend=One` equation for real premultiplied input. |
| `NonPremultiplied` | ✅ | Maps to `'source-over'` directly, no extra processing — Canvas2D already treats `drawImage`/`putImageData` input as straight alpha natively, exactly what `NonPremultiplied`'s `srcBlend=SourceAlpha` factor assumes. |
| `Additive` | ✅ | Maps to `'lighter'`. |
| Any other custom `Blend`/`BlendFunction` combination | ✅ throws-by-design | `globalCompositeOperation` has no generic blend-factor/equation model to fall back on — a narrower, more honest scope than `SDL_RENDERER`'s own `SDL_ComposeCustomBlendMode`-based approximation. |

## 4. SamplerState

| Feature | Status | Notes |
|---|---|---|
| `TextureFilter` (magnification) | ✅ | Maps the "expand" component to `ctx.imageSmoothingEnabled` — same magnification-dominant grouping `SDL_RENDERER`'s Task 701 fix used, against a coarser native primitive (Canvas2D has no separate min/mag/mip control). |
| `TextureAddressMode::Clamp` | ✅ | Implemented for real via an explicit source-rect clamp — not reliance on `drawImage`'s native out-of-bounds behavior, which this dev loop cannot verify in a real browser. |
| `TextureAddressMode::Wrap` | 🟨 | Implemented via `ctx.createPattern(source,'repeat')`, only takes effect when `sourceRectangle` exceeds the texture's own bounds (the only case Wrap can ever visibly differ from Clamp). Unverified in a real browser. |
| `TextureAddressMode::Mirror` | 🟨 | Implemented via a lazily-built, cached 2×2 pre-tiled mirrored canvas as the pattern source (no native mirror-repeat mode exists); the cache is invalidated on both `Texture2D::SetData` and `RenderTarget2D::BindAsRenderTarget()` (fixed in two external-review rounds — a render target's pixels can change via direct draws while bound, never going through `SetData` at all). Same bounds-exceeding trigger and same real-browser caveat as `Wrap`. |
| Mixed per-axis `Wrap`/`Mirror`/`Clamp` (`addressU != addressV`) | ✅ throws-by-design (fixed) | Real `std::runtime_error`, thrown from C++ (`ValidateAddressModeCombination`) before ever reaching the JS draw path. An earlier version only logged `console.error` to the browser console and silently skipped the draw entirely — contradicting the plan's own "throw rather than guess" intent (found in external review). |
| Tinted draw + out-of-bounds `Wrap`/`Mirror` `sourceRectangle` | ✅ throws-by-design (fixed) | Same fix as above — real exception, not a silent skip. |
| `AlphaBlend` draw + out-of-bounds `Wrap`/`Mirror` `sourceRectangle` | ✅ throws-by-design | Also validated in C++ — the tiled pattern source would need its own per-pixel un-premultiply pass, not yet implemented; throws rather than producing wrong output. |

## 5. Viewport / PresentationParameters

| Feature | Status | Notes |
|---|---|---|
| `GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode` | ✅ | Verbatim port of `EasyGLRenderer`'s own `FixedHeightDynamicWidth` logical-size math against `SDL_GetWindowSize(window_,...)` — SDL3 keeps the DOM `<canvas>` element's width/height attributes in sync with the window it backs. |
| `TransformWindowToLogical`/`TransformLogicalToWindow` | ✅ | Same verbatim port, for correct mouse-coordinate mapping under letterboxing. |
| Window ownership / SDL renderer exposure | ✅ | `GameWindow` owns the window-facing handle; the common renderer interface exposes neither an SDL window nor an `SDL_Renderer*` (PLAT-59). |
| `Present()` | ✅ | Genuine no-op — the browser compositor presents the canvas automatically on the next paint tick. |
| `Clear(color)` | ✅ (fixed) | `ctx.save()`/`fillRect` under an explicit `globalCompositeOperation='copy'`/`ctx.restore()`. An earlier version used a plain `fillRect` with whatever composite mode a previous `SpriteBatch` draw happened to leave active (e.g. `'lighter'`/`'copy'` from `Additive`/`Opaque`), blending the clear color with old content instead of the exact overwrite real XNA/FNA's `Clear()` performs; it also permanently reset the transform via `setTransform(identity)` rather than `save`/`restore`, corrupting an active `SpriteBatch` camera transform if `Clear()` was called mid-`Begin()`/`End()` (both found in external review). |

## 6. 3D surface (`ThrowNo3D`) and remaining defaults

| Feature | Status | Notes |
|---|---|---|
| `ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` | ✅ throws-by-design | |
| `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` | ✅ throws-by-design | |
| `CreateVertexBuffer`/`CreateIndexBuffer16`/`CreateIndexBuffer32` | ✅ throws-by-design | `CreateIndexBuffer32` throws via inherited delegation to `CreateIndexBuffer16()` — no Canvas-local override needed. |
| `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`/`DrawInstancedPrimitivesEx` | ✅ throws-by-design | The `*Ex` variants and `DrawInstancedPrimitivesEx` throw via `IGraphicsRenderer`'s own shared defaults — no Canvas-local override needed. |
| `SupportsDepthStencil()` | ✅ | `false` — the one method in the 3D-surface audit that genuinely needed a new override (the shared default is `true`). |
| `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube` | ✅ | Return `nullptr` via the shared `IGraphicsRenderer` default — no 3D-only resource types on a 2D-only renderer. |
| `CreateOcclusionQuery()` | ✅ | Returns `nullptr` — a deliberate, already-recorded choice (unlike `SDL_RENDERER`, which overrides this to throw instead) that happens to coincide with the shared default. |
| `CreateEffectRenderer()` | ✅ | Returns `nullptr` via the shared default. |
| `DebugSimulateContextLoss`/`DebugRestoreContext` | ✅ | Confirmed (not assumed) that the shared no-op default is correct: unlike WebGL's `WEBGL_lose_context` extension, Canvas2D has no analogous API to force or listen for a context loss on demand. |

---

## Bugs found in external review (2026-07-15) and fixed

An external code review of this renderer (after the initial "all 8 phases complete" pass) found 5
real bugs — all confirmed against the actual Canvas2D/CSS Compositing spec math (not just re-argued
abstractly) and fixed. Listed here for visibility, not buried in the per-feature tables above:

1. **`BlendState.Opaque` cleared the entire canvas, not just the sprite.** `globalCompositeOperation
   = 'copy'` with no clip region wipes out everything outside the newly-drawn shape too (Porter-Duff
   `'copy'` is evaluated over the whole compositing area). Fixed via `ctx.clip()` to the exact drawn
   rect first.
2. **`AlphaBlend` and `NonPremultiplied` were wrongly treated as identical.** The original reasoning
   ("this renderer's textures are never genuinely premultiplied, so no conversion is needed") doesn't
   hold project-wide: `SDL_RENDERER` has a dedicated pixel test (Task 697) that deliberately
   constructs premultiplied source data to verify `AlphaBlend` specifically. Fixed with a real
   per-pixel un-premultiply pass, applied only for `AlphaBlend`.
3. **RGB tint darkened/lightened semi-transparent edges (an A² bug).** The original `multiply` +
   `destination-in` scratch-canvas trick was algebraically wrong (verified against the CSS
   Compositing spec's blend-mode formula). Fixed with a direct, exact per-pixel RGB multiply.
4. **Wrap/Mirror validation gaps silently skipped the draw instead of throwing.** Contradicted the
   plan's own "throw rather than guess" intent. Fixed by moving validation into C++, throwing a real
   `std::runtime_error` before ever reaching JS.
5. **The Mirror-tile cache was never invalidated after a pixel update.** Fixed in two passes: first
   for `Texture2D::SetData` (deleting the cached tile on every `UpdatePixels` call); a follow-up
   review round then caught that a bound `RenderTarget2D`'s pixels can *also* change via direct
   `Clear()`/`Draw()` calls made against it while bound, never going through `UpdatePixels` at all —
   so `RenderTarget2D::BindAsRenderTarget()` now also invalidates the cache, conservatively, on every
   bind (treating "this target was just bound" as "its content may be about to change").

A 6th issue (Clear() blending with old content under a leftover composite mode, and permanently
resetting the transform rather than save/restore) was found and fixed in the same pass — see the
`Clear(color)` row in §5 above.

**Still true after these fixes**: none of this has been pixel-verified in a real browser (this dev
loop has no `CanvasRenderingContext2D` at all — see the checklist below). The fixes above were
verified by deriving the exact Porter-Duff/CSS-Compositing math by hand and checking the code against
it, plus structural GTest coverage of the now-pure, now-testable C++ logic (`BlendStateToCompositeOp`,
`ValidateAddressModeCombination`) — not by looking at actual rendered pixels.

## Manual browser verification checklist

This is the closest honest equivalent to `SDL_RENDERER`'s pixel-verified completeness table for
this renderer — everything below needs a human with a real browser (e.g. via `emrun`), since this
dev loop's `node`-based test runner has no `CanvasRenderingContext2D` at all (confirmed empirically,
Phase C1). None of this has been checked yet.

- [ ] Basic sprite draw (`Draw(texture, x, y)`) — correct position, correct pixels.
- [ ] Rotation around a non-trivial `origin` (not the top-left corner) — pivot lands exactly where
      XNA would place it, at every rotation angle, not just axis-aligned ones.
- [ ] `SpriteEffects::FlipHorizontally`/`FlipVertically`, combined with a non-center `origin` — the
      destination quad's screen position must stay fixed; only the visible content should mirror.
- [ ] Color tint: alpha-only (fade), and non-white RGB tint (verify no dark/light fringing at
      semi-transparent edges).
- [ ] `Opaque` blend against a scene with other content already drawn elsewhere on the canvas —
      confirm only the sprite's own footprint is affected, nothing else disappears.
- [ ] `AlphaBlend` with genuinely premultiplied texture data (RGB pre-scaled by alpha) vs.
      `NonPremultiplied` with straight-alpha data — confirm both render correctly (no double-darkening)
      and, drawn against each other's "wrong" data convention, confirm they now look *different* (not
      identical, per the fix above).
- [ ] `RenderTarget2D` round-trip: render into it, sample it back as a `Texture2D`, `GetBackBufferData`.
- [ ] `SpriteFont` text: multi-line (`\n`), a string containing an unmapped character
      (`defaultCharacter` fallback), and a flipped+rotated `DrawString`.
- [ ] `TextureAddressMode::Wrap` and `Mirror` with a `sourceRectangle` larger than the texture (the
      one case they can ever visibly differ from `Clamp`) — **this is the least-verified area in the
      whole renderer**; also confirm a `Mirror`-addressed draw shows updated pixels after a
      `Texture2D::SetData()` call AND after drawing new content into a bound `RenderTarget2D` that
      was previously Mirror-addressed (both cache-invalidation fixes above).
- [ ] `Begin(transformMatrix)` with a genuinely non-Identity matrix (e.g. a camera pan/zoom) —
      confirm sprites transform correctly on top of their own placement/rotation.
- [ ] `Clear()` called *between* a `SpriteBatch.Begin(transformMatrix)`/`End()` pair — confirm draws
      after the `Clear()` still use the batch's own transform correctly (not reset to identity).
- [ ] Mouse-coordinate mapping under a letterboxed/scaled window (`TransformWindowToLogical`).

## See also

- `plan_canvas.md` — full task breakdown, design decisions, per-task implementation notes.
- `docs/sdl-renderer-2d-completeness.md` — the 2D-only sibling renderer this one's scope mirrors.
- `docs/web-emscripten-graphics-limitations.md` — general Emscripten/Web platform constraints.
