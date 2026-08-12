# Renderer SDL audit (PLAT-3)

**Generated** by `tools/platform/renderer_sdl_audit.py`. Regenerate with `--out`, gate with
`--check` (PLAT-76). Do not hand-edit.

46 renderer identities over 42 module families.

| Verdict | Families | Meaning |
|---|---:|---|
| `sdl-native` | 2 | Identity **is** an SDL3 API. Permanently allowlisted. |
| `sdl-upstream` | 2 | Own sources are effectively SDL-free; the wrapped third-party library links SDL3. Allowlisted for a dependency reason. |
| `cpu-presentation` | 2 | CPU rasteriser using SDL_Renderer only to present finished pixels. Needs a platform presentation service. |
| `interface-leak` | 4 | Names SDL types only to satisfy `IGraphicsRenderer`'s SDL-typed methods. Freed by PLAT-59/PLAT-60 alone. |
| `migratable` | 32 | Uses platform services only (native handle, GL/Vulkan, window, events). |

## Per-family detail

| Family | Identities | Verdict | SDL refs (all / in code) | Platform services needed | Presentation calls |
|---|---|---|---:|---|---|
| `sdl-gpu` | SDL_GPU | `sdl-native` | 1707 / 1513 | `window` | — |
| `sdl-renderer` | SDL_RENDERER | `sdl-native` | 304 / 201 | `window` | `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyRenderer`, `SDL_DestroyTexture`, `SDL_GetRenderLogicalPresentationRect`, `SDL_GetRenderOutputSize`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_RenderReadPixels`, `SDL_RenderTexture`, `SDL_SetRenderClipRect`, `SDL_SetRenderDrawColor`, `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderTarget`, `SDL_SetRenderVSync`, `SDL_SetTextureBlendMode`, `SDL_SetTextureScaleMode`, `SDL_UpdateTexture` |
| `freedirect` | FREEDIRECT | `sdl-upstream` | 24 / 8 | `window` | — |
| `fna3d` | FNA3D | `sdl-upstream` | 14 / 9 | `window` | — |
| `skia` | SKIA | `cpu-presentation` | 97 / 83 | `gl-vulkan-interop`, `window` | `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyRenderer`, `SDL_DestroyTexture`, `SDL_GetRenderOutputSize`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_RenderTexture`, `SDL_SetRenderDrawColor`, `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderVSync`, `SDL_UpdateTexture` |
| `blend2d` | BLEND2D | `cpu-presentation` | 51 / 42 | — | `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyRenderer`, `SDL_DestroyTexture`, `SDL_GetRenderOutputSize`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_RenderTexture`, `SDL_SetRenderDrawColor`, `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderVSync`, `SDL_UpdateTexture` |
| `headless` | HEADLESS | `interface-leak` | 4 / 4 | — | — |
| `portablegl` | PORTABLEGL | `interface-leak` | 4 / 3 | — | — |
| `software` | SOFTWARE | `interface-leak` | 4 / 4 | — | — |
| `stub` | STUB | `interface-leak` | 3 / 3 | — | — |
| `sokol` | SOKOL | `migratable` | 67 / 49 | `gl-vulkan-interop`, `window` | — |
| `easygl` | OPENGL33 OPENGLES2 OPENGLES3 WEBGL1 WEBGL2 | `migratable` | 60 / 53 | `gl-vulkan-interop`, `window` | — |
| `opengl1` | OPENGL1 | `migratable` | 60 / 48 | `gl-vulkan-interop`, `window` | — |
| `opengles1` | OPENGLES1 | `migratable` | 59 / 54 | `gl-vulkan-interop`, `window` | — |
| `opengl2` | OPENGL2 | `migratable` | 50 / 39 | `gl-vulkan-interop`, `window` | — |
| `diligent` | DILIGENT | `migratable` | 48 / 35 | `native-handle`, `gl-vulkan-interop`, `window`, `display` | — |
| `opengl4` | OPENGL4 | `migratable` | 48 / 37 | `gl-vulkan-interop`, `window` | — |
| `openvg` | OPENVG | `migratable` | 44 / 36 | `gl-vulkan-interop`, `window` | — |
| `webgpu` | WEBGPU | `migratable` | 41 / 40 | `native-handle`, `gl-vulkan-interop`, `window`, `display` | — |
| `bgfx` | BGFX | `migratable` | 40 / 37 | `native-handle`, `window`, `display` | — |
| `gdi` | GDI | `migratable` | 38 / 35 | `native-handle`, `event`, `window` | — |
| `magnum` | MAGNUM | `migratable` | 36 / 34 | `gl-vulkan-interop`, `window` | — |
| `svg-dom` | SVG_DOM | `migratable` | 32 / 26 | `native-handle`, `window`, `filesystem` | — |
| `vulkan` | VULKAN | `migratable` | 29 / 23 | `gl-vulkan-interop`, `window` | — |
| `llgl` | LLGL | `migratable` | 28 / 23 | `native-handle`, `window`, `display` | — |
| `directx1` | DIRECTX1 | `migratable` | 19 / 11 | `native-handle`, `window` | — |
| `directx2` | DIRECTX2 | `migratable` | 19 / 11 | `native-handle`, `window` | — |
| `directx3` | DIRECTX3 | `migratable` | 19 / 11 | `native-handle`, `window` | — |
| `directx5` | DIRECTX5 | `migratable` | 19 / 11 | `native-handle`, `window` | — |
| `directx6` | DIRECTX6 | `migratable` | 19 / 11 | `native-handle`, `window` | — |
| `directx7` | DIRECTX7 | `migratable` | 19 / 11 | `native-handle`, `window` | — |
| `metal` | METAL | `migratable` | 19 / 14 | `gl-vulkan-interop`, `window` | — |
| `direct2d` | DIRECT2D | `migratable` | 18 / 15 | `native-handle`, `window` | — |
| `directx8` | DIRECTX8 | `migratable` | 17 / 12 | `native-handle`, `window` | — |
| `canvas` | CANVAS | `migratable` | 16 / 11 | `window` | — |
| `directx12` | DIRECTX12 | `migratable` | 16 / 16 | `native-handle`, `window` | — |
| `directx11` | DIRECTX11 | `migratable` | 15 / 15 | `native-handle`, `window` | — |
| `html-dom` | HTML_DOM | `migratable` | 15 / 9 | `window` | — |
| `directx9` | DIRECTX9 | `migratable` | 14 / 14 | `native-handle`, `window` | — |
| `directx10` | DIRECTX10 | `migratable` | 12 / 11 | `native-handle`, `window` | — |
| `wicked` | WICKED | `migratable` | 11 / 8 | `window` | — |
| `glide` | GLIDE | `migratable` | 8 / 8 | `native-handle` | — |

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

3. **4 renderers are coupled only by the interface itself:** `headless`, `portablegl`, `software`, `stub`.
   Their entire SDL surface is naming `SDL_Renderer`/`SDL_Texture`/`SDL_Window` to satisfy
   `IGraphicsRenderer::GetRendererInternal()`/`GetNativeTexture()`/`GetWindowInternal()` —
   the three methods already marked *"TODO: SDL dependency should be abstracted later"*.
   `STUB` and `HEADLESS` do no rendering at all and still appear SDL-coupled. They need no
   per-renderer migration work: PLAT-59 and PLAT-60 free all four at once.
