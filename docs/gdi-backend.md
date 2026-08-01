# GDI backend

`CNA_GRAPHICS_BACKEND=GDI` selects CNA's Windows-only, 2D-only GDI presentation backend.

It uses the CPU SpriteBatch/textures/render-target path shared with `SOFTWARE`, then presents the
main RGBA8 backbuffer into SDL's native Win32 `HWND` with GDI `StretchDIBits`.  This is a real GDI
display path; it does not create an SDL renderer, D3D device, OpenGL context or GPU swap chain.

## Scope

- Supported: Clear, RGBA textures, SpriteBatch (including source rectangles, transforms, rotation,
  flip, CPU alpha blending), 2D render targets, backbuffer/read-target readback, viewport/scissor,
  and CNA presentation modes.
- Not supported: vertex/index buffers, 3D draw calls, depth/stencil, MSAA, cube/3D textures,
  occlusion queries and custom effects. `SupportsCapability()` returns `false` and direct 3D API
  calls throw rather than silently rendering through the inherited CPU 3D code.
- `PresentInterval` is ignored because GDI has no swap-chain interval control. The backbuffer is
  single-sampled.

## Performance

Every frame is rasterized on the CPU and copied to the window by GDI. It is appropriate for
compatibility applications, UI, retro games and modest-resolution 2D workloads. Large render
targets, extensive alpha blending/rotation, high resolutions or a hard 60/120 FPS requirement are
better served by `SDL_RENDERER`, `SDL_GPU`, or a Direct3D backend.

## Build

```bash
cmake -S . -B build-gdi \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
  -DCNA_GRAPHICS_BACKEND=GDI \
  -DCNA_BUILD_TESTS=ON
cmake --build build-gdi --target CNA cna_test_gdi_smoke
```

The backend is hard-gated to Windows targets. `cna_test_gdi_smoke` runs automatically as `GDI_Smoke`
on native Windows; for MinGW cross-builds, run the produced executable under a Wine setup with an
available display.
