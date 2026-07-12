# WebGPU graphics backend

## Status

The WebGPU backend was activated by the project owner on **2026-07-12** and is currently an
**experimental fifth CNA graphics backend**. Select it with:

```bash
cmake -S . -B cmake-build-webgpu \
  -DCNA_GRAPHICS_BACKEND=WEBGPU \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-webgpu -j
```

CNA pins the native implementation to **wgpu-native v29.0.1.1**. By default CMake downloads the
official prebuilt package for a supported host. For offline or reproducible builds, extract the
official package yourself and configure:

```bash
cmake -S . -B cmake-build-webgpu \
  -DCNA_GRAPHICS_BACKEND=WEBGPU \
  -DCNA_WEBGPU_ROOT=/absolute/path/to/extracted/wgpu-native \
  -DCNA_WEBGPU_AUTO_DOWNLOAD=OFF
```

The extracted root must contain `include/webgpu.h` (or `include/webgpu/webgpu.h`) and a
`libwgpu_native` library below `lib/`. The existing CNA requirements still apply: the SDL3,
SDL3_image and SDL3_mixer submodules, plus the sibling `../sharp-runtime` checkout, must be
present.

For the verified Linux x86_64 layout, an extracted package may be placed at
`vendor/wgpu-native/` with `include/webgpu/webgpu.h` and `lib/libwgpu_native.so`. The following
clean offline sequence builds a self-contained demo directory:

```bash
cmake -S . -B /tmp/cna-webgpu-128 \
  -DCNA_GRAPHICS_BACKEND=WEBGPU \
  -DCNA_WEBGPU_ROOT="$PWD/vendor/wgpu-native" \
  -DCNA_WEBGPU_AUTO_DOWNLOAD=OFF \
  -DCNA_BUILD_TESTS=OFF \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cna-webgpu-128 --target cna_demo_2d -j1
cd /tmp/cna-webgpu-128
readelf -d ./cna_demo_2d | grep -E 'NEEDED.*wgpu|RUNPATH'
ldd ./cna_demo_2d | grep libwgpu_native.so
timeout 60s ./cna_demo_2d --smoke 120
```

The WebGPU CMake target links wgpu-native by filename despite the package library lacking an ELF
SONAME, copies the runtime beside `cna_demo_2d`, and gives the executable a `$ORIGIN` runtime path.
This enables the executable to use its sibling `libwgpu_native.so` rather than requiring the
original package location at runtime.

## Automated native smoke test

With `CNA_BUILD_TESTS=ON`, the WebGPU configuration registers `WebGPU_Native2D_Smoke` with CTest:

```bash
ctest --test-dir /tmp/cna-webgpu-128 -R '^WebGPU_Native2D_Smoke$' --output-on-failure
```

The test runs `cna_demo_2d --smoke 120` when the host exposes Wayland or X11. It passed in 2.30
seconds on the verified desktop. When neither `WAYLAND_DISPLAY` nor `DISPLAY` is available, the
wrapper reports a clear skipped result rather than treating the lack of a desktop GPU/display as a
backend failure.

CNA enables compiler caching automatically when `ccache` is installed. The setting is applied
before the sibling `sharp-runtime` project is added, so both CNA and `sharp-runtime` objects are
reused across compatible build directories. Disable it with `-DCNA_USE_CCACHE=OFF`; existing custom
compiler launchers are never overwritten. Even without `ccache`, reusing the same CMake build
directory keeps the normal incremental object cache and avoids rebuilding unchanged
`sharp-runtime` sources.

## Verified native smoke gate

`WEBGPU-125` was verified on Linux desktop on 2026-07-12 with the explicit offline package:

```bash
cmake -S . -B /tmp/cna-webgpu-125 \
  -DCNA_GRAPHICS_BACKEND=WEBGPU \
  -DCNA_WEBGPU_ROOT="$PWD/vendor/wgpu-native" \
  -DCNA_WEBGPU_AUTO_DOWNLOAD=OFF \
  -DCNA_BUILD_TESTS=OFF \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cna-webgpu-125 --target cna_demo_2d -j1
cd /tmp/cna-webgpu-125
timeout 60s ./cna_demo_2d --smoke 120
```

The executable cleared and presented 120 frames, then exited with status 0 in 2.10 seconds. No
uncaptured WebGPU error, device-loss report or dynamic-loader failure was emitted. This verifies
the native initialization, frame submission, present and normal teardown path only; it is not a
pixel-correctness result for SpriteBatch or Texture2D.

## SpriteBatch validation scene

`cna_demo_2d` provides the deterministic coverage scene used to verify `WEBGPU-126`:

```bash
cd /tmp/cna-webgpu-125
./cna_demo_2d --webgpu-2d-validation --smoke 120
```

It uploads `player.png` and displays source rectangles, tint/alpha blending, rotation, horizontal
and vertical flips, linear and point filtering, and clamp/wrap/mirror address modes. The last
three use UVs outside the texture's `[0, 1]` range so their visual results differ. During the run,
the native window resizes from 800×600 to 960×540 and back, exercising logical presentation and
surface reconfiguration. The command completes without WebGPU errors; a manual desktop screenshot
review on 2026-07-12 confirmed the expected crop, tint/alpha, rotation, flips and sampler results.

## Lifecycle recovery

`WEBGPU-127` replaced the original unbounded adapter/device callback waits with
`wgpuInstanceProcessEvents` polling and a 10-second timeout. This avoids the pinned v29 package's
unimplemented `wgpuInstanceWaitAny` path, which aborts rather than waiting. A minimized or
zero-size window now unconfigures the surface and releases the depth attachment; restoring it
causes the normal surface configuration path to rebuild them. The 180-frame validation command
above exercised resize, minimize, restore and normal teardown with exit status 0 and no WebGPU
error.

## Independent application integration and native 2D baseline

`WEBGPU-130` integrated `../mobile-eggbert` (`WindowsPhoneSpeedyBlupi`, an independently-developed
XNA-style game, not a CNA example) as a second, real-world consumer of this backend, verified on
Linux desktop 2026-07-12:

```bash
cd ../mobile-eggbert
cmake -B cmake-build-debug \
  -DCNA_GRAPHICS_BACKEND=WEBGPU \
  -DCNA_WEBGPU_ROOT=/absolute/path/to/cna_graphics/vendor/wgpu-native \
  -DCNA_WEBGPU_AUTO_DOWNLOAD=OFF
cmake --build cmake-build-debug --target WindowsPhoneSpeedyBlupi -j
cd cmake-build-debug
DISPLAY=:0 ./WindowsPhoneSpeedyBlupi
```

The build links `cna_backend_graphics_webgpu` and copies `libwgpu_native.so` beside the executable
with `$ORIGIN` first in `RUNPATH`, the same deployment shape `WEBGPU-128` verified for `cna_demo_2d`.
On a real desktop session it reached its main menu automatically (~5 seconds after launch) with
pixel-correct `SpriteBatch` rendering (title text, an animated character, a player-select panel, a
Setup icon and a Play button), and a simulated click on the Play button correctly triggered its
mission-start sequence — an animated cutscene with a filling progress bar and a cross-fade
transition, rendered correctly frame-by-frame with no WebGPU validation errors. This exercises real
multi-sprite, multi-frame animation and alpha-blended fade compositing beyond the synthetic
validation scene above, independently confirming the SpriteBatch pipeline, Texture2D upload and
resize/present paths against a second, unrelated codebase.

`WEBGPU-131` closes the native 2D baseline on this evidence: `WEBGPU-124`–`WEBGPU-130` are all
verified. 3D, effects, render targets, GPU readback and MRT remain open (Phase 57 onward in
`plan_webgpu.md`).

## Implemented baseline

The initial backend is deliberately useful rather than an empty scaffold. It currently provides:

- backend selection and `CNA_BACKEND_WEBGPU` build wiring;
- native SDL3 surface creation for Win32, macOS/Metal, Linux/X11 and Linux/Wayland, with an
  Android native-window path compiled when the required SDL property is available;
- instance, adapter, device and queue initialization with device-lost and uncaptured-error
  reporting;
- surface capability selection, resize reconfiguration and FIFO/Immediate/Mailbox present-mode
  selection from CNA's swap interval;
- back-buffer acquisition, command encoding, color/depth/stencil clears, submission and present;
- RGBA8 `Texture2D` creation, level uploads and mip-chain allocation;
- 16-bit and 32-bit index-buffer uploads plus generic vertex-buffer uploads;
- a WGSL SpriteBatch pipeline with tint, source rectangle, destination rectangle, rotation,
  origin, layer depth, transforms, flips, linear/nearest filtering and wrap/clamp/mirror sampler
  caching;
- CNA logical-presentation modes and window/logical coordinate conversion.

## Important limitations

This is **not yet equivalent to CNA's Vulkan, EasyGL or Bgfx 3D backends**. The following remain
open in `plan_webgpu.md`:

- 3D primitive and indexed draw pipelines, stock effects, lighting, skinning and instancing;
- render targets, cube/3D textures, compressed formats, MSAA and multiple render targets;
- GPU readback (`GetBackBufferData`, texture `GetData`) and pixel-test integration;
- full BlendState, DepthStencilState, RasterizerState, viewport and scissor mapping;
- custom SpriteBatch effects and custom WGSL effects;
- browser/Emscripten WebGPU; this first implementation is the native wgpu-native backend.

Calls to the two legacy colored-3D entry points fail explicitly with a descriptive exception rather
than silently producing incorrect output. Interface methods not overridden by this initial backend
retain the common backend's existing unsupported/default behavior.

## Architecture notes

WebGPU and Vulkan are conceptually related explicit APIs, so the Vulkan backend informed resource
lifetime, surface recovery, command encoding and render-pass structure. The implementation is not a
line-by-line Vulkan translation:

- WebGPU uses WGSL shader modules rather than CNA's Vulkan SPIR-V modules.
- Resource bindings use bind-group layouts and bind groups rather than Vulkan descriptor sets.
- WebGPU has no push constants, so future 3D effect data will use uniform buffers.
- Pipeline state is largely immutable and must eventually be represented in a pipeline cache.
- Native surface creation is performed directly from SDL3 window properties; CNA does not require
  the separate `sdl3webgpu` compatibility library.

See `plan_webgpu.md` for task-level status and the remaining parity work.
