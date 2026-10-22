# SDL_GPU ShaderEffect source and bytecode layout

`ShaderEffect` is a CNA extension, not XNA compiled-effect bytecode. Its SDL_GPU
source route requires the optional target-native GLSL compiler. Query the renderer's
capability profile; source shaders remain unavailable on builds without that compiler.

GLSL 450 source and portable SPIR-V now share descriptor reflection. CNA uses the
resources actually declared, remaps them into SDL's dense per-stage slots, and retains
the original texture unit and dimension. It does not assume one sampler or one uniform.

## Fragment samplers

| Source declaration | Meaning |
| --- | --- |
| Native set 2, binding 0..3 | Unit `binding`, with the declared sampler2D/samplerCube/sampler3D dimension |
| Portable set 1, binding 0..3 | Effect-owned 2D unit `binding` |
| Portable set 1, binding 4..7 | Effect-owned cube unit `binding - 4` |
| Portable set 1, binding 8..11 | Effect-owned volume unit `binding - 8` |
| Portable set 0, binding 0 | SpriteBatch's own 2D texture |

Native set2/binding0 **sampler2D** also uses SpriteBatch's texture. A sampler3D at that
binding instead uses `effect.SetTexture(0, volume)`; it must never receive the batch's
2D texture. Sampler unit0 uses the batch's sampler state during SpriteBatch rendering;
other units use their own `SamplerStates[unit]`.

The set1 binding must agree with the declared dimension. Other sets, out-of-range
units, sampler descriptor arrays, array/multisampled images and vertex-stage samplers
are refused with `IsEffectValid() == false` and a nonempty `GetCompileErrorEXT()` before
native shader/pipeline use. Combining layouts still cannot exceed SDL's sixteen
samplers per stage; excess declarations are refused before native shader creation.
This does not implement the separately tracked vertex
texture-fetch feature. A declared cube or volume with no bound texture throws
`InvalidOperationException` while preparing the draw, before it is queued. The 2D
white fallback is not used for cube/volume descriptors.

Example volume fragment source, paired with `effect.SetTexture(0, volume)`:

```glsl
#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outputColor;
layout(set=2,binding=0) uniform sampler3D volume;
void main() { outputColor = texture(volume, vec3(uv, 0.75)); }
```

## Uniforms and coordinates

The native per-draw uniform block is at vertex set1/binding0 and fragment
set3/binding0, if that stage needs it. Its 128-byte std140 shape is:

```glsl
layout(set=1,binding=0) uniform Parameters {
    vec4 viewportSize; // xy supplied by SpriteBatch; zw padding
    mat4 matrix;
    vec4 vectorValue;
    vec4 scalarAndPadding; // scalar value in x
} parameters;
```

Portable uniform descriptors and push-constant blocks retain the existing binding
mapping. Stages declaring no uniform buffers receive no uniform pushes. Uniform
array bindings retain their existing meaning; this repair does not change their ABI.

Source GLSL emits SDL's Y-up clip space as written. For SpriteBatch pixel positions,
use `vec2 ndc = position / parameters.viewportSize.xy * 2.0 - 1.0` and
`gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0)`. Portable bytecode retains its existing
Y correction. Selecting a portable descriptor layout in **source GLSL** does not
change its coordinate convention.

The renderer rules follow SDL's [shader descriptor contract](https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader)
and [typed fragment sampler contract](https://wiki.libsdl.org/SDL3/SDL_BindGPUFragmentSamplers).
Regression coverage: `SdlGpu_ShaderEffect_SourceLayout_*`, offline
`SdlGpuSpirvBindingsTest.*`, and the existing portable two-volume per-unit sampler test.
