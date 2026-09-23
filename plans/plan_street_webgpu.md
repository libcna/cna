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
| STREETW-0002 | cna-street: the atmospheric sky was GLSL or SPIR-V only, so a WGSL renderer drew no sky | ✅ |
| STREETW-0003 | WebGPU: the whole frame sits half a pixel off the other renderers' | 📏 measured, not fixed |

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

**Fix, in cna-street** (`6d45b96`): the branch asks `GetShaderDialectEXT()`, and the packaged
variant covers everything that is not GLSL. The sky package gained a WGSL payload beside its SPIR-V
one, generated from the same `sky.vulkan.*.glsl` sources by `tools/shader_package/generate_shader_package.py`
(naga-cli 28.0.0 — 29.x needs rustc 1.87 and this machine has 1.85), so the three variants cannot
drift apart. EasyGL and Vulkan take exactly the branches they took before, which their unchanged
captures confirm.

One trap: `ShaderCodeEXT` has a text constructor and a binary one, and WGSL is text. Handing the
WGSL through the binary overload is refused at construction — "a binary payload requires a binary
shader format" — which surfaces as a fatal exception out of `Game::Run()`, not as a shader error.

---

## STREETW-0003 — what still differs, and why it is not fixed here

After `STREETW-0001` and `STREETW-0002` the 18 viewpoints differ from the OPENGL33 capture by
8–32 % of pixels on WEBGPU, against 0.3–1.9 % on VULKAN. Measured, in order:

* **Not noise.** Each renderer is deterministic: two captures of the same renderer differ by
  0.00 %.
* **Not tone.** Region means agree to within 1–5/255 and the difference is not monotonic in
  brightness, so it is not a gamma, exposure or tone-map difference.
* **Not sharpness.** The high-frequency detail energy of the same oblique facades, road and
  canopy matches to three decimal places, so it is not filtering, anisotropy or mip selection.
* **Not anti-aliasing.** `--preset medium` turns MSAA off in all three and the gap grows slightly
  rather than closing.
* **Not a pass.** With `--no-bloom --no-ssao --no-fog --no-shadows --no-ibl --no-light-shafts
  --no-probes` it is still 26 %, so it is in the base opaque pass.
* **It is a sub-pixel offset.** A search over sub-pixel shifts finds the WebGPU frame displaced by
  about **half a pixel in both x and y**: aligning it drops the mean absolute difference from 4.64
  to 3.32/255, and the difference map is exactly the texture detail and silhouettes of otherwise
  identical geometry.

That is this renderer's long-standing XNA-pixel-centre-convention gap, which
[`plan_webgpu.md`](plan_webgpu.md) already records by name — `WebGPU_PointSamplingContract` and
`WebGPU_DescriptorCapacityContract` are the two failures it has carried for months, and both are in
the ten that fail on this branch with and without `STREETW-0001`.

Not fixed here, deliberately: changing the pixel-centre convention moves every draw on the WebGPU
renderer and rewrites the expectations of its whole test corpus. It wants a task of its own in
`plan_webgpu.md`, with the owner's decision, rather than being changed as a side effect of a demo
bring-up. What this row adds is the measurement: the gap is not confined to two contract tests, it
displaces every frame the renderer produces.
