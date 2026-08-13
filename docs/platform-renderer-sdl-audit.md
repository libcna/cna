# Renderer SDL audit (PLAT-3)

**Generated** by `tools/platform/renderer_sdl_audit.py`. Regenerate with `--out`, gate with
`--check` (PLAT-76). Do not hand-edit.

46 renderer identities over 42 module families.

| Verdict | Families | Meaning |
|---|---:|---|
| `sdl-native` | 2 | Identity **is** an SDL3 API. Permanently allowlisted. |
| `sdl-upstream` | 2 | Own sources are effectively SDL-free; the wrapped third-party library links SDL3. Allowlisted for a dependency reason. |
| `cpu-presentation` | 2 | CPU rasteriser using SDL_Renderer only to present finished pixels. Needs a platform presentation service. |
| `migratable` | 31 | Uses platform services only (native handle, GL/Vulkan, window, events). |
| `sdl-free` | 5 | No SDL references at all. |

## Per-family detail

| Family | Identities | Verdict | SDL refs (all / in code) | Platform services needed | Presentation calls |
|---|---|---|---:|---|---|
| `sdl-gpu` | SDL_GPU | `sdl-native` | 1705 / 1512 | `window` | — |
| `sdl-renderer` | SDL_RENDERER | `sdl-native` | 308 / 204 | `window` | `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyRenderer`, `SDL_DestroyTexture`, `SDL_GetRenderLogicalPresentationRect`, `SDL_GetRenderOutputSize`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_RenderReadPixels`, `SDL_RenderTexture`, `SDL_SetRenderClipRect`, `SDL_SetRenderDrawColor`, `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderTarget`, `SDL_SetRenderVSync`, `SDL_SetTextureBlendMode`, `SDL_SetTextureScaleMode`, `SDL_UpdateTexture` |
| `freedirect` | FREEDIRECT | `sdl-upstream` | 19 / 3 | `window` | — |
| `fna3d` | FNA3D | `sdl-upstream` | 10 / 8 | `window` | — |
| `skia` | SKIA | `cpu-presentation` | 96 / 82 | `gl-vulkan-interop`, `window` | `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyRenderer`, `SDL_DestroyTexture`, `SDL_GetRenderOutputSize`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_RenderTexture`, `SDL_SetRenderDrawColor`, `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderVSync`, `SDL_UpdateTexture` |
| `blend2d` | BLEND2D | `cpu-presentation` | 50 / 41 | `window` | `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyRenderer`, `SDL_DestroyTexture`, `SDL_GetRenderOutputSize`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_RenderTexture`, `SDL_SetRenderDrawColor`, `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderVSync`, `SDL_UpdateTexture` |
| `opengl1` | OPENGL1 | `migratable` | 58 / 46 | `gl-vulkan-interop`, `window` | — |
| `sokol` | SOKOL | `migratable` | 58 / 43 | `gl-vulkan-interop`, `window` | — |
| `opengles1` | OPENGLES1 | `migratable` | 57 / 52 | `gl-vulkan-interop`, `window` | — |
| `opengl2` | OPENGL2 | `migratable` | 46 / 35 | `gl-vulkan-interop`, `window` | — |
| `opengl4` | OPENGL4 | `migratable` | 46 / 35 | `gl-vulkan-interop`, `window` | — |
| `diligent` | DILIGENT | `migratable` | 43 / 33 | `native-handle`, `gl-vulkan-interop`, `window`, `display` | — |
| `openvg` | OPENVG | `migratable` | 43 / 35 | `gl-vulkan-interop`, `window` | — |
| `gdi` | GDI | `migratable` | 39 / 36 | `native-handle`, `event`, `window` | — |
| `webgpu` | WEBGPU | `migratable` | 39 / 38 | `native-handle`, `gl-vulkan-interop`, `window`, `display` | — |
| `bgfx` | BGFX | `migratable` | 36 / 33 | `native-handle`, `window`, `display` | — |
| `magnum` | MAGNUM | `migratable` | 33 / 32 | `gl-vulkan-interop`, `window` | — |
| `svg-dom` | SVG_DOM | `migratable` | 27 / 24 | `native-handle`, `window`, `filesystem` | — |
| `vulkan` | VULKAN | `migratable` | 25 / 19 | `gl-vulkan-interop`, `window` | — |
| `llgl` | LLGL | `migratable` | 24 / 21 | `native-handle`, `window`, `display` | — |
| `direct2d` | DIRECT2D | `migratable` | 16 / 13 | `native-handle`, `window` | — |
| `canvas` | CANVAS | `migratable` | 14 / 9 | `window` | — |
| `directx1` | DIRECTX1 | `migratable` | 14 / 6 | `native-handle`, `window` | — |
| `directx2` | DIRECTX2 | `migratable` | 14 / 6 | `native-handle`, `window` | — |
| `directx3` | DIRECTX3 | `migratable` | 14 / 6 | `native-handle`, `window` | — |
| `directx5` | DIRECTX5 | `migratable` | 14 / 6 | `native-handle`, `window` | — |
| `directx6` | DIRECTX6 | `migratable` | 14 / 6 | `native-handle`, `window` | — |
| `directx7` | DIRECTX7 | `migratable` | 14 / 6 | `native-handle`, `window` | — |
| `directx12` | DIRECTX12 | `migratable` | 12 / 12 | `native-handle`, `window` | — |
| `directx8` | DIRECTX8 | `migratable` | 12 / 7 | `native-handle`, `window` | — |
| `metal` | METAL | `migratable` | 12 / 8 | `gl-vulkan-interop`, `window` | — |
| `directx11` | DIRECTX11 | `migratable` | 11 / 11 | `native-handle`, `window` | — |
| `directx9` | DIRECTX9 | `migratable` | 10 / 10 | `native-handle`, `window` | — |
| `html-dom` | HTML_DOM | `migratable` | 10 / 7 | `window` | — |
| `directx10` | DIRECTX10 | `migratable` | 7 / 6 | `native-handle`, `window` | — |
| `wicked` | WICKED | `migratable` | 6 / 6 | `window` | — |
| `glide` | GLIDE | `migratable` | 4 / 4 | `native-handle` | — |
| `easygl` | OPENGL33 OPENGLES2 OPENGLES3 WEBGL1 WEBGL2 | `sdl-free` | 0 / 0 | — | — |
| `headless` | HEADLESS | `sdl-free` | 0 / 0 | — | — |
| `portablegl` | PORTABLEGL | `sdl-free` | 0 / 0 | — | — |
| `software` | SOFTWARE | `sdl-free` | 0 / 0 | — | — |
| `stub` | STUB | `sdl-free` | 0 / 0 | — | — |

## Findings

1. **The allowlist is 4, not three:** `fna3d`, `freedirect`, `sdl-gpu`, `sdl-renderer`. `sdl-renderer` and `sdl-gpu` are SDL3 APIs by identity. `fna3d` and `freedirect` wrap
   third-party libraries that link SDL3 themselves — free-direct creates and owns an
   internal `SDL_Renderer` CNA never sees — so no amount of migrating CNA code removes
   their dependency. That is a different kind of exception and is recorded as one.

2. **2 CPU rasterisers present through `SDL_Renderer`:** `blend2d`, `skia`.
   They are not SDL renderers by identity — they rasterise on the CPU and then use
   `SDL_CreateTexture`/`SDL_UpdateTexture`/`SDL_RenderTexture`/`SDL_RenderPresent` purely
   to get finished pixels onto the window, with letterbox scaling and vsync. The platform
   contract as originally drafted had nowhere for this to go, which would have left them
   permanently allowlisted for want of an interface. **Present a CPU pixel buffer to a
   window** is a genuine platform capability (SDL2 has it, SDL 1.2 has it as a software
   surface, Win32 has `StretchDIBits`), so it belongs in the contract.

3. **PLAT-59 closed the interface-only renderer coupling.** `HEADLESS`, `PORTABLEGL`,
   `SOFTWARE` and `STUB` are now SDL-free. `IGraphicsRenderer` exposes neither native
   window/renderer getter and no longer names `SDL_Renderer`; PLAT-60 had already
   removed its `SDL_Texture*` method.
