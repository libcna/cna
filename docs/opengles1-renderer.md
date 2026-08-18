# OpenGL ES 1.1 graphics renderer

## Status

The OpenGLES1 renderer was added on **2026-07-21** as CNA's sixth graphics renderer: a genuine
**OpenGL ES 1.1 fixed-function ("Common", CM profile)** implementation, deliberately independent
of the EasyGL renderer. EasyGL targets WebGL2/OpenGL ES 3.0 (a shader-based, programmable
pipeline) and cannot create an ES 1.1 context at all — there is no shared code between the two.

Select it with:

```bash
cmake -S . -B cmake-build-opengles1 \
  -DCNA_GRAPHICS_RENDERER=OPENGLES1 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-opengles1 -j
```

This requires a **real system OpenGL ES 1.1 library and Khronos headers** — e.g. on Debian/
Ubuntu:

```bash
sudo apt-get install libgles1 libgles-dev
```

which provides `libGLESv1_CM.so` plus `GLES/gl.h`/`GLES/glext.h`. `cmake/BackendLibraries.cmake`
`find_library`/`find_path`s these at configure time and fails with a clear `FATAL_ERROR` (same
shape as Vulkan's `find_package(Vulkan REQUIRED)`) if they are absent — this is a hard system
dependency, not a vendored/fetched one.

## A real, empirically-found limitation: Debian's Mesa is built without ES1

During this renderer's own bring-up, a genuine OpenGL ES 1.1 context could **not** be created on
this project's Linux development container, whose only GL driver is Mesa's software rasterizer
(llvmpipe) via EGL. This was verified independently of CNA/SDL3 with a minimal raw EGL program:

- `eglChooseConfig()` with `EGL_RENDERABLE_TYPE = EGL_OPENGL_ES_BIT` **succeeds** and reports
  every enumerated config as ES1-capable (`EGL_CONFORMANT` includes the ES1 bit).
- `eglCreateContext()` on that same config with `EGL_CONTEXT_CLIENT_VERSION = 1` **fails** with
  `EGL_BAD_ALLOC` (`0x3003`), on every config, consistently. (Earlier revisions of this document
  called `0x3003` `EGL_BAD_CONFIG` — that was wrong; `EGL_BAD_CONFIG` is `0x3005`. Mesa's own debug
  output names it exactly: `EGL user error 0x3003 (EGL_BAD_ALLOC) in eglCreateContext:
  dri2_create_context`.)
- The identical program requesting an ES2 context (`EGL_CONTEXT_CLIENT_VERSION = 2`,
  `EGL_RENDERABLE_TYPE = EGL_OPENGL_ES2_BIT`) **succeeds** on the same driver.

In other words: Mesa as packaged here advertises ES1-capable EGL configs but refuses to create an
ES1 context — not a CNA or SDL3 bug.

**Root cause (established 2026-07-22):** Debian's Mesa source package sets, in its `debian/rules`:

```make
confflags_GLES = -Dgles1=disabled -Dgles2=enabled
```

Debian deliberately builds Mesa **without** OpenGL ES 1.x. Upstream Mesa still implements ES1; the
Debian binary packages simply don't contain it. With `gles1` disabled Mesa reports
`max_gl_es1_version = 0`, which drops `__DRI_API_GLES` from the DRI screen's `api_mask`, so
`dri2_create_context()` rejects the ES1 API — while the EGL config list, computed on a separate
path, keeps advertising every config as ES1-renderable and ES1-conformant. That inconsistency is
exactly what makes the failure look like a driver/hardware problem when it is a build-flag one.

This was confirmed on a Debian 13 host with a **real AMD Radeon 780M (`radeonsi`) GPU**, where all
three available Mesa drivers — `radeonsi`, `llvmpipe`, and `softpipe` — refuse ES1 identically
while ES2 succeeds on every one of them. Per-driver and "software rasterizer only" explanations are
therefore both ruled out. Building and running `cna_test_opengles1_clear_readback` end-to-end on
this same container reaches the identical conclusion through the real renderer/SDL3 stack (which
routes an `SDL_WINDOW_OPENGL` window through GLX on X11, a distinct code path from the raw EGL
spike above, yet fails for the same underlying reason):

```
[WindowDebug] after SDL_CreateWindow: flags=0x622 borderless=false fullscreen=false
terminate called after throwing an instance of 'std::runtime_error'
  what():  OpenGLES1: SDL_GL_CreateContext failed (no OpenGL ES 1.1 driver available on this
  system): Could not create GL context: BadAlloc (insufficient resources for operation)
```

`OpenGLES1Renderer`'s `CreateGLContext()` uses the exact same
`SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES)` +
`SDL_GL_CreateContext()` pattern EasyGL already uses for ES3 (just requesting major=1, minor=1
instead), so it fails identically and throws a clear `std::runtime_error` identifying the missing
driver, rather than silently falling back to something else.

**Practical implication:** this renderer cannot be runtime-verified (context creation, Clear/
Present, any pixel-level test) against Debian's stock Mesa packages, on any GPU. Suitable targets
are:

- **a locally built Mesa configured with `-Dgles1=enabled`** — the cheapest option by far, and
  hardware-independent: a pure-software `softpipe`/`llvmpipe` build is enough to exercise the whole
  renderer including pixel readback. This is the recommended route for CI/dev verification,
- embedded/mobile Linux targets with a vendor ES1 driver (PowerVR, Mali, VideoCore/Broadcom,
  older Android devices via the NDK's `libGLESv1_CM.so`),
- desktop hosts whose GL vendor driver (proprietary NVIDIA/AMD/Intel, or a translation layer such
  as ANGLE, which does implement ES1) genuinely provides ES1 CM.

Note that "a machine with a real GPU" is *not* by itself sufficient, and neither is switching
between Mesa drivers: the gate is Debian's Mesa build flag, so `radeonsi` on real AMD hardware
fails exactly like `llvmpipe` and `softpipe` do.

This mirrors this project's own existing precedent for D3D11/D3D12 (Windows-only, cannot be
validated on a Linux container either) and Vulkan (requires a real Vulkan ICD) — a renderer whose
correctness this repository can assert by code review and compile-time verification, with runtime
pixel verification left to a host that actually has the driver. `cmake/Tests/OpenGLES1Tests.cmake`
registers `OpenGLES1_Clear_Readback` unconditionally (no automatic skip) — on a host without a
real ES1 driver it fails fast with the `SDL_GL_CreateContext failed` message above, same as a
Vulkan test would fail without an ICD.

### Verified behaviour

Against that Mesa the renderer runs for real. Seven test executables
(`ctest -R OpenGLES1`, driven through `scripts/opengles1-test-env.sh`) assert 51 checks via
`GetBackBufferData()`/`RenderTarget2D::GetData()` readback and all pass: clear/present/readback,
`SpriteBatch` (including its transform matrix), colored and textured draws, blend/depth/cull/
sampler state and the direct depth/blend toggles, viewport and scissor clipping, virtual resolution
and window↔logical transforms, directional lighting, linear fog, the alpha test, real GPU vertex/
index buffers, render targets, wireframe emulation, dual texturing, environment mapping and context
loss/restore.

Actually running the code exposed four defects that had shipped as "code complete" after review:
the alpha test was inverted for the Less/Greater family, `RenderTarget2D::GetData()` returned all
zeroes because the renderer never overrode the interface's no-op default, and neither textures nor
vertex/index buffers were rebuilt after a context loss (textures sampled as plain white, buffer
draws rendered nothing). All four are fixed.

Rendering the 39-scene XNA oracle corpus afterwards (`OPENGLES1-78`) found three further real
defects that those targeted tests had missed — sampler state was applied to whichever texture
happened to be bound rather than the one the 3D draw binds, and `TextureAddressMode::Mirror`
degraded to Wrap even though `GL_OES_texture_mirrored_repeat` was available. A third followed
(`OPENGLES1-81`): `DualTextureEffect` ignored the second texture-coordinate set entirely, so its
28-byte two-UV vertex layout fell through to the plain colored path. All three are fixed;
`docs/opengles1-parity-report.md` has the per-scene numbers and the comparison against EasyGL.

One behaviour that looks like a bug but is not: `Clear` deliberately forces the depth mask writable
so the clear value always lands, which means a `SetDepthWriteEnabled(false)` issued *before* a clear
is undone by it. That is OPENGLES1-7's stated behaviour and matches EasyGL; set the toggle after
the clear.

### Getting an ES1-capable Mesa locally — done, and it works

Since the blocker is only Debian's `-Dgles1=disabled`, a side-by-side Mesa build with ES1 turned
back on is enough: no special hardware, no replacing the system Mesa, and **no root** — the build
below was actually performed and produced a working ES1 driver on this host on 2026-07-22.

Toolchain first. `meson`, `bison`, `flex` and a few `-dev` headers are missing from this host, but
none of them need `sudo`: `apt-get download` works as an ordinary user, and `dpkg-deb -x` unpacks
into a private prefix.

```bash
mkdir -p ~/deps/es1-toolchain/debs && cd ~/deps/es1-toolchain/debs
apt-get download m4 bison flex libfl-dev libexpat1-dev \
    libxcb-dri3-dev libxcb-present-dev libxcb-glx0-dev libxshmfence-dev \
    libxcb-dri2-0-dev libxcb-randr0-dev libxcb-shm0-dev libxcb-sync-dev \
    libxcb-xfixes0-dev libxcb-shape0-dev libxcb-keysyms1-dev libxcb-util-dev \
    libx11-xcb-dev libxcb-render0-dev libxfixes-dev libxdamage-dev
for d in *.deb; do dpkg-deb -x "$d" ~/deps/es1-toolchain/root; done
pip install --user meson mako
```

Three gotchas, all of which will fail the build if skipped:

1. The unpacked `bison` cannot find its own skeleton files — set `BISON_PKGDATADIR` (and `M4`,
   since `m4` is not on the system either).
2. `xcb/dri3.h` uses a *quoted* `#include "xcb.h"`, so it resolves against its own directory. The
   unpacked headers live in a different prefix from the system `libxcb1-dev` ones, so symlink the
   system `xcb` headers alongside them.
3. The `-dev` packages' `.so` symlinks are relative and therefore dangle inside the private prefix.
   Repoint them at the real system libraries and expose the directory through `LIBRARY_PATH`
   (`meson` has already baked its link commands by then, so a late `-L` will not help).

```bash
TC=~/deps/es1-toolchain/root
for h in /usr/include/xcb/*.h; do
    [ -e "$TC/usr/include/xcb/$(basename $h)" ] || ln -s "$h" "$TC/usr/include/xcb/"
done
for l in $TC/usr/lib/x86_64-linux-gnu/*.so; do
    [ -L "$l" ] && [ ! -e "$l" ] && ln -sf "/usr/lib/x86_64-linux-gnu/$(readlink $l)" "$l"
done

export PATH="$HOME/.local/bin:$TC/usr/bin:$PATH"
export PKG_CONFIG_PATH="$TC/usr/lib/x86_64-linux-gnu/pkgconfig:$TC/usr/share/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig"
export CPPFLAGS="-I$TC/usr/include"
export BISON_PKGDATADIR="$TC/usr/share/bison" M4="$TC/usr/bin/m4"
export LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu"
```

Then Mesa itself (software drivers only — ES1 conformance here is about the fixed-function
pipeline, not GPU throughput):

```bash
git clone --depth 1 -b mesa-25.0.7 https://gitlab.freedesktop.org/mesa/mesa.git ~/deps/mesa
meson setup ~/deps/mesa/build-es1 ~/deps/mesa \
    --prefix=$HOME/deps/mesa-es1-install --buildtype=release \
    -Dgles1=enabled -Dgles2=enabled -Dopengl=true \
    -Dgallium-drivers=softpipe,llvmpipe -Dvulkan-drivers= \
    -Dplatforms=x11 -Dglx=dri -Degl=enabled -Dgbm=enabled -Dvideo-codecs=
ninja -C ~/deps/mesa/build-es1 -j4 install
```

Confirm `gles1: enabled` in meson's summary, and `-DHAVE_OPENGL_ES_1=1` in the compile lines.

To run the tests against it — system Mesa untouched — use the wrapper committed alongside this
document, which sets the loader variables (and forces SDL off Wayland onto X11):

```bash
scripts/opengles1-test-env.sh ctest --test-dir cmake-build-opengles1 -R OpenGLES1 --output-on-failure
```

Do not trust a PASS without checking the ES1 path is genuinely live: the renderer prints its
context version at startup, and it must read `OpenGL ES-CM 1.1` — anything else means the loader
silently fell back to the system Mesa.

## Implemented baseline

- Window/context lifecycle: real ES 1.1 context creation via SDL3, `Clear()`/`Present()`,
  `SetVirtualResolution()`/`SetPresentationMode()`, `TransformWindowToLogical()`/
  `TransformLogicalToWindow()` (same `FixedHeightDynamicWidth` scaling math as every other
  renderer), `DebugSimulateContextLoss()`/`DebugRestoreContext()` (destroy + recreate).
- `Texture2D` creation and level-0 upload (`glTexImage2D`), sampler filter/wrap state
  (`glTexParameteri`), anisotropic filtering where `GL_EXT_texture_filter_anisotropic` is present
  (OPENGLES1-86), and `ITextureRenderer::GetData` readback (OPENGLES1-89) — through a scratch
  framebuffer where FBOs exist, falling back to the CPU copy otherwise.
- Real GPU-side vertex/index buffer objects (`OpenGLES1VertexBufferRenderer`/
  `OpenGLES1IndexBufferRenderer`) — `glGenBuffers`/`glBindBuffer`/`glBufferData` turned out to be
  **core** ES 1.1 entry points (confirmed directly against the real system `GLES/gl.h`), so every
  draw binds the real buffer and uses byte offsets with `glVertexPointer`/`glColorPointer`/
  `glTexCoordPointer`/`glNormalPointer`/`glDrawElements`, not client-side memory. The index buffer
  additionally keeps a small CPU-side shadow of the raw index values, used only for wireframe
  emulation (below). 16-bit indices are core; real 32-bit indices need
  `GL_OES_element_index_uint` (OPENGLES1-83) and are refused where it is absent.
- `SpriteBatch` (2D): an orthographic `glOrthof` projection matching XNA's top-left-origin pixel
  convention, `GL_MODULATE` texture environment, per-vertex color, rotation/origin/flip/layer
  handling identical to every other renderer's `SpriteBatch` math. Sizes itself to a bound
  `RenderTarget2D` instead of the window when one is active (see below).
- `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (`BasicEffect` with
  `VertexColorEnabled=true`, no lighting/texture): loads `GL_PROJECTION`/`GL_MODELVIEW` directly
  from XNA's `Matrix::ToColumnMajor()`.
- `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`: dispatches by vertex stride (16/20/24/32, the same
  convention as every other renderer) and translates `GpuDrawParams` onto fixed-function state:
  - texture (`glTexEnvi(GL_MODULATE)`),
  - up to 3 directional lights (`GL_LIGHT0..2`, `glLightfv`/`glMaterialfv`/`glMaterialf`), applied
    under a view-only `GL_MODELVIEW` so world-space light directions land correctly in eye space,
  - fog (`glFog*`, `GL_LINEAR` mode, matching `BasicEffect`'s eye-space start/end convention),
  - a best-effort `glAlphaFunc` mapping of `AlphaTestEffect`'s 4-way tolerance-band test,
  - **`DualTextureEffect`**: real ES 1.1 multitexturing (`glActiveTexture`/`glClientActiveTexture`,
    both core) — two `GL_COMBINE` stages reproduce the exact `(tex0*2) * tex1 * diffuseTint`
    formula, gated on `GL_MAX_TEXTURE_UNITS >= 2`,
  - **`EnvironmentMapEffect`**: real cube-map reflection via `GL_OES_texture_cube_map`'s
    `glTexGeniOES(GL_TEXTURE_GEN_STR_OES, GL_TEXTURE_GEN_MODE_OES, GL_REFLECTION_MAP_OES)` —
    genuine fixed-function automatic reflection-vector texture-coordinate generation, blended with
    the base lit color via a `GL_INTERPOLATE` combine stage.
  - Vertex-stride/extension preconditions not met for a requested dual-texture/env-map draw fall
    back to the plain colored path, matching the skinned/PBR/instanced/custom-effect fallback.
- Off-screen render targets (`OpenGLES1RenderTargetRenderer`, `CreateRenderTarget2D`/
  `SetRenderTarget2D`) via `GL_OES_framebuffer_object`, gated on the extension **and** every
  needed entry point actually resolving via `SDL_GL_GetProcAddress` — returns `nullptr` (not a
  `FATAL_ERROR`) when unavailable, matching `IGraphicsRenderer`'s "unsupported renderer" contract.
- Wireframe emulation (`RasterizerState.FillMode = WireFrame`): re-expands `TriangleList`/
  `TriangleStrip` draws into a `GL_LINES` edge list at draw time (reading real triangle indices
  from the index buffer's CPU shadow for indexed draws), the same technique
  `EasyGLRenderer::DrawWireframe` uses for its own no-`glPolygonMode` problem.
- Render state: `ApplyBlendState` (`GL_OES_blend_func_separate`/`GL_OES_blend_subtract` resolved
  at runtime via `SDL_GL_GetProcAddress`, with a documented fallback when absent),
  `ApplyDepthStencilState`, `ApplyRasterizerState` (cull mode, scissor, wireframe), `ApplySamplerState`,
  `SetScissorRect`, `SetViewport`.
- `ReadBackbuffer()` (`glReadPixels` + row-flip, used by `GraphicsDevice::GetBackBufferData()`).

## Important limitations

This renderer has **no programmable shader path at all** — `CreateEffectRenderer()` keeps
`IGraphicsRenderer`'s base `nullptr` default. The following are **permanent, not "not yet
implemented"**, gaps for a fixed-function ES 1.1 pipeline.

**How an unsupported effect behaves, since 2026-08-18 (plan_gltf.md `GLTF-473`).** Each of these used
to fall through to the plain colored path, and that was worse than it sounds: the colour path binds
`glColorPointer` at byte offset 12, which is a colour in exactly two of CNA's vertex records (stride
16 and stride 24) and is the `NORMAL` in every PBR and skinned one. Six of the eight canonical glTF
fixtures were therefore drawn with per-vertex colours read out of the bytes of their own normals — a
stable, plausible, wrong tint reported as a successful draw. All four draw routes now call the shared
`RequireFixedFunctionClientArrayEXT` guard before touching any GL state, which asks the canonical
layout table whether the record really carries that semantic at that offset and **refuses the draw by
name when it does not**. The message names this renderer, the route, the array, the semantic and
offset it would have read, what the record actually keeps there, `GLTF-473`, and which effect sent the
draw down the fallback. A draw whose record genuinely is a `VertexPositionColor` or
`VertexPositionColorTexture` one is unaffected, and so is this renderer's own 28-byte dual-UV record,
which the shared table does not list and the guard therefore leaves alone.

- **Custom `ShaderEffect` / GLSL shaders** — no shader compiler exists in ES 1.1 at all.
- **`SkinnedEffect`/`SkinnedPbrEffect`** — no vertex skinning without the rare
  `GL_OES_matrix_palette` extension, not implemented.
- **`PbrEffect`** — metallic-roughness BRDF math has no fixed-function equivalent.
- **`EnvironmentMapEffect`'s Fresnel edge-weighting and cube-alpha specular tint** — the base
  reflection mapping itself IS implemented (real `GL_OES_texture_cube_map` reflection-vector
  texgen, see above); only the per-pixel Fresnel weight and `EnvironmentMapSpecular` tint are not,
  since neither has a fixed-function equivalent without much deeper `GL_COMBINE` staging than
  this baseline implements (flat `envMapAmount` blending is used instead).
- **Instancing** (`DrawInstancedPrimitivesEx`) — no fixed-function instancing mechanism exists.
  `DrawInstancedPrimitivesEx` itself is not overridden, so it takes `IGraphicsRenderer`'s own
  throwing default; an `instanceCount > 1` reaching the ordinary route takes the fallback above and
  is refused there unless the record really is a colour one.
- **Multi-stream vertex input** (REMED-GFX-201) — the fixed-function pointer setup binds one
  `GL_ARRAY_BUFFER` and reads every attribute out of it at stride offsets, so there is no second
  per-vertex stream to resolve.
- **Cube-map readback above mip level 0** — a cube face is read back by attaching it to a
  framebuffer, and `GL_OES_framebuffer_object` requires an attached texture's level to be 0. Level
  0 reads back exactly; any level above it is refused. Storage of the whole declared mip chain
  still works, since `glTexImage2D` takes the level directly.
- **Multiple render targets** — ES 1.1 has no MRT mechanism and no CM-registry extension adds one.
  `SetRenderTargets` **refuses** a count above one with `System::NotSupportedException` rather than
  binding the first and silently dropping the rest, and
  `SupportsCapability(MultipleRenderTargets)` reports `false` for the same reason. A single
  `RenderTarget2D` or a single `RenderTargetCube` face binds normally.
- **Vertex declarations this renderer cannot bind faithfully** — the fixed-function pointer setup
  selects its layout from the buffer stride alone, so `REMED-GFX-DECL-GUARD`'s draw-time check
  refuses a declaration that stride cannot represent (`System::NotSupportedException`) rather than
  rendering it from the wrong bytes. The check is asymmetric: only what the caller actually
  declared is verified, never equality against this renderer's own template.
- **`Texture3D`/`OcclusionQuery`** — not implemented, and both now report `false` from
  `SupportsCapability()` rather than falling through a permissive default. `RenderTarget2D`, `TextureCube` and
  `RenderTargetCube` all ARE implemented (OPENGLES1-72/74/84 — the last via per-face framebuffer
  attachment). `OcclusionQuery` is a **confirmed-impossible**, not "not yet implemented", gap —
  direct inspection of the real system `GLES/gl.h`/`GLES/glext.h` found no occlusion-query
  mechanism anywhere in the ES 1.1 CM registry.
- **`ApplyBlendState`'s `BlendFactor`/`InverseBlendFactor`** — ES 1.1 core has no `glBlendColor`/
  `GL_CONSTANT_COLOR`; these two `Blend` ordinals fall back to `SourceAlpha`/`InverseSourceAlpha`.
- **`BlendFunction::Max`/`Min`** — `GL_OES_blend_subtract` only defines Add/Subtract/
  ReverseSubtract, so these need `GL_EXT_blend_minmax` (OPENGLES1-88). They are honoured where
  that extension is present and fall back to Add only where it is absent.
- **Two-sided stencil** — ES 1.1 core has no separate front/back stencil functions; only the
  clockwise (front) face's stencil state is applied.
- **`RasterizerState.DepthBias`/`SlopeScaleDepthBias`** — ES 1.1 has no `glPolygonOffset`; both are
  silently ignored. (`FillMode = WireFrame` IS implemented, via `GL_LINES` re-expansion — see
  above.)
- **MSAA** — backbuffer multisampling is requested at context creation and reported honestly
  (OPENGLES1-87): `SupportsCapability(MultiSampleAntiAliasing)` returns whether the driver actually
  granted more than one sample, not a hardcoded answer. Render-target multisampling is still not
  implemented.
- ~~Combining per-vertex `Color` with `BasicEffect.DiffuseColor`~~ — **implemented**
  (OPENGLES1-92). The fixed-function pipeline has one "current color" input to `GL_MODULATE`, so
  the two are folded together through a 1x1 white carrier texture and a `GL_COMBINE` stage, which
  reproduces XNA's multiply. When `VertexColorEnabled` is false, `DiffuseColor` is still used as
  the flat constant color.
- **Compressed texture formats (DXT/BC)** — not implemented; same cross-renderer gap every other
  CNA renderer currently has.
- **Render-target multisampling** — `CreateRenderTarget2D`'s `multiSampleCount` is accepted but
  ignored. `mipMap` IS honoured (OPENGLES1-85) where `glGenerateMipmapOES` resolves, and degrades
  to a single level where it does not. Cube render targets honour neither.

`GraphicsCapability::CustomEffects`, `MultipleRenderTargets`, `OcclusionQuery`, `Texture3D` and
`MultiStreamVertexInput` report `false` from `SupportsCapability()`. `AnisotropicFiltering` and `MultiSampleAntiAliasing` are answered from
what the driver actually granted (`GL_EXT_texture_filter_anisotropic`'s reported maximum and the
context's real sample count respectively), so both can report either value;
`WireFrame` now reports `true` (2026-07-21) — query before relying on any of these, per that
method's own documented contract.

## Architecture notes

Unlike every other CNA graphics renderer, there is no shader compilation, no bind groups/descriptor
sets, and no pipeline objects — the entire draw path is GL's global matrix stack
(`glMatrixMode`/`glLoadMatrixf`), texture environment stage (`glTexEnv*`), and per-vertex
lighting/fog fixed-function state, reconfigured on every draw call. Vertex/index data lives in
real GPU buffer objects (`glGenBuffers`/`glBufferData` — confirmed core ES 1.1, not the optional
extension originally assumed during this renderer's initial bring-up), bound via `glBindBuffer`
before each draw with `glVertexPointer`/etc. taking byte offsets rather than raw pointers.
`SpriteBatch`'s own internal quad batcher is the one place that still uses genuine CPU-side
client arrays directly (`glVertexPointer`/`glDrawElements` from a `std::vector`) — a deliberate,
simpler choice for a batch that's rebuilt fresh every flush anyway, not a limitation.

Effects with no natural single-`GL_TEXTURE_ENV`-stage mapping (`DualTextureEffect`,
`EnvironmentMapEffect`) are implemented via ES 1.1's real multi-stage `GL_COMBINE` texture
environment and multitexturing (`glActiveTexture`/`glClientActiveTexture`, both core), plus, for
environment mapping, `GL_OES_texture_cube_map`'s automatic reflection-vector texture-coordinate
generation (`glTexGeniOES(..., GL_REFLECTION_MAP_OES)`) — the exact fixed-function technique this
extension exists for. Wireframe rendering has no native fixed-function path at all (no
`glPolygonMode`), so it's synthesized by re-expanding each triangle into a `GL_LINES` edge list at
draw time instead, the same technique already established by `EasyGLRenderer::DrawWireframe`
for its own (different) "no polygon-mode" problem.

See `plan_opengles1.md` for task-level status and the remaining work.
