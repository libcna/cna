# fna3d-spike — FNA3D existence gate (plan_fna3d.md FNA3D-0)

Standalone probe written **before** any CNA renderer code, following the precedent set by
`dx9-spike/`, `dx2-spike/`, … Its job is to prove that the third-party library actually works in
this environment, so that every later CNA-side claim rests on a measurement rather than on
upstream documentation.

## What it proves

| Check | Result on this machine |
|---|---|
| `FNA3D_PrepareWindowAttributes()` selects a driver and returns SDL window flags | returns `0x00000002` = `SDL_WINDOW_OPENGL` |
| An SDL3 window created with exactly those flags is accepted by `FNA3D_CreateDevice()` | PASS |
| `FNA3D_Clear()` + `FNA3D_SwapBuffers()` complete | PASS |
| `FNA3D_ReadBackbuffer()` returns the **exact** cleared colour at (0,0) and at the far corner | PASS — `0,255,0,255` |
| `FNA3D_CreateTexture2D` + `SetTextureData2D` + `GetTextureData2D` preserve bytes | PASS |
| `FNA3D_CreateEffect()` accepts a real D3D9 Effect Framework binary and MojoShader reports 0 errors | PASS |

Measured driver banner:

```
FNA3D Driver: OpenGL
OpenGL Renderer: llvmpipe (LLVM 20.1.2, 256 bits)
OpenGL Driver: 4.5 (Compatibility Profile) Mesa 25.2.8
MojoShader Profile: glsl120
```

Effect probe (FNA's stock `SpriteEffect.fxb`, 1104 bytes):

```
effect: 1 techniques, 3 params, 0 errors
  param[0] = "Texture" (1 values)
  param[1] = "TextureSampler" (1 values)
  param[2] = "MatrixTransform" (16 values)
  technique[0] = "SpriteBatch" (1 passes)
```

## Why the effect check matters

FNA3D exposes **no shader entry point other than `FNA3D_CreateEffect`** — there is no "compile
this GLSL/HLSL string" call anywhere in `FNA3D.h`, and the OpenGL driver refuses to draw without a
bound MojoShader program. Any draw at all therefore requires a Direct3D 9 Effect Framework binary.
This probe is what established that the FNA stock-effect blobs parse cleanly through the FNA3D
build CNA links, and therefore that a real 2D/3D pipeline is reachable at all.

## Two findings that shaped the renderer

1. **`mojoshader.h` must be included before `FNA3D.h`.** `FNA3D.h` forward-declares
   `MOJOSHADER_effect` as an incomplete type unless `_INCL_MOJOSHADER_H_` is already defined, and
   the Effect Framework structs live behind `MOJOSHADER_EFFECT_SUPPORT`. Consumers also need
   `MOJOSHADER_NO_VERSION_INCLUDE` (the generated `mojoshader_version.h` is not installed).
   `cmake/ThirdPartyFNA3D.cmake` carries all three on the interface target.
2. **The SDL_GPU driver is first in FNA3D's driver list.** With no Vulkan ICD present it declines
   and FNA3D falls through to OpenGL by itself; `FNA3D_FORCE_DRIVER=OpenGL` pins that
   deterministically, which is what the CNA renderer's tests set.

## Build & run

```sh
# 1. FNA3D (static, against CNA's already-vendored SDL3)
SDLROOT=$PWD/../.sdl-prebuilt-Linux-x86_64/install
cmake -S ~/deps/FNA3D -B fna3d-build -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=OFF -DCMAKE_PREFIX_PATH="$SDLROOT"
cmake --build fna3d-build -j4

# 2. The probe
gcc -std=gnu99 -O1 -g -DMOJOSHADER_NO_VERSION_INCLUDE -DMOJOSHADER_USE_SDL_STDLIB \
    fna3d_device_spike.c -o fna3d_device_spike \
    -I~/deps/FNA3D/include -I~/deps/FNA3D/MojoShader -I$SDLROOT/include \
    fna3d-build/libFNA3D.a fna3d-build/libmojoshader.a \
    -L$SDLROOT/lib -lSDL3 -Wl,-rpath,$SDLROOT/lib -lm

# 3. Run (headless)
FNA3D_FORCE_DRIVER=OpenGL \
FNA3D_SPIKE_FXB=~/deps/FNA/src/Graphics/Effect/StockEffects/FXB/SpriteEffect.fxb \
  xvfb-run -a ./fna3d_device_spike
```

Exit code 0 = every check passed. Built binaries and the `fna3d-build/` tree are gitignored; the
`.c` source and this record are committed.
