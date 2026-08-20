// SPDX-License-Identifier: MS-PL
//
// plans/plan_sokol.md SOKOL-33: the Sokol renderer's DualTextureEffect program -- two textures sampled at
// the SAME texcoord0 (this codebase has no second UV-set concept anywhere; every CNA renderer's own
// DualTextureEffect implementation already reuses one shared UV for both textures, matching
// EasyGLRenderer::EnsureDualTextured3DProgram()'s identical shape). FNA's PSDualTexture:
//     color = SAMPLE(Texture);  color.rgb *= 2;  color *= overlay * pin.Diffuse;
// -- a doubling factor on the base texture's RGB (alpha untouched), then multiplied by the overlay
// texture and DiffuseColor. No alpha test: FNA's DualTextureEffect.fx has none.
//
// tint is computed once in the vertex stage, mirroring textured3d.glsl/colored3d.glsl's own
// convention: a vertex declaration with no Color element leaves color0 unbound (reads its generic
// default), and flags.x (VertexColorEnabled) is 0 in exactly that case.

@module cna

@vs dualtextured3d_vs
layout(binding = 0) uniform dualtextured3d_vs_params {
    mat4 mvp;
    vec4 diffuse;
    // x = VertexColorEnabled (0 or 1); y, z, w unused.
    vec4 flags;
    // REMED-GFX-010 (GpuDrawParams::fogVector's own doc): dotted with OBJECT-space position, not
    // world-space -- the vector already bakes World*View's own contribution.
    vec4 fogVector;
};

in vec3 position;
in vec2 texcoord0;
in vec4 color0;

out vec2 uv;
out vec4 tint;
out float fogFactor;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    uv = texcoord0;
    tint = mix(vec4(1.0), color0, flags.x) * diffuse;
    // "keep" convention: 1 - saturate(dot(pos, fogVector)) -- all-zero fogVector (fog disabled)
    // gives dot=0 -> keep=1, a true no-op.
    fogFactor = 1.0 - clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0);
}
@end

@fs dualtextured3d_fs
layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;
layout(binding = 1) uniform texture2D tex2;
layout(binding = 1) uniform sampler smp2;

layout(binding = 2) uniform dualtextured3d_fs_params {
    vec4 fogColor; // xyz used
    // REMED-GFX-147: x for `tex`, y for `tex2` -- 1 when that slot is a RenderTarget2D's colour
    // attachment, 0 for a plain texture. See IsRenderTargetSourceEXT's own doc comment.
    vec4 rtFlipV;
};

in vec2 uv;
in vec4 tint;
in float fogFactor;

out vec4 frag_color;

void main() {
    vec2 uv0 = vec2(uv.x, mix(uv.y, 1.0 - uv.y, rtFlipV.x));
    vec2 uv1 = vec2(uv.x, mix(uv.y, 1.0 - uv.y, rtFlipV.y));
    vec4 base = texture(sampler2D(tex, smp), uv0);
    base.rgb *= 2.0;
    vec4 overlay = texture(sampler2D(tex2, smp2), uv1);
    vec4 c = base * overlay * tint;
    c.rgb = mix(fogColor.xyz, c.rgb, fogFactor);
    frag_color = c;
}
@end

@program dualtextured3d dualtextured3d_vs dualtextured3d_fs
