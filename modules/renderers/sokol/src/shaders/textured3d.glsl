// SPDX-License-Identifier: MS-PL
//
// plans/plan_sokol.md SOKOL-21: the Sokol renderer's textured, unlit 3D program -- BasicEffect with
// TextureEnabled=true, LightingEnabled=false. Also serves AlphaTestEffect's own textured-quad
// shape, since alphaTest's default {0,0,1,1} is a true no-op (see GpuDrawParams::alphaTest's own
// doc) and this shader applies it unconditionally rather than needing a second variant.
//
// tint is computed once in the vertex stage and passed through, mirroring sprite.glsl and
// colored3d.glsl's own convention: a vertex declaration with no Color element leaves color0
// unbound (reads its generic default), and flags.x (VertexColorEnabled) is 0 in exactly that
// case, multiplying the unread attribute out of the result.

@module cna

@vs textured3d_vs
layout(binding = 0) uniform textured3d_vs_params {
    mat4 mvp;
    vec4 diffuse;
    // x = VertexColorEnabled (0 or 1); y, z, w unused.
    vec4 flags;
    // x=refVal, y=tolerance, z=passWeight, w=failWeight -- see GpuDrawParams::alphaTest's doc.
    vec4 alphaTest;
    // REMED-GFX-010 (GpuDrawParams::fogVector's own doc): dotted with OBJECT-space position, not
    // world-space -- the vector already bakes World*View's own contribution.
    vec4 fogVector;
};

in vec3 position;
in vec2 texcoord0;
in vec4 color0;

out vec2 uv;
out vec4 tint;
out vec4 alphaTestOut;
out float fogFactor;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    uv = texcoord0;
    tint = mix(vec4(1.0), color0, flags.x) * diffuse;
    alphaTestOut = alphaTest;
    // "keep" convention: 1 - saturate(dot(pos, fogVector)) -- all-zero fogVector (fog disabled)
    // gives dot=0 -> keep=1, a true no-op.
    fogFactor = 1.0 - clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0);
}
@end

@fs textured3d_fs
layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;

layout(binding = 1) uniform textured3d_fs_params {
    vec4 fogColor; // xyz used
    // REMED-GFX-147: 1 when `tex` is a RenderTarget2D's colour attachment, 0 for a plain texture --
    // see IsRenderTargetSourceEXT's own doc comment. x used, yzw unused.
    vec4 rtFlipV;
};

in vec2 uv;
in vec4 tint;
in vec4 alphaTestOut;
in float fogFactor;

out vec4 frag_color;

void main() {
    vec2 sampleUv = vec2(uv.x, mix(uv.y, 1.0 - uv.y, rtFlipV.x));
    vec4 c = texture(sampler2D(tex, smp), sampleUv) * tint;
    float pass = (alphaTestOut.y > 0.0)
        ? ((abs(c.a - alphaTestOut.x) < alphaTestOut.y) ? alphaTestOut.z : alphaTestOut.w)
        : ((c.a < alphaTestOut.x) ? alphaTestOut.z : alphaTestOut.w);
    if (pass < 0.0) discard;
    c.rgb = mix(fogColor.xyz, c.rgb, fogFactor);
    frag_color = c;
}
@end

@program textured3d textured3d_vs textured3d_fs
