# Renderer SDL audit (PLAT-3)

**Generated** by `tools/platform/renderer_sdl_audit.py`. Regenerate with `--out`, gate with
`--check` (PLAT-76). Do not hand-edit.

49 renderer identities over 45 module families.

| Verdict | Families | Meaning |
|---|---:|---|
| `sdl-native` | 2 | Identity **is** an SDL3 API. Permanently allowlisted. |
| `sdl-upstream` | 2 | Own sources are effectively SDL-free; the wrapped third-party library links SDL3. Allowlisted for a dependency reason. |
| `sdl-free` | 41 | No SDL references at all. |

## Per-family detail

| Family | Identities | Verdict | SDL refs (all / in code) | Platform services needed | Presentation calls |
|---|---|---|---:|---|---|
| `sdl-gpu` | SDL_GPU | `sdl-native` | 1821 / 1620 | `window` | — |
| `sdl-renderer` | SDL_RENDERER | `sdl-native` | 309 / 204 | `window` | `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyRenderer`, `SDL_DestroyTexture`, `SDL_GetRenderLogicalPresentationRect`, `SDL_GetRenderOutputSize`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_RenderReadPixels`, `SDL_RenderTexture`, `SDL_SetRenderClipRect`, `SDL_SetRenderDrawColor`, `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderTarget`, `SDL_SetRenderVSync`, `SDL_SetTextureBlendMode`, `SDL_SetTextureScaleMode`, `SDL_UpdateTexture` |
| `freedirect` | FREEDIRECT | `sdl-upstream` | 19 / 3 | `window` | — |
| `fna3d` | FNA3D | `sdl-upstream` | 13 / 10 | `window` | — |
| `bgfx` | BGFX | `sdl-free` | 0 / 0 | — | — |
| `blend2d` | BLEND2D | `sdl-free` | 0 / 0 | — | — |
| `canvas` | CANVAS | `sdl-free` | 0 / 0 | — | — |
| `diligent` | DILIGENT | `sdl-free` | 0 / 0 | — | — |
| `direct2d` | DIRECT2D | `sdl-free` | 0 / 0 | — | — |
| `directx1` | DIRECTX1 | `sdl-free` | 0 / 0 | — | — |
| `directx10` | DIRECTX10 | `sdl-free` | 0 / 0 | — | — |
| `directx11` | DIRECTX11 | `sdl-free` | 0 / 0 | — | — |
| `directx12` | DIRECTX12 | `sdl-free` | 0 / 0 | — | — |
| `directx2` | DIRECTX2 | `sdl-free` | 0 / 0 | — | — |
| `directx3` | DIRECTX3 | `sdl-free` | 0 / 0 | — | — |
| `directx5` | DIRECTX5 | `sdl-free` | 0 / 0 | — | — |
| `directx6` | DIRECTX6 | `sdl-free` | 0 / 0 | — | — |
| `directx7` | DIRECTX7 | `sdl-free` | 0 / 0 | — | — |
| `directx8` | DIRECTX8 | `sdl-free` | 0 / 0 | — | — |
| `directx9` | DIRECTX9 | `sdl-free` | 0 / 0 | — | — |
| `easygl` | OPENGL33 OPENGLES2 OPENGLES3 WEBGL1 WEBGL2 | `sdl-free` | 0 / 0 | — | — |
| `gdi` | GDI | `sdl-free` | 0 / 0 | — | — |
| `glide` | GLIDE | `sdl-free` | 0 / 0 | — | — |
| `headless` | HEADLESS | `sdl-free` | 0 / 0 | — | — |
| `html-dom` | HTML_DOM | `sdl-free` | 0 / 0 | — | — |
| `igl` | IGL | `sdl-free` | 0 / 0 | — | — |
| `llgl` | LLGL | `sdl-free` | 0 / 0 | — | — |
| `magnum` | MAGNUM | `sdl-free` | 0 / 0 | — | — |
| `metal` | METAL | `sdl-free` | 0 / 0 | — | — |
| `nanovg` | NANOVG | `sdl-free` | 0 / 0 | — | — |
| `opengl1` | OPENGL1 | `sdl-free` | 0 / 0 | — | — |
| `opengl2` | OPENGL2 | `sdl-free` | 0 / 0 | — | — |
| `opengl4` | OPENGL4 | `sdl-free` | 0 / 0 | — | — |
| `opengles1` | OPENGLES1 | `sdl-free` | 0 / 0 | — | — |
| `openvg` | OPENVG | `sdl-free` | 0 / 0 | — | — |
| `pixijs` | PIXIJS | `sdl-free` | 0 / 0 | — | — |
| `portablegl` | PORTABLEGL | `sdl-free` | 0 / 0 | — | — |
| `software` | SOFTWARE | `sdl-free` | 0 / 0 | — | — |
| `sokol` | SOKOL | `sdl-free` | 0 / 0 | — | — |
| `stub` | STUB | `sdl-free` | 0 / 0 | — | — |
| `svg-dom` | SVG_DOM | `sdl-free` | 0 / 0 | — | — |
| `tinygl` | TINYGL | `sdl-free` | 0 / 0 | — | — |
| `vulkan` | VULKAN | `sdl-free` | 0 / 0 | — | — |
| `webgpu` | WEBGPU | `sdl-free` | 0 / 0 | — | — |
| `wicked` | WICKED | `sdl-free` | 0 / 0 | — | — |

## Findings

1. **The allowlist is 4, not three:** `fna3d`, `freedirect`, `sdl-gpu`, `sdl-renderer`. `sdl-renderer` and `sdl-gpu` are SDL3 APIs by identity. `fna3d` and `freedirect` wrap
   third-party libraries that link SDL3 themselves — free-direct creates and owns an
   internal `SDL_Renderer` CNA never sees — so no amount of migrating CNA code removes
   their dependency. That is a different kind of exception and is recorded as one.

2. **No CPU rasteriser still presents through `SDL_Renderer`.** CPU renderers hand one
   finished frame to `IPlatformSurfacePresenter`; native upload, scaling and vsync now
   remain behind the selected platform implementation.

3. **PLAT-59 closed the interface-only renderer coupling.** `HEADLESS`, `PORTABLEGL`,
   `SOFTWARE` and `STUB` are now SDL-free. `IGraphicsRenderer` exposes neither native
   window/renderer getter and no longer names `SDL_Renderer`; PLAT-60 had already
   removed its `SDL_Texture*` method.
