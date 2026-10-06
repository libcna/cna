# Renderer SDL audit (PLAT-3)

**Generated** by `tools/platform/renderer_sdl_audit.py`. Regenerate with `--out`, gate with
`--check` (PLAT-76). Do not hand-edit.

14 renderer identities over 12 module families.

| Verdict | Families | Meaning |
|---|---:|---|
| `sdl-native` | 2 | Identity **is** an SDL3 API. Permanently allowlisted. |
| `sdl-upstream` | 1 | Own sources are effectively SDL-free; the wrapped third-party library links SDL3. Allowlisted for a dependency reason. |
| `sdl-free` | 9 | No SDL references at all. |

## Per-family detail

| Family | Identities | Verdict | SDL refs (all / in code) | Platform services needed | Presentation calls |
|---|---|---|---:|---|---|
| `sdl-gpu` | SDL_GPU | `sdl-native` | 2651 / 2331 | `window`, `display` | — |
| `sdl-renderer` | SDL_RENDERER | `sdl-native` | 310 / 205 | `window` | `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyRenderer`, `SDL_DestroyTexture`, `SDL_GetRenderLogicalPresentationRect`, `SDL_GetRenderOutputSize`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_RenderReadPixels`, `SDL_RenderTexture`, `SDL_SetRenderClipRect`, `SDL_SetRenderDrawColor`, `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderTarget`, `SDL_SetRenderVSync`, `SDL_SetTextureBlendMode`, `SDL_SetTextureScaleMode`, `SDL_UpdateTexture` |
| `fna3d` | FNA3D | `sdl-upstream` | 13 / 10 | `window` | — |
| `directx11` | DIRECTX11 | `sdl-free` | 0 / 0 | — | — |
| `directx9` | DIRECTX9 | `sdl-free` | 0 / 0 | — | — |
| `easygl` | OPENGL33 OPENGLES3 WEBGL2 | `sdl-free` | 0 / 0 | — | — |
| `headless` | HEADLESS | `sdl-free` | 0 / 0 | — | — |
| `metal` | METAL | `sdl-free` | 0 / 0 | — | — |
| `software` | SOFTWARE | `sdl-free` | 0 / 0 | — | — |
| `stub` | STUB | `sdl-free` | 0 / 0 | — | — |
| `vulkan` | VULKAN | `sdl-free` | 0 / 0 | — | — |
| `webgpu` | WEBGPU | `sdl-free` | 0 / 0 | — | — |

## Findings

1. **The allowlist is 3:** `fna3d`, `sdl-gpu`, `sdl-renderer`. `sdl-renderer` and `sdl-gpu` are SDL3 APIs by identity. `fna3d` wraps a third-party
   library that links SDL3 itself, so no amount of migrating CNA code removes its
   dependency. That is a different kind of exception and is recorded as one.

2. **No CPU rasteriser still presents through `SDL_Renderer`.** CPU renderers hand one
   finished frame to `IPlatformSurfacePresenter`; native upload, scaling and vsync now
   remain behind the selected platform implementation.

3. **PLAT-59 closed the interface-only renderer coupling.** `HEADLESS`, `SOFTWARE` and
   `STUB` are now SDL-free. `IGraphicsRenderer` exposes neither native
   window/renderer getter and no longer names `SDL_Renderer`; PLAT-60 had already
   removed its `SDL_Texture*` method.
