# Renderer SDL audit (PLAT-3)

**Generated** by `tools/platform/renderer_sdl_audit.py`. Regenerate with `--out`, gate with
`--check` (PLAT-76). Do not hand-edit.

10 renderer identities over 8 module families.

| Verdict | Families | Meaning |
|---|---:|---|
| `sdl-native` | 1 | Identity **is** an SDL3 API. Permanently allowlisted. |
| `sdl-upstream` | 1 | Own sources are effectively SDL-free; the wrapped third-party library links SDL3. Allowlisted for a dependency reason. |
| `sdl-free` | 6 | No SDL references at all. |

## Per-family detail

| Family | Identities | Verdict | SDL refs (all / in code) | Platform services needed | Presentation calls |
|---|---|---|---:|---|---|
| `sdl-gpu` | SDLGPU | `sdl-native` | 2425 / 2143 | `window`, `display` | — |
| `fna3d` | FNA3D | `sdl-upstream` | 15 / 11 | `window` | — |
| `directx9` | DIRECTX9 | `sdl-free` | 0 / 0 | — | — |
| `easygl` | OPENGL33 OPENGLES3 WEBGL2 | `sdl-free` | 0 / 0 | — | — |
| `headless` | HEADLESS | `sdl-free` | 0 / 0 | — | — |
| `software` | SOFTWARE | `sdl-free` | 0 / 0 | — | — |
| `stub` | STUB | `sdl-free` | 0 / 0 | — | — |
| `webgpu` | WEBGPU | `sdl-free` | 0 / 0 | — | — |

## Findings

1. **The allowlist is 2:** `fna3d`, `sdl-gpu`. `sdl-gpu` is an SDL3 API by identity. `fna3d` wraps a third-party
   library that links SDL3 itself, so no amount of migrating CNA code removes its
   dependency. That is a different kind of exception and is recorded as one.

2. **No CPU rasteriser still presents through `SDL_Renderer`.** CPU renderers hand one
   finished frame to `IPlatformSurfacePresenter`; native upload, scaling and vsync now
   remain behind the selected platform implementation.

3. **PLAT-59 closed the interface-only renderer coupling.** `HEADLESS`, `SOFTWARE` and
   `STUB` are now SDL-free. `IGraphicsRenderer` exposes neither native
   window/renderer getter and no longer names `SDL_Renderer`; PLAT-60 had already
   removed its `SDL_Texture*` method.
