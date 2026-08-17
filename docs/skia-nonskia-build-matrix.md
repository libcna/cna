# Skia cross-renderer build matrices

## SKIA-20: initial non-Skia selection matrix

This document records the regression matrix run after adding the Skia renderer-selection branch.
It is evidence that selecting Skia did not make the other renderer branches syntactically or
transitively unbuildable; it is not a runtime or rendering-conformance claim for those renderers.

## Validation environment

- Date: 2026-08-01
- CNA baseline: `5eecbf32`
- Host: Linux 6.12.96+deb13-amd64, x86_64
- Compiler: GNU C++ 14.2.0
- Generator: Ninja 1.12.1, CMake 3.31.6
- Configuration: Debug, tests/examples/networking disabled, compiler cache disabled
- Concurrency: every build used `cmake --build <dir> --parallel 2`

Every positive row used a fresh `cmake-build-matrix-*` directory and the following configure
shape, with `<RENDERER>` and `<name>` replaced per row:

```sh
cmake -S . -B cmake-build-matrix-<name> -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=<RENDERER> \
  -DCNA_BUILD_TESTS=OFF \
  -DCNA_BUILD_EXAMPLES=OFF \
  -DCNA_ENABLE_NET=OFF \
  -DCNA_USE_CCACHE=OFF \
  -DCMAKE_C_COMPILER_LAUNCHER= \
  -DCMAKE_CXX_COMPILER_LAUNCHER=
cmake --build cmake-build-matrix-<name> --parallel 2
```

Disabling networking here means the optional CNA networking component, not dependency downloads.
BGFX and WebGPU followed their existing renderer-specific dependency resolution described below.

## Native Linux results

| Selection | Configure | Full configured build | Dependency evidence |
|---|---|---|---|
| `HEADLESS` | pass | pass | no renderer-specific external dependency |
| `SOFTWARE` | pass | pass | no renderer-specific external dependency |
| `SDL_RENDERER` | pass | pass | vendored SDL target |
| `ASCII` | pass | pass | ASCII archive plus the composed SDL_Renderer core archive |
| `EASYGL` | pass | pass | adjacent `../easy-gl` checkout |
| `DIRECTX3` | pass | pass | adjacent `../free-direct` and `../free-api` checkouts; generated source remained in the build tree |
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
toolchain. Each failed at its intentional platform gate before renderer compilation:

| Selection | Observed gate | Result on this host |
|---|---|---|
| `D3D9` | Windows target required; native Windows or `cmake/toolchains/mingw-w64.cmake` suggested | expected rejection |
| `D3D11` | Windows target required; native Windows or `cmake/toolchains/mingw-w64.cmake` suggested | expected rejection |
| `D3D12` | Windows target required; native Windows or `cmake/toolchains/mingw-w64.cmake` suggested | expected rejection |
| `CANVAS` | Emscripten target required | expected rejection |

These four results verify selection isolation and diagnostics only. They are deliberately not
reported as successful builds; their platform/cross-toolchain build evidence remains owned by the
corresponding renderer plans and CI.

## SKIA-113: release-candidate fresh-configure matrix

SKIA-113 repeated the selection check at the end of the Skia work, this time including Skia itself,
the four Linux renderers exercised by current GitHub Actions, the requested Software comparison,
and both platform-gated renderers exercised by native Windows CI. The worktree was clean at baseline
commit `77c3a604`; every build directory was newly created under `/tmp` and no compiler cache was
used.

### Environment and command boundary

- Date: 2026-08-01
- Host: Linux 6.12.96+deb13-amd64, x86_64
- Native compiler: GNU C++ 14.2.0
- Windows cross compiler: x86_64-w64-mingw32 GNU C++ 14
- Generator: Ninja 1.12.1, CMake 3.31.6
- Concurrency: `CMAKE_BUILD_PARALLEL_LEVEL=8` and every explicit build used `--parallel 8`
- Current CI selections: EasyGL, SDL_Renderer, Vulkan, BGFX, D3D11, and D3D12
- Locally unavailable CI toolchain: native Windows/MSVC; the D3D rows therefore add MinGW compile
  and Wine runtime evidence but do not replace the authoritative Windows jobs
- Unavailable platform toolchain: Emscripten (`emcmake`/`em++` absent), so Canvas could not be
  compiled locally and remains covered by its intentional platform gate

The six native Linux rows used this shape, replacing the renderer and build directory per row:

```sh
export CMAKE_BUILD_PARALLEL_LEVEL=8
cmake -S . -B /tmp/cna-skia113-<name> -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=<RENDERER> \
  -DCNA_BUILD_TESTS=OFF \
  -DCNA_BUILD_EXAMPLES=OFF \
  -DCNA_ENABLE_NET=OFF \
  -DCNA_USE_CCACHE=OFF
cmake --build /tmp/cna-skia113-<name> --parallel 8
```

The Skia row used the same pinned CPU-raster artifact validated by SKIA-112. The BGFX row reused
the already pinned `bgfx.cmake` source tree through `FETCHCONTENT_SOURCE_DIR_BGFX_CMAKE`, keeping
this verification offline. No tracked source or dependency state was changed.

### Native Linux results

| Selection | Fresh configure | Full configured build | Notes |
|---|---|---|---|
| `SKIA` | pass | pass | selected the pinned raster renderer; 480 Ninja edges |
| `EASYGL` | pass | pass | selected adjacent `../easy-gl`; 499 Ninja edges |
| `SDL_RENDERER` | pass | pass | selected the vendored SDL renderer; 473 Ninja edges |
| `SOFTWARE` | pass | pass | selected the CPU rasterizer; 473 Ninja edges |
| `VULKAN` | pass | pass | Vulkan 1.4.309 found; optional `glslc` and `glslangValidator` absent; 473 Ninja edges |
| `BGFX` | pass | pass | reused the pinned local `bgfx.cmake` source; renderer archive and CNA linked successfully |

This is a compile-selection smoke, not a claim that these non-Skia renderers gained new
capabilities. All six fresh configurations selected exactly the requested renderer; none fell back
to Skia or another renderer.

### Windows CI selections from Linux

D3D11 and D3D12 were freshly configured with the repository's MinGW-w64 toolchain, matching the
CI options (`RelWithDebInfo`, renderer tests on, examples and networking off). `CNA`, the selected
renderer archive, and every renderer-owned executable were built explicitly with eight workers.
Runtime tests used the project's established Wine prefixes and translation-layer engagement gates,
so a silent WineD3D substitution could not pass.

| Selection | Fresh cross-configure | Renderer compile | Runtime evidence |
|---|---|---|---|
| `D3D11` | pass | `CNA`, D3DCommon, D3D11, and all 41 renderer CTest executables pass | 41/41 on display `:0` under Wine + DXVK 2.6.0, 168.79 s |
| `D3D12` | pass | `CNA`, D3DCommon, D3D12, all renderer executables, and the swap-chain diagnostic pass | 2/2 registered off-screen tests under Wine + vkd3d-proton, 7.24 s |

The D3D12 build emitted only previously existing test-source warnings (ignored `[[nodiscard]]`
results and an ambiguous integral `Color` overload); neither affected compilation or the two
registered off-screen tests. Window/swap-chain D3D12 fixtures remain compile-only on this Linux
loop by the existing DX-100 policy and are exercised by native Windows CI.

### Pre-existing out-of-scope failure

A first `cmake --build /tmp/cna-skia113-d3d11 --parallel 8` invocation built both D3D11 libraries
and `CNA`, then failed while compiling the unrelated monolithic `CnaTests` target. With
`CNA_ENABLE_NET=OFF`, `cmake/UnitTests.cmake` still registers
`tests/CNA/Internal/Net/ENetBackendTests.cpp`, which transitively includes missing `enet/enet.h`.
SKIA-112 had already reproduced the same defect in a fresh native build. The Windows workflow says
its scope is CNA plus renderer-owned tests, but its unqualified `all` build currently includes
`CnaTests`; this is therefore a pre-existing CI/build-graph issue rather than a renderer-selection
regression. SKIA-113 did not change unrelated networking or Windows workflow behavior: explicitly
building the documented D3D scope proved both renderers and all their test executables compile.

The complete requested matrix consequently found no Skia-induced selection or compilation
regression. All observed non-green behavior is either the documented ENet-off registration defect,
an unavailable host toolchain, or an established D3D12 Wine windowing boundary.
