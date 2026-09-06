# WebGPU compiled-Effect translation spike — `plans/plan_webgpu.md` `WEBGPU-166` / `WEBGPU-203`

Can the pinned WebGPU stack run a **compiled XNA Effect**? Measured on **wgpu-native v29.0.1.1**
(`~/deps/wgpu-native-v29.0.1.1`) against an AMD Radeon 780M (RADV, Mesa 25.0.7) under `Xvfb :131`,
and against the **emdawnwebgpu** port shipped with the emsdk in this sandbox, on 2026-09-06.

**Answer: yes natively, through MojoShader's `spirv` profile plus one bounded SPIR-V rewrite.
No in the browser, because the browser's shader ingestion accepts WGSL and nothing else.**

## Build and run

```sh
export CCACHE_DIR=$HOME/.cache/ccache CCACHE_BASEDIR=/rv
W=$HOME/deps/wgpu-native-v29.0.1.1
M=$HOME/deps/FNA3D/MojoShader
D="-DMOJOSHADER_NO_VERSION_INCLUDE -DMOJOSHADER_USE_SDL_STDLIB -DUSE_SDL3 \
   -DMOJOSHADER_EFFECT_SUPPORT -DMOJOSHADER_DEPTH_CLIPPING -DMOJOSHADER_FLIP_RENDERTARGET \
   -DMOJOSHADER_XNA4_VERTEX_TEXTURES -DSUPPORT_PROFILE_ARB1=0 -DSUPPORT_PROFILE_ARB1_NV=0 \
   -DSUPPORT_PROFILE_BYTECODE=0 -DSUPPORT_PROFILE_D3D=0 -DSUPPORT_PROFILE_HLSL=0 \
   -DSUPPORT_PROFILE_METAL=0"

mkdir -p spikes/webgpu-spirv-spike/obj
for f in mojoshader mojoshader_common mojoshader_effects \
         profiles/mojoshader_profile_common profiles/mojoshader_profile_glsl \
         profiles/mojoshader_profile_spirv; do
  ccache gcc -std=gnu11 -O1 -g -w $D -I$M -I$M/profiles $(pkg-config --cflags sdl3) \
    -c $M/$f.c -o spikes/webgpu-spirv-spike/obj/$(basename $f).o
done
ccache g++ -std=c++23 -O1 -g -o spikes/webgpu-spirv-spike/webgpu_spirv_spike \
  spikes/webgpu-spirv-spike/webgpu_spirv_spike.cpp spikes/webgpu-spirv-spike/obj/*.o \
  -I$W/include -I$M $D $(pkg-config --cflags sdl3) \
  -L$W/lib -lwgpu_native $(pkg-config --libs sdl3) -Wl,-rpath,$W/lib

DISPLAY=:131 ./spikes/webgpu-spirv-spike/webgpu_spirv_spike \
  modules/renderers/fna3d/effects/*.fxb tests/fixtures/compiled-effects/*.fxb
```

The MojoShader checkout must carry CNA's patch series (`cmake/patches/`), **including the two this
spike produced** — without them the last two questions below have different answers.

The binary and `obj/` are gitignored; the sources and this file are the artefacts.

## Q1 — is the SPIR-V instance feature askable at runtime? **No, and asking aborts the process.**

`plans/plan_webgpu.md` `WEBGPU-166` was written expecting `wgpuHasInstanceFeature()` to answer
whether the implementation advertises `WGPUInstanceFeatureName_ShaderSourceSPIRV`. In
wgpu-native v29.0.1.1 **both** query entry points are exported symbols that panic:

```
thread '<unnamed>' panicked at src/lib.rs:689:5:
not implemented: wgpuGetInstanceFeatures is not implemented
...
thread '<unnamed>' panicked at src/lib.rs:696:5:
not implemented: wgpuHasInstanceFeature is not implemented
...
thread caused non-unwinding panic. aborting.
```

It is `panic_cannot_unwind`, so it is not a false return and not a catchable failure — it kills the
process. **A renderer must never call either function against this pin.** What works instead is to
name the feature in `WGPUInstanceDescriptor::requiredFeatures` and let instance creation succeed or
fail; here it succeeds.

## Q2 — does a shader module build from MojoShader's SPIR-V? **The vertex one does; the pixel one needs a rewrite.**

Profile string `"spirv"` (SPIRV_MODE_VK: real `DescriptorSet`/`Binding` decorations), not
`"glspirv"` — the same distinction `tools/graphics/mojoshader_vulkan_probe.cpp` records for
`FX-064`. `MOJOSHADER_linkSPIRVShaders`'s return value is the trailing patch-table size to subtract
from `output_len`; for `CnaConformanceEffect.fxb` that is 7440 bytes.

The emitted module's own decorations, read straight out of the word stream:

```
vertex: 416 words, 4 uniforms, 0 samplers, 2 attributes
    OpDecorate %5  Location 0        (vs_v0)         POSITION0
    OpDecorate %6  Location 1        (vs_v1)         TEXCOORD0
    OpDecorate %15 DescriptorSet 1 Binding 0  (vs_uniforms)
pixel : 346 words, 3 uniforms, 1 samplers, 1 attributes
    OpDecorate %36 Location 0        (ps_oC0)
    OpDecorate %6  DescriptorSet 2 Binding 0  (ps_s0)
    OpDecorate %20 DescriptorSet 3 Binding 0  (ps_uniforms)
```

Two facts a renderer can build on:

* **MojoShader's four fixed descriptor sets are exactly WebGPU's four bind groups.**
  `mojoshader_profile_spirv.h` fixes them at 0 = vertex samplers, 1 = vertex uniforms,
  2 = pixel samplers, 3 = pixel uniforms, and core WebGPU's `maxBindGroups` is 4. No remapping.
* **Vertex input locations come out of `MOJOSHADER_parse` in the shader's own declaration order**
  (`v0` → 0, `v1` → 1), which is what the Vulkan backend already relies on. This spike reads them
  back out of the module rather than assuming it.

The vertex module was accepted unchanged. The pixel module was rejected:

```
In wgpuDeviceCreateShaderModule, label = 'spike-ps-A'
  invalid id %12
```

`%12` is `OpLoad %11 %6`, where `%11 = OpTypeSampledImage %10` — a **combined image sampler**.
That is Vulkan's `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` shape, and WGSL has no such type: a
texture binding and a sampler binding are always two separate resources, so naga's SPIR-V frontend
refuses a load of a combined global. This is the ONLY construct in MojoShader's output that WebGPU's
shading model cannot express; everything else passed untouched.

`spirv_split_combined_samplers.hpp` in this directory is the rewrite, and it is small because
MojoShader's emission pattern is fixed: one `OpTypeSampledImage` global per sampler register, loaded
and immediately sampled. Splitting it into an image variable plus a sampler variable and
reconstructing the combined value with `OpSampledImage` **under the original load's result id**
keeps every downstream instruction valid with no renumbering. With it, both modules are accepted.

One ordering detail the fixtures found: the new type declarations must be anchored after the **last**
`OpTypeSampledImage`, not the first. `EnvironmentMapEffect.fxb` declares a 2D and a cube sampled
image, and the cube image type is declared after the first `OpTypeSampledImage` — anchoring on the
first put an `OpTypePointer` in front of the type it referenced, reported as `invalid id`.

## Q2b — wgpu-native's own `wgpuDeviceCreateShaderModuleSpirV`? **Unavailable on this pin.**

```
In wgpuDeviceCreateShaderModuleSpirV, label = 'spike-vs-B'
  Features Features { features_wgpu: FeaturesWGPU(PASSTHROUGH_SHADERS), ... }
  are required but not enabled on the device
```

`WGPUNativeFeature_PassthroughShaders = 0x00030036` is **commented out** in this release's `wgpu.h`,
so there is no supported way to request it. The passthrough entry point is therefore not a route
here — which is no loss, since it would also bypass naga's validation.

## Q3 — does a pipeline built from that pair render the effect's pixels? **Yes.**

The spike builds the four bind-group layouts from the sets above (the pixel sampler set now holding
two entries per register), packs the uniform buffers from MojoShader's register file with the same
16-byte-slot-per-register rule `VulkanCompiledEffect::CaptureUniformSnapshotEXT` uses, binds a white
1×1 texture, and draws a clip-space quad into a 64×64 `RGBA8Unorm` target.

`CnaConformanceEffect.fx`'s `MainPixelShader` is `sampled * Tint * Gain * saturate(Intensity + Thresholds[0])`.
The registers MojoShader actually wrote for technique 0 pass 0 were

```
ps c0 = (0.25, 0, 0, 0)          Gain
ps c1 = (0.1, 0.2, 0.3, 0.4)     Tint
ps c2 = (0.5, 0, 0, 0)           Lighting.Intensity, Thresholds[0] left at 0
```

so the expected result is `(1,1,1,1) * (0.1,0.2,0.3,0.4) * 0.25 * 0.5`, i.e.
`(0.0125, 0.025, 0.0375, 0.05)`, i.e. `(3, 6, 10, 13)` in 8-bit with rounding. The readback is
**exactly `(3, 6, 10, 13)`** — the GPU computed the compiled shader's arithmetic on the registers it
was given, not something that merely looked plausible.

One trap: the entry point is **not** `"main"`. MojoShader names it `ShaderFunction<N>` and reports it
in `MOJOSHADER_parseData::mainfn`; hardcoding `"main"` fails pipeline creation with
`Unable to find entry point 'main'`, and wgpu then aborts the process at submit rather than
returning an error.

## Q4 — does it hold for every committed fixture, or just one lucky shader?

Every technique and every pass of every compiled-effect fixture in the repository, translated,
split, and handed to `wgpuDeviceCreateShaderModule`:

| Fixture | Passes | Failures |
|---|---|---|
| `CnaConformanceEffect.fxb` | 3 | 0 |
| `SpriteEffect.fxb` | 1 | 0 |
| `BasicEffect.fxb` | 1 | 0 |
| `AlphaTestEffect.fxb` | 1 | 0 |
| `DualTextureEffect.fxb` | 1 | 0 |
| `EnvironmentMapEffect.fxb` | 1 | 0 |
| `SkinnedEffect.fxb` | 1 | 0 |
| `racing-shadow-map-xna4.fxb` (real XNA 4.0 game content) | 4 | 1 |
| `racing-normal-mapping-xna4.fxb` (real XNA 4.0 game content) | 14 | 5 |

The 14-fixture `crash-corpus/` was swept too: no crash, no hang, every malformed input refused by
name at parse time or at module creation.

> **The two racing rows were first measured as 0 failures, and that number was wrong.** It was taken
> against a `~/deps/FNA3D/MojoShader` checkout that was silently missing two of CNA's own patches
> (`mojoshader.h`'s hunks from `effect-parser-robustness`, and the whole of
> `legacy-texcoord-input`) — a partially-patched shared tree, not the state the repository declares.
> With the full series applied, 21 of the 27 passes translate and 6 do not. The table above is the
> corrected measurement. The lesson is worth more than the number: **verify that a shared dependency
> checkout matches the patch set the repository declares before measuring anything through it.**
> `git -C ~/deps/FNA3D/MojoShader apply --reverse --check` on every patch is the one-line check.

**Getting the racing fixtures to parse at all needed two MojoShader fixes, both shipped as patches
in `cmake/patches/` and both gaps in the SPIR-V profile that the GLSL profile does not have** —
which matters, because the GLSL profile is what EasyGL runs, so these were parity gaps rather than
WebGPU gaps:

1. **`TEXCRD` was `EMIT_SPIRV_OPCODE_UNIMPLEMENTED_FUNC`.** Both racing fixtures failed to parse at
   all with `TEXCRD unimplemented in spirv profile`. CNA had already implemented it for GLSL
   (`mojoshader-6333f74-glsl-texcrd.patch`); `mojoshader-6333f74-spirv-texcrd.patch` is the same
   instruction for SPIR-V.
2. **`ps_1_x` colour output had no `Location` decoration.** `ps_1_x` has no `oC#` register: `r0`
   *is* the colour output, and nothing decorated it, so the entry-point interface was illegal —
   `Entry point arguments and return values must all have bindings`. Shipped as
   `mojoshader-6333f74-spirv-ps1x-interface.patch`.

Both are additive and profile-local; they change no behaviour for shaders that already worked.

**A third fix was written, measured, and then DELETED, which is worth recording.** On the
partially-patched tree the `ps_1_x` texcoord varyings kept their `0xDEADBEEF` placeholder, because a
`t#` register appeared in no `pixel->attributes` entry for `MOJOSHADER_spirv_link_attributes()` to
find. A third loop in that linker fixed it. Once the tree was repaired it turned out CNA's own
`legacy-texcoord-input.patch` **already** registers those pairs
(`add_attribute_register(ctx, REG_TYPE_TEXTURE, regnum, MOJOSHADER_USAGE_TEXCOORD, regnum, …)`), so
the placeholder never occurs with the full series and the extra loop was solving a problem the
repository had already solved. It was removed rather than kept "just in case": a patch that cannot
fire is a patch nobody can reason about.

### What still does not translate, and why it is not a WebGPU defect

Six of the eighteen racing passes produce a module naga rejects with
`Multiple bindings at location 1 are present`. Dumping the finished module's decorations names the
shape exactly:

```
Location 0  on %28  (ps_r0)     <- the ps_1_x colour OUTPUT
Location 0  on %31  (ps_v0)     <- a COLOR varying INPUT (a different space; not the collision)
Location 1  on %64  (ps_t0)
Location 2  on %66  (ps_t1)
```

The collision is between `ps_t0` and the second TEXCOORD0 fragment input the profile creates for the
`gl_PointCoord`-or-`TEXCOORD0` dual-purpose variable, which the linker addresses through
`pTable->attrib_offsets[TEXCOORD][0]` — the same table slot `legacy-texcoord-input` now gives to the
`ps_1_x` `t0` register. **This reproduces with the linker loop above removed**, so it is neither
caused nor masked by anything in this spike, and it lives in MojoShader's shared SPIR-V linker —
which means the **Vulkan and SDL_GPU** compiled-effect backends have it too. It is recorded as a
shared limitation rather than patched speculatively here: the fix belongs to whoever can measure it
against all three backends. A pass that hits it is refused BY NAME at module creation, never drawn
as nothing.

**These are shared fixes, not WebGPU-local ones.** The Vulkan (`FX-065`) and SDL_GPU (`FX-061`)
backends consume the same SPIR-V profile and the same linker, so both gain `ps_1_x` support from
this patch series — which is also why the evidence belongs here rather than in a WebGPU-only note.

## Q5 — the browser. **SPIR-V is not ingestible there at all.**

Not a runtime measurement but a source one, and it is unambiguous. The emsdk's emdawnwebgpu port
implements `wgpuDeviceCreateShaderModule` in JavaScript
(`emdawnwebgpu_pkg/webgpu/src/library_webgpu.js`), and its chained-struct switch has exactly one
case:

```js
switch (sType) {
  case ShaderSourceWGSL: {
    desc["code"] = WebGPU.makeStringFromStringView(...);
    break;
  }
  default: abort('unrecognized ShaderModule sType');
}
```

`WGPUShaderSourceSPIRV` is declared by the port's `webgpu.h` and its sType is in the enum tables,
but no binding consumes it: a SPIR-V chained struct aborts the module with assertions on, and
produces an **empty** `code` string without them. SPIR-V is not part of the WebGPU specification,
and the browser is the implementation — so this is not a port bug to work around.

### The WGSL routes, and why none was taken in this pass

| Candidate | Verdict | Evidence |
|---|---|---|
| MojoShader `spirv` → browser | **Infeasible** | the switch above; SPIR-V is not a WebGPU shader source |
| wgpu-native `…SpirvV` passthrough | **Infeasible in the browser, and unavailable natively** | Q2b; there is no wgpu-native in a browser at all |
| A MojoShader **WGSL profile** | **Feasible, not affordable here** | the profiles are pluggable (`mojoshader_profile_spirv.c` is the precedent) but that file alone is 4 500 lines; a WGSL twin is a new emitter, not a patch |
| **SPIR-V → WGSL for MojoShader's own emitted subset**, written in CNA | **Feasible, and the recommended route** | the subset is generated code with a small opcode repertoire; it would reuse this spike's parse/reflect/link/split half unchanged and share the runtime with native |
| naga or Tint compiled to WebAssembly | **Feasible, rejected on dependency cost** | `cargo`/`rustc` exist in this sandbox but neither naga nor Tint is vendored; both are general-purpose shader translators, which is the "Rosetta Stone" `plans/plan_webgpu.md` `WEBGPU-203` rules out |

The browser gap is therefore an **implementation gap with a named route**, not a fundamental
blocker: nothing about WGSL prevents expressing a Shader Model 2/3 program. `WEBGPU-204` records the
split decision — SPIR-V natively, browser deferred — and `WEBGPU-171` requires the Emscripten build
to report `SupportsCompiledEffects() == false` rather than throwing at draw time.
