# RLGL renderer

`CNA_GRAPHICS_RENDERER=RLGL` selects CNA's experimental renderer built on the standalone
[`rlgl.h`](https://github.com/raysan5/raylib/blob/dbc56a87da87d973a9c5baa4e7438a9d20121d28/src/rlgl.h)
low-level OpenGL abstraction from raylib 6.0. CNA compiles that header directly; it does not build,
link, initialize, or otherwise use the raylib application framework.

The authoritative implementation ledger and EasyGL capability matrix are in
[`../plans/plan_rlgl.md`](../plans/plan_rlgl.md). This document describes only the capability that
has actually been validated. RLGL is not yet suitable for normal CNA games.

## Architecture and ownership

```text
XNA-compatible CNA graphics API
  -> GraphicsDevice and CNA graphics resources/effects
  -> CNA IGraphicsRenderer contracts
  -> CNA RLGL renderer
  -> standalone rlgl low-level resource/state wrappers
  -> OpenGL 3.3 core
  -> CNA IPlatformGlContext
  -> the selected CNA platform implementation
```

CNA owns the window, GL context, context switching, drawable size, resize/fullscreen events,
presentation scaling, swap interval, and buffer swap. It also continues to own input, audio, the
game loop, effects, and public resource objects. The renderer passes CNA's context procedure loader
to `rlLoadExtensions()`, initializes rlgl only after that context is current, and calls `rlglClose()`
before CNA releases the context. No raylib window or second event loop is created.

The rendering path uses rlgl's low-level wrappers. It does not use rlgl's default immediate-mode
batch as CNA's draw scheduler: that batch owns ordering and selected state transitions that conflict
with XNA's explicit state model. A small private bridge may call the GL dispatch already loaded by
rlgl only where rlgl exposes no faithful wrapper; the exact bridge gaps are recorded in the plan.

## Verified boundary

✅ means there is executable runtime evidence. 🟨 means the code path exists but its complete XNA
behavior has not yet been verified. ❌ means CNA refuses the operation rather than reporting false
success.

| Capability | Status | Evidence or current behavior |
|---|---:|---|
| CNA-created GL 3.3 core context and standalone rlgl initialization | ✅ | Two sequential device lifecycles under SDL3's offscreen driver and Mesa llvmpipe |
| `GraphicsDevice`, color/depth/stencil clear, backbuffer RGBA8 readback | ✅ | Exact pixels checked before `Present()`; combined clear also exercises depth/stencil planes |
| Explicit `Present()` and swap-interval forwarding | ✅ | Both device lifecycles present; requested interval zero is retained and sent to the platform context |
| Drawable resize and presentation rectangle refresh | ✅ | A `GraphicsDeviceManager` resize is followed by dimension and post-resize pixel checks |
| Viewport/scissor coordinate application | 🟨 | Top-left CNA rectangles are mapped to bottom-left GL coordinates; transition/pixel coverage remains in `RLGL-013` |
| Backbuffer MSAA | 🟨 | Context samples are requested with a non-MSAA retry and the achieved count is reported; resolve/output coverage remains in `RLGL-014` |
| `Texture2D` (`Color`, `Bgr565`, `Bgra5551`, `Bgra4444`, `NormalizedByte2/4`) | ✅ | Exact full/partial format-native uploads and native readback passed, including packed XNA/GL bit-layout translation, signed-normalized storage, odd-row alignment, row order, binding, and non-zero RGBA8 mips |
| XNA `SamplerState` | ✅ | Independent GL sampler objects passed all filter/address ordinals, mip/bias, anisotropy, transition, slot-isolation, and sampled-pixel checks |
| Other Texture2D formats | ❌ | High-precision/float formats and DXT1/3/5 are tracked by `RLGL-027`/`RLGL-028`; unsupported formats fail instead of changing their byte interpretation |
| SpriteBatch/SpriteFont | ❌ | Factory throws a diagnostic naming `RLGL-010` |
| Vertex/index buffers and draw calls | ❌ | Factories/draw hooks throw diagnostics naming `RLGL-011` |
| Stock/custom effects and normal 3D workloads | ❌ | Await the buffer, texture, and shader tasks; `GraphicsCapability::ThreeD` remains false |
| Render targets, MRT, cube resources, instancing, queries | ❌ | Explicitly unavailable until their recorded tasks pass focused validation |
| Concurrent RLGL devices | ❌ | rlgl 6.0 has one process-global state object; a second live device is rejected |
| Device loss/reset and resource restoration | ❌ | Not claimed until `RLGL-017` completes |

`SupportsCapability(AnisotropicFiltering)` follows rlgl's live extension probe and measured ceiling;
all other capability results remain false. Native GL support alone is not treated as a CNA
implementation promise. The renderer opts in only after its complete path and observable behavior
are tested.

## Profile and platforms

The one public `RLGL` identity currently compiles rlgl with `GRAPHICS_API_OPENGL_33` and requests an
OpenGL 3.3 core context. There are no separate `RLGL33`, `RLGL43`, or RLGL/OpenGL-ES identities.
Additional profiles will remain build-time implementation choices if they are added after measured
portability work.

| Environment | Status |
|---|---|
| Linux x86_64, SDL3 offscreen, Mesa llvmpipe 25.0.7 | Runtime validated; the driver granted OpenGL 4.5 core for CNA's 3.3-core request |
| Linux with an on-screen X11/Wayland driver | Expected from the shared CNA GL context contract, but not yet runtime validated for RLGL |
| Windows | Source/build architecture is intended to be portable; not yet compiled or runtime validated |
| macOS | A 3.3 core context is within the platform's OpenGL ceiling; not yet compiled or runtime validated |
| SDL2 platform implementation | Uses the same CNA GL contract in principle; the current focused smoke executable is SDL3-only and no RLGL runtime result exists yet |
| Emscripten / browser | Unsupported by the initial desktop GL 3.3 profile |

## Configure, build, and smoke

With the repository's normal sibling/submodule dependencies available:

```bash
cmake -S . -B cmake-build-rlgl -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_RENDERER=RLGL \
      -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-rlgl --parallel 4 --target \
      cna_renderer_rlgl cna_test_rlgl_smoke cna_test_rlgl_texture cna_test_rlgl_sampler
```

The smoke target is deliberately available with `CNA_BUILD_TESTS=OFF`. On a Linux machine with
SDL3's offscreen driver and Mesa software rendering it can be run without a display server:

```bash
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  ./cmake-build-rlgl/cna_test_rlgl_smoke
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  ./cmake-build-rlgl/cna_test_rlgl_texture
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  ./cmake-build-rlgl/cna_test_rlgl_sampler
```

With `CNA_BUILD_TESTS=ON`, the executables are registered as `Rlgl_Smoke`, `Rlgl_Texture`, and
`Rlgl_Sampler`, labelled `Rlgl` plus their focused graphics category, with the proven offscreen
environment attached to each CTest entry.

## Dependency and offline builds

`cmake/ThirdPartyRlgl.cmake` fetches the official `raysan5/raylib` source archive at exact raylib
6.0 commit `dbc56a87da87d973a9c5baa4e7438a9d20121d28`, verified with SHA-256
`81b06ce7c19cf3b634b0271c23c361ba6ad8bf45fb8b036abbfeb4260ec1e126`. Only `src/rlgl.h` and its
bundled `src/external/glad.h` loader are exposed to the renderer target. The raylib CMake project is
never configured.

For an offline build, point CMake at a checkout whose root contains `src/rlgl.h`:

```bash
cmake -S . -B cmake-build-rlgl \
      -DCNA_GRAPHICS_RENDERER=RLGL \
      -DFETCHCONTENT_SOURCE_DIR_RLGL=/absolute/path/to/raylib
```

The dependency uses raylib's zlib/libpng license. Its required notice is preserved in
[`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

### Updating rlgl

1. Select an immutable upstream raylib release commit; never use a moving branch.
2. Recalculate the official commit archive's SHA-256 and update all three
   `CNA_RLGL_REVISION`, `CNA_RLGL_SOURCE_URL`, and `CNA_RLGL_SOURCE_SHA256` cache defaults in
   `cmake/ThirdPartyRlgl.cmake`.
3. Review the `src/rlgl.h`, `src/external/glad.h`, and license diffs. In particular, re-audit the
   implementation macros, profile defines, loader callback, global state, default resources,
   exposed wrapper signatures, pixel formats, and any private-bridge gap.
4. Configure from a clean build directory, build the renderer and smoke target, run the focused
   runtime smoke, identity/runtime/platform source gates, and every implemented RLGL resource/state
   suite.
5. Record the new revision, behavioral changes, exact test environment, and results in
   `plans/plan_rlgl.md`, this document, and the third-party notice if its text changed.

## Diagnostics

- Initialization reports the granted GL version through CNA's renderer log. Upstream rlgl
  `TRACELOG` output is routed through CNA's logger rather than printed by a raylib subsystem.
- A context below OpenGL 3.3 core, missing default rlgl texture/shader, loader failure, GL error
  during initialization/readback, or a second live device produces an explicit exception.
- For a deterministic Linux software-driver reproduction, set
  `SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1` and run `cna_test_rlgl_smoke` directly.
- An unsupported resource path names its owning `RLGL-*` plan task. Do not replace these failures
  with no-ops while bringing up new functionality.

The dedicated `.github/workflows/rlgl-ci.yml` lane configures the Linux renderer against an
independently checked-out copy of the exact upstream commit and compiles the renderer plus smoke
executable. It intentionally does not yet claim hosted runtime coverage; promotion to a CI runtime
gate belongs to `RLGL-021` after the runner's context path is proven.
