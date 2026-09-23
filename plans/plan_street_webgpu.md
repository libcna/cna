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
| STREETW-0003 | WebGPU: the whole frame sat half a pixel off the other renderers' — XNA's pixel-centre convention was never applied | ✅ |
| STREETW-0004 | WebGPU: every OTHER stock family, instanced — textured, lit, alpha-test, dual-texture, env-map | ✅ |
| STREETW-0005 | WebGPU: the skinned families, instanced — the last gap against Vulkan's instanced set | ✅ |

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

* `kPbr` gained instance markers beside its existing colour ones, expanded by
  `ExpandPbrVertexColourWgslEXT`. **`STREETW-0004` replaced that with the general rewrite** every
  family now shares, and `kPbr` went back to being byte-identical to its pre-task text; the
  description here is kept because it is what this commit did.
* Four modules rather than two: WGSL rejects a vertex input with no matching attribute, so a shader
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
  route it does not cover. `STREETW-0004` widened that refusal to `SkinnedEffect` and left it as
  the one gap between this renderer's instanced set and Vulkan's.

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

## STREETW-0003 — the whole frame was half a pixel off

After `STREETW-0001` and `STREETW-0002` the 18 viewpoints differed from the OPENGL33 capture by
8–32 % of pixels on WEBGPU, against 0.3–1.9 % on VULKAN. What that was, measured in order:

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

**Root cause.** XNA rasterizes under Direct3D 9's convention, where a pixel's centre is the integer
coordinate; WebGPU, OpenGL and Vulkan all put it at the half-integer. Every other CNA renderer
compensates by moving filled geometry just under half a pixel right and down —
`EasyGLRenderer`'s `xnaPixelCenter` matrix and `VulkanRenderer::XnaPixelCenterCorrectionEXT`. The
WebGPU renderer had no such correction at all, on any route. This is the renderer's long-standing
XNA-pixel-centre gap, which [`plan_webgpu.md`](plan_webgpu.md) already records by name:
`WebGPU_PointSamplingContract` and `WebGPU_DescriptorCapacityContract` have carried it for months.

**Fix.** `WebGPURenderer::XnaPixelCenterCorrectionEXT(PrimitiveType)`, Vulkan's field for field —
the same 63/64 scale, the same filled-primitives-only rule (a line or a point has no fill rule, so
the same shift only moves it off the pixels XNA lights), the same multisample exemption
(REMED-GFX-235: at four samples the outer sample positions sit inside the correction's margin, so
it starts *removing* coverage from the outermost row and column), and the same viewport resolution
order — explicit `Viewport`, then the bound render target or cube face, then the surface. It is
post-multiplied onto the WVP of all ten stock 3D families and onto the instanced route's
view-projection. The signs are EasyGL's unchanged: WebGPU's clip space is y-up like OpenGL's and
this renderer's stock WGSL negates nothing, so `+x/-y` is the same half-pixel shift down-and-right.

**Two stated boundaries**, neither changed here:
* The custom-`ShaderEffect` route is not corrected, because a custom program owns its own
  transform — EasyGL does not correct one either.
* The compiled-effect route is not corrected, which matches Vulkan (`VulkanCompiledEffect` has no
  pixel-centre handling either). EasyGL corrects its own through MojoShader, so that difference
  between the three predates this task and outlives it.
* WebGPU exposes no sub-pixel-precision limit, so the guard EasyGL and Vulkan apply — lower the
  scale on a device that cannot represent 63/64 below half — cannot be applied. 63/64 is both
  renderers' own default and is used unconditionally here.

**Measured.**
* `WebGPU_PointSamplingContract`, failing for months, now **passes** (163/163). Its one failing leg
  was `U2`, a 3D textured quad magnified 3×3 → 10×10, 19/100 pixels selecting the neighbouring
  texel; every sprite leg already passed, which is what said the offset was in the 3D raster rather
  than in the composite.
* `WebGPU_DescriptorCapacityContract`, the other half of the recorded pair, passes too.
* `ctest -L WebGPU`: **139/147**, against 137/147 before this row and the same 137 on the
  unmodified branch. Two fixed, none broken.
* The renderer-neutral corpus, A/B rather than argued. A bounded `CnaTests` run on WEBGPU
  (`tools/tests/run_gtest_bounded.sh` inside the private compositor) reached 42 of its 53 shards
  before it was stopped for time, and every failure in it that could plausibly be raster-shaped was
  then run **both ways** from the same build tree — the 196 tests of `IndexedDrawDeferredTest`,
  `ClassicTextureFormat`, `HdrRenderTargetRoundTripTest`, `TextureCubeTest`, `Texture3DTest`,
  `Texture3DTextureCubeContentTypeReaderTest` and `CnjStockEffectTest.CustomGlslEffectStillWorks`,
  with this commit's two source files reverted to `975fd282a` and rebuilt in between. **The same 20
  fail either way**, name for name: no regression and no accidental fix. Their messages say so
  independently — `Expected: (nullptr) != (vulkanRenderer)` for a Vulkan-only test running under
  WEBGPU, "The vertex buffer resource is in use", and a blue channel 255 out rather than a fraction
  of a pixel.
* cna-street, the 18-viewpoint capture against OPENGL33: **8–32 % → 0.5–8.8 %** of pixels
  differing, 17 of the 18 under 3.2 %, against VULKAN's 0.3–1.9 %. Viewpoint 13 alone goes
  28.3 % → 1.7 %, and the sub-pixel search that found the offset now reports its optimum at
  exactly (0, 0), symmetric in both axes. The one viewpoint still above 3 % is the aerial
  `06-above-the-junction`, which is Vulkan's worst case too.

**One test changed, and why it is not a weakened expectation.** `WebGPU_GraphicsState`'s wireframe
leg probed the quad's exact centre. That quad is two triangles sharing the `tr`–`bl` diagonal, and
on a 64×64 target its centre pixel lies within one pixel of that diagonal — so the probe measured
whether the shared EDGE covers a pixel, not whether the interior is filled, and any sub-pixel change
of raster position flips it. The probe moved a quarter of the way in, where the interior genuinely
is interior. This is the same correction `WebGpuWireFrameContract` had already made to its own probe
for the same reason ("the oracle's probe is the shared TRIANGLE's centroid, and asserting it about a
quad measures the quad's diagonal rather than its fill"); this example had kept the centre. The
Solid control and the recovery check moved with it, so the leg is still a differential.

---

## STREETW-0004 — the rest of the stock families, instanced

`STREETW-0001` gave `PbrEffect` its family back because that is what cna-street draws. The defect
was never PBR's: `DrawInstancedPrimitivesEx` built an `InstancedDrawCommand` for **any** stock draw
that carried a per-instance stream, whatever effect was applied, so a textured `BasicEffect`, a lit
one, an `AlphaTestEffect`, a `DualTextureEffect` and an `EnvironmentMapEffect` were all rendered by
`instanced3d.wgsl` — position, an optional COLOR0, and `u.diffuseColor` as the output. Vulkan had
closed the same hole family by family in `VULKAN-222`…`VULKAN-232`.

**The cascade is not a second copy.** The instanced route now asks `SelectStockVertexShapeEXT` —
the *same* function the ordinary route asks — and hands the draw to `DispatchStockDrawEXT` with its
instance stream. An instanced draw and a non-instanced draw of the same buffer and effect therefore
cannot land in different families, which is the property Vulkan's own instanced cascade was written
to have and had to restate by hand. Two shapes stay where they were: `Colored` is what this
renderer's `instanced3d` family exists for (`WEBGPU-27`), and a stride-derived draw that is neither
PBR nor skinned still falls through to it.

**One rewrite, not fourteen copies.** `MakeInstancedStockWgslEXT` is the WGSL counterpart of the
`CNA_INSTANCED` define that `compile_shaders.py` compiles every Vulkan stock source a second time
with. WGSL has no preprocessor, so the rewrite is a checked text transformation in the renderer:
it inserts the four world-matrix columns at locations 12–15, adds the stage input, moves the
object-space position through the instance matrix at the one place each family derives it — which
carries the fog term and the world position with it, because they read that same local — composes
the instance's inverse-transpose into the CPU-computed normal matrix, and folds it into the tangent
basis where a family has one. Every step is checked: a family whose vertex stage does not have the
shape it rewrites throws by name rather than compiling to a shader that ignores its instances.
`STREETW-0001`'s bespoke PBR markers were removed in favour of it, which puts `kPbr` back to being
**byte-identical to its pre-task text**.

The shared parts are shared: `WebGPUInstanceStreamEXT` (the materialized records),
`CaptureInstanceStreamEXT` (`InstanceFrequency` honoured by repetition, as
`CaptureStockVertexStreamsEXT` does it), `BindInstanceStreamEXT`, `AppendInstanceBufferLayoutEXT`
and `StockInstanceSlotEXT`, which derives the native slot from the layout rather than remembering
it so the pipeline and the binding cannot drift.

**One bug this found in existing code.** `WEBGPU-155`'s neutral record — the (0,0,0,1) any stock
input a declaration does not name reads — is bound with a **per-instance step and exactly one
record**, deliberately, because "every stock draw on this route submits exactly one instance". That
stopped being true: wgpu refuses the draw with *"Instance 3 extends beyond limit 1 imposed by the
buffer in slot 1"*. An instanced draw now binds it at a zero stride under vertex step, which reads
record 0 for every vertex of every instance — the same "one value for the whole draw" the record
exists to supply.

**Skinned was refused by name here, and `STREETW-0005` then implemented it.** The refusal is
described because it is what this commit shipped: an instanced `SkinnedEffect` or
`SkinnedPbrEffect` draw threw rather than rendering, which is a named refusal on a route rather
than a silent wrong result.

**Test.** `WebGPU_InstancedStockFamilies`
(`modules/renderers/webgpu/examples/webgpu_instanced_stock_families_test.cpp`), 10/10 on the real
GPU. Each family draws three instances of a small quad through the ordinary XNA surface with a RED
texture and a WHITE `DiffuseColor`, and reads the three instance centres plus one pixel away from
them: red at three distinct positions is a result the position-only program cannot produce, and it
proves the routing and the per-instance transform in one reading.

**Measured.** `ctest -L WebGPU`: **140/148**, the same 8 failures as before this row and as the
unmodified branch — nothing broken, and the 148th test is this row's own. cna-street's 18-viewpoint
capture is unchanged to three decimal places (viewpoint 13: 1.734 % against 1.733 % before), which
is the regression check that matters for `STREETW-0001`'s own path after it was rebuilt on the
shared machinery.

**One test expectation moved, and it is the fix rather than a weakening.**
`WebGPU_InstancedVertexColor_Cardinality` asserted that a stride-24 (position + colour + UV)
instanced draw "builds its OWN Instanced3D variant". It no longer builds one: that record names a
texture coordinate, so the instanced route hands it to `ColoredTextured3D` — the family the
ordinary route has always given it. The leg's subject, that a different declaration gets a pipeline
of its own rather than sharing one, is now counted natively, which is the count that does not
depend on which family owns the draw.

---

## STREETW-0005 — the skinned families, instanced

The last gap between this renderer's instanced set and Vulkan's. `STREETW-0004` refused an
instanced `SkinnedEffect` or `SkinnedPbrEffect` draw by name; it now draws one.

**Nothing new was needed, which is the point.** Every piece was already in place: the WGSL rewrite
has a `Skinned` shape, which moves the instance matrix onto `skinnedPos` — **after** the bone skin,
the composition `VULKAN-231` chose and `EasyGL`'s skinned program uses — and from there the
existing text carries it into `u.mvp`, `lp.world` and the fog term, because they all read that same
local. The normal follows the rule the rewrite already applies: `normalMatrix` is `W^-T`, so the
instance's own `I^-T` multiplies it and the total is `W^-T · I^-T · (bone normal)`. SkinnedPbr's
tangent basis picks the instance up through `mat3(lp.world * cnaInstanceMatrix)`, so a mirroring
instance flips the bitangent exactly as a mirroring World does — `sign(det(W·I)) ==
sign(det(W))·sign(det(I))`, which is what `pbr3d.vert.glsl`'s own header says about its version.

Six modules: `Skinned3D` ×4 (per-pixel/per-vertex lit × with/without the trailing colour) and
`SkinnedPbr3D` ×2 (with/without COLOR_0). These families build their vertex layout by hand rather
than through `BuildStockVertexStateEXT`, so the per-instance columns come from
`FillInstanceBufferLayoutEXT` — factored out of `AppendInstanceBufferLayoutEXT` in this row so that
the stride-derived families and the declaration-driven ones state the layout once between them.

**One bone palette serves every instance.** That is what an instanced skinned draw *means*: the
palette is effect state, not per-instance state, so a crowd sharing one pose is what this draws.
Per-instance palettes would need a storage buffer indexed by `instance_index`, which is a different
feature and is not what Vulkan implements either.

**The route no longer refuses anything.** `DrawInstancedPrimitivesEx` hands every stock shape to
`DispatchStockDrawEXT`; `Colored` keeps the `instanced3d` family it exists for, and an unsupported
stride-derived draw still falls through to it exactly as before.

**Test.** `WebGPU_InstancedStockFamilies` grew two legs, 14/14 on the real GPU: `Skinned3D` at
stride 52 and `SkinnedPbr3D` at stride 68, each with an identity bone so the leg measures the
family and the per-instance matrix rather than the skin, three instances at three screen positions
through a red texture.

**Measured.** `ctest -L WebGPU`: **140/148**, the same 8 failures as the unmodified branch. No
family in this renderer now renders an instanced draw with the wrong program, and none refuses one.
