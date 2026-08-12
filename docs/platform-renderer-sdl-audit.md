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
| `sdl-gpu` | SDL_GPU | `sdl-native` | 1705 / 1511 | `window` | — |
| `sdl-renderer` | SDL_RENDERER | `sdl-native` | 307 / 203 | `window` | `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyRenderer`, `SDL_DestroyTexture`, `SDL_GetRenderLogicalPresentationRect`, `SDL_GetRenderOutputSize`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_RenderReadPixels`, `SDL_RenderTexture`, `SDL_SetRenderClipRect`, `SDL_SetRenderDrawColor`, `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderTarget`, `SDL_SetRenderVSync`, `SDL_SetTextureBlendMode`, `SDL_SetTextureScaleMode`, `SDL_UpdateTexture` |
| `freedirect` | FREEDIRECT | `sdl-upstream` | 22 / 6 | `window` | — |
| `fna3d` | FNA3D | `sdl-upstream` | 10 / 7 | `window` | — |
| `skia` | SKIA | `cpu-presentation` | 95 / 81 | `gl-vulkan-interop`, `window` | `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyRenderer`, `SDL_DestroyTexture`, `SDL_GetRenderOutputSize`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_RenderTexture`, `SDL_SetRenderDrawColor`, `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderVSync`, `SDL_UpdateTexture` |
| `blend2d` | BLEND2D | `cpu-presentation` | 49 / 40 | — | `SDL_CreateRenderer`, `SDL_CreateTexture`, `SDL_DestroyRenderer`, `SDL_DestroyTexture`, `SDL_GetRenderOutputSize`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_RenderTexture`, `SDL_SetRenderDrawColor`, `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderVSync`, `SDL_UpdateTexture` |
| `portablegl` | PORTABLEGL | `interface-leak` | 3 / 2 | — | — |
| `headless` | HEADLESS | `interface-leak` | 2 / 2 | — | — |
| `software` | SOFTWARE | `interface-leak` | 2 / 2 | — | — |
| `stub` | STUB | `interface-leak` | 2 / 2 | — | — |
| `sokol` | SOKOL | `migratable` | 61 / 45 | `gl-vulkan-interop`, `window` | — |
| `easygl` | OPENGL33 OPENGLES2 OPENGLES3 WEBGL1 WEBGL2 | `migratable` | 58 / 51 | `gl-vulkan-interop`, `window` | — |
| `opengl1` | OPENGL1 | `migratable` | 58 / 46 | `gl-vulkan-interop`, `window` | — |
| `opengles1` | OPENGLES1 | `migratable` | 57 / 52 | `gl-vulkan-interop`, `window` | — |
| `opengl2` | OPENGL2 | `migratable` | 48 / 37 | `gl-vulkan-interop`, `window` | — |
| `opengl4` | OPENGL4 | `migratable` | 46 / 35 | `gl-vulkan-interop`, `window` | — |
| `diligent` | DILIGENT | `migratable` | 44 / 33 | `native-handle`, `gl-vulkan-interop`, `window`, `display` | — |
| `openvg` | OPENVG | `migratable` | 43 / 35 | `gl-vulkan-interop`, `window` | — |
| `webgpu` | WEBGPU | `migratable` | 39 / 38 | `native-handle`, `gl-vulkan-interop`, `window`, `display` | — |
| `bgfx` | BGFX | `migratable` | 38 / 35 | `native-handle`, `window`, `display` | — |
| `gdi` | GDI | `migratable` | 38 / 35 | `native-handle`, `event`, `window` | — |
| `magnum` | MAGNUM | `migratable` | 34 / 32 | `gl-vulkan-interop`, `window` | — |
| `svg-dom` | SVG_DOM | `migratable` | 28 / 24 | `native-handle`, `window`, `filesystem` | — |
| `vulkan` | VULKAN | `migratable` | 27 / 21 | `gl-vulkan-interop`, `window` | — |
| `llgl` | LLGL | `migratable` | 25 / 21 | `native-handle`, `window`, `display` | — |
| `directx1` | DIRECTX1 | `migratable` | 17 / 9 | `native-handle`, `window` | — |
| `directx2` | DIRECTX2 | `migratable` | 17 / 9 | `native-handle`, `window` | — |
| `directx3` | DIRECTX3 | `migratable` | 17 / 9 | `native-handle`, `window` | — |
| `directx5` | DIRECTX5 | `migratable` | 17 / 9 | `native-handle`, `window` | — |
| `directx6` | DIRECTX6 | `migratable` | 17 / 9 | `native-handle`, `window` | — |
| `directx7` | DIRECTX7 | `migratable` | 17 / 9 | `native-handle`, `window` | — |
| `metal` | METAL | `migratable` | 17 / 12 | `gl-vulkan-interop`, `window` | — |
| `direct2d` | DIRECT2D | `migratable` | 16 / 13 | `native-handle`, `window` | — |
| `directx8` | DIRECTX8 | `migratable` | 15 / 10 | `native-handle`, `window` | — |
| `canvas` | CANVAS | `migratable` | 14 / 9 | `window` | — |
| `directx12` | DIRECTX12 | `migratable` | 14 / 14 | `native-handle`, `window` | — |
| `directx11` | DIRECTX11 | `migratable` | 13 / 13 | `native-handle`, `window` | — |
| `directx9` | DIRECTX9 | `migratable` | 12 / 12 | `native-handle`, `window` | — |
| `html-dom` | HTML_DOM | `migratable` | 11 / 7 | `window` | — |
| `directx10` | DIRECTX10 | `migratable` | 10 / 9 | `native-handle`, `window` | — |
| `glide` | GLIDE | `migratable` | 7 / 7 | `native-handle` | — |
| `wicked` | WICKED | `migratable` | 7 / 6 | `window` | — |

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
   Their entire remaining SDL surface is naming `SDL_Renderer`/`SDL_Window` to satisfy
   `IGraphicsRenderer::GetRendererInternal()`/`GetWindowInternal()` — the two common
   methods still marked *"TODO: SDL dependency should be abstracted later"*.
   `STUB` and `HEADLESS` do no rendering at all and still appear SDL-coupled. They need no
   per-renderer migration work: PLAT-59 frees all four at once; PLAT-60 removed the
   already-unused `SDL_Texture*` method.
