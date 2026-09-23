# cna-street on the WebGPU renderer

Owner brief of 2026-09-23: build `cna-street` (`../cna-street`) against CNA's WebGPU renderer,
compare the result with the renderers the street already runs on, and fix what the comparison
finds.

Branch `street-webgpu` from `next` `c3fae8755`. Task IDs `STREETW-0001`, … . Defects that belong
to cna-street are fixed in cna-street and listed here only for the record; defects that belong to
CNA are fixed here, on the layer they belong to, with a regression test.

The predecessor is [`plan_street.md`](plan_street.md), which did the same for Vulkan on
2026-09-22 and whose `STREET-0001`…`STREET-0008` are in `next`.

## How the comparison is run

One build tree, three renderers, chosen at runtime:

```sh
export CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache \
      -DCNA_GRAPHICS_RENDERERS="OPENGL33;VULKAN;WEBGPU" \
      -DCNA_WEBGPU_ROOT=$HOME/deps/wgpu-native-v29.0.1.1 \
      -DCNA_WEBGPU_COMPILED_EFFECTS=ON        # default stays OPENGL33
cmake --build build -j8

R=../cna/tools/platform/run_gpu_tests_private.sh     # never the live desktop
CNA_GRAPHICS_RENDERER=WEBGPU $R --exec ./build/bin/cna-street --no-audio --no-overlay --capture out/webgpu
./build/bin/compare-images out/opengl33/<view>.png out/webgpu/<view>.png
```

## Status

| ID | Task | Status |
|---|---|---|
| STREETW-0001 | WebGPU: every instanced draw took the position-only `instanced3d` program, whatever effect was applied | ✅ |
| STREETW-0002 | cna-street: the atmospheric sky is GLSL or SPIR-V only, so a WGSL renderer drew no sky | ⬜ |

---

## STREETW-0001 — an instanced PbrEffect draw was rendered by the wrong program

**Symptom.** On WEBGPU every tree, parked car, bench, hydrant, bollard and piece of street
furniture rendered as a flat white silhouette, while the moving vehicles, the pedestrians and the
buildings were correct. 52–98 % of pixels differed from the OPENGL33 capture at every one of the
18 viewpoints; VULKAN differed from OPENGL33 by 0.3–1.9 %.

**Root cause.** `WebGPURenderer::DrawInstancedPrimitivesEx` resolved the *layout* of an instanced
draw from the bound declarations (`WEBGPU-172`) but never its *family*: after the compiled-effect
and custom-`ShaderEffect` branches it always built an `InstancedDrawCommand`, which
`IssueInstancedDraw` renders with `instanced3d.wgsl` — position, an optional COLOR0, and
`u.diffuseColor` as the output. No texture, no normal, no light. Every stock effect other than an
untextured `BasicEffect` therefore rendered with the wrong program and the renderer reported
success. The street splits its props exactly along this line: `CNA::Graphics::InstancedRendererEXT`
for anything placed many times, one draw per item for everything else, all of them through the same
`PbrEffect` — which is why one half of the frame was right and the other half was white.

Vulkan does not share the defect: `VULKAN-222`…`VULKAN-232` gave its instanced route every stock
family, each vertex shader compiled a second time with `CNA_INSTANCED`.

**Fix.** The PBR family on the instanced route, following Vulkan's own composition rule
(`VULKAN-219`): the per-instance matrix applies **inside** the effect's World, which `u.mvp` and
`lp.world` already carry.

* `webgpu_shaders.hpp`'s `kPbr` gained three instance markers beside its existing colour ones, and
  its vertex stage now reads `localPos`, `instancedWorld` and `normalMatrix` — which the
  non-instanced expansion defines as exactly the expressions that were written there before.
* `ExpandPbrVertexColourWgslEXT` expands them, so one marked source still produces every variant.
  Four modules rather than two: WGSL rejects a vertex input with no matching attribute, so a shader
  that declares the instance columns cannot also serve a draw that binds none.
* The four world-matrix columns are `Float32x4` at locations 12–15, the numbering
  `pbr3d.vert.glsl` already uses, and the instance records are materialized at queue time the way
  `CaptureStockVertexStreamsEXT` materializes every other per-instance stream — WebGPU's Instance
  step mode has an implicit divisor of one, so `InstanceFrequency` is honoured by repetition and
  never reaches a native layout.
* The normal matrix composes rather than being recomputed: `(W·I)^-T == W^-T · I^-T`, so the
  CPU-computed `lp.normalMatrixCol*` is multiplied by the instance half, which the shader inverts
  itself because WGSL has no `inverse()`. A singular instance matrix falls back to the identity
  instead of producing NaN.
* An instanced `SkinnedPbrEffect` draw is **refused by name** rather than silently rendered by the
  wrong program. There is no instanced skinned WGSL variant; the street does not need one (its
  crowd is drawn per person), and a named refusal is this renderer's own established answer for a
  route it does not cover.

**Test.** `WebGPU_InstancedPbr3D` (`modules/renderers/webgpu/examples/webgpu_instanced_pbr3d_test.cpp`),
5/5 on the real GPU through the private compositor. It draws through the ordinary XNA surface —
`PbrEffect`, `SetVertexBuffers`, `DrawInstancedPrimitives` — because that is the route the street
reaches and the one that was broken, and every check asserts a colour the position-only program
cannot produce: three instances sampling a red base-colour texture at their own screen positions
while `DiffuseColor` is white; the clear colour surviving away from them; the result following a
texture swap; and an instance rotated away from the only light rendering black while its unrotated
twin is lit — the leg that fails if the instance transform reaches the position but not the normal
matrix.

**Measured.** `ctest -L WebGPU` in `cmake-build-webgpu`: 137/147 pass, the same 10 failures as the
unmodified branch measured the same way (`ContextRecovery`, `RealWindowResize`,
`SpriteBatch_SortMode`, `Viewport_Cardinality`, `Scissor_Cardinality`, `TextureFilterMipContract`,
`DescriptorCapacityContract`, `PointSamplingContract`, `Parity_backbuffer_msaa`,
`Parity_compressed_cube`) — none of them touched by this change.

**One trap worth keeping.** A first run showed two *extra* failures (`WebGPU_DebugMarker`,
`WebGPU_EffectOutlivedByDraw`) that the baseline passed. They were not a regression: this change
adds members to `PbrDrawCommand` and to `WebGPURenderer`, and only `libcna.so` plus the new test
had been rebuilt — every other test executable still carried the old class layout. After a full
`ninja -k 0` both passed again. A renderer-header change means rebuilding the tests, not only the
library.

---

## STREETW-0002 — the street's sky does not exist on a WGSL renderer

**Symptom.** Black sky on WEBGPU, and `the sky shader did not compile: ... found "#"` at start-up:
the renderer was handed `#version 300 es` GLSL.

**Root cause.** `SkySystem::build()` asks `ExecutesShaderEffectSourceEXT()` and takes the GLSL
branch when it is true. WebGPU answers true — it executes shader *source* — but the source it
executes is WGSL (`GetShaderDialectEXT() == Wgsl`). The question the code means to ask is "which
language", not "source or not".

Status: open at the time of writing; see the row above for the current state.
