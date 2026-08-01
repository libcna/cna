# SKIA-20 non-Skia backend build matrix

This document records the regression matrix run after adding the Skia backend-selection branch.
It is evidence that selecting Skia did not make the other backend branches syntactically or
transitively unbuildable; it is not a runtime or rendering-conformance claim for those backends.

## Validation environment

- Date: 2026-08-01
- CNA baseline: `5eecbf32`
- Host: Linux 6.12.96+deb13-amd64, x86_64
- Compiler: GNU C++ 14.2.0
- Generator: Ninja 1.12.1, CMake 3.31.6
- Configuration: Debug, tests/examples/networking disabled, compiler cache disabled
- Concurrency: every build used `cmake --build <dir> --target CNA --parallel 2`

Every positive row used a fresh `cmake-build-matrix-*` directory and the following configure
shape, with `<BACKEND>` and `<name>` replaced per row:

```sh
cmake -S . -B cmake-build-matrix-<name> -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_BACKEND=<BACKEND> \
  -DCNA_BUILD_TESTS=OFF \
  -DCNA_BUILD_EXAMPLES=OFF \
  -DCNA_ENABLE_NET=OFF \
  -DCNA_USE_CCACHE=OFF \
  -DCMAKE_C_COMPILER_LAUNCHER= \
  -DCMAKE_CXX_COMPILER_LAUNCHER=
cmake --build cmake-build-matrix-<name> --target CNA --parallel 2
```

Disabling networking here means the optional CNA networking component, not dependency downloads.
BGFX and WebGPU followed their existing backend-specific dependency resolution described below.

## Native Linux results

| Selection | Configure | Full `CNA` target | Dependency evidence |
|---|---|---|---|
| `HEADLESS` | pass | pass | no backend-specific external dependency |
| `SOFTWARE` | pass | pass | no backend-specific external dependency |
| `SDL_RENDERER` | pass | pass | vendored SDL target |
| `ASCII` | pass | pass | ASCII archive plus the composed SDL_Renderer core archive |
| `EASYGL` | pass | pass | adjacent `../easy-gl` checkout |
| `DX3` | pass | pass | adjacent `../free-direct` and `../free-api` checkouts; generated source remained in the build tree |
| `VULKAN` | pass | pass | system Vulkan 1.4.309; absent optional `glslc`/`glslangValidator` did not affect this target |
| `SDL_GPU` | pass | pass | vendored SDL GPU API plus `/usr/lib/x86_64-linux-gnu/libshaderc.so.1` |
| `BGFX` | pass | pass | `bgfx.cmake` `99752df38e40179cf998bb880fe4c16c0b3d60ca`; bgfx `c7684e20d`, bimg `3b4baab01`, bx `0b001f5f3` |
| `WEBGPU` | pass | pass | pinned `wgpu-native v29.0.1.1`; downloaded archive SHA-256 `95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a` |

The BGFX and WebGPU inputs were downloaded only into their ignored build directories because no
local cache existed. The first sandboxed configure attempt for each failed at DNS resolution; the
same unmodified CMake command passed once network access was allowed. No downloaded dependency is
tracked by CNA.

## Host-incompatible selections

The remaining non-Skia selections were each configured in a fresh directory without a cross
toolchain. Each failed at its intentional platform gate before backend compilation:

| Selection | Observed gate | Result on this host |
|---|---|---|
| `D3D9` | Windows target required; native Windows or `cmake/toolchains/mingw-w64.cmake` suggested | expected rejection |
| `D3D11` | Windows target required; native Windows or `cmake/toolchains/mingw-w64.cmake` suggested | expected rejection |
| `D3D12` | Windows target required; native Windows or `cmake/toolchains/mingw-w64.cmake` suggested | expected rejection |
| `CANVAS` | Emscripten target required | expected rejection |

These four results verify selection isolation and diagnostics only. They are deliberately not
reported as successful builds; their platform/cross-toolchain build evidence remains owned by the
corresponding backend plans and CI.
