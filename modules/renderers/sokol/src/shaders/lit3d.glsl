// SPDX-License-Identifier: MS-PL
//
// plans/plan_sokol.md SOKOL-21: the Sokol renderer's lit 3D program -- BasicEffect with
// LightingEnabled=true (ambient + up to 3 directional lights, Blinn-Phong specular, emissive,
// fog, alpha test). Always samples a texture: SokolRenderer binds a real one when
// TextureEnabled is true and a 1x1 opaque-white fallback otherwise, so this one shader serves
// both "textured and lit" and "vertex-coloured and lit" without a second variant -- the same
// convention the EasyGL renderer's own default-white-texture fallback uses.
//
// This is real per-pixel (Blinn-Phong) lighting, computed in the fragment stage from an
// interpolated world-space normal -- not per-vertex/Gouraud shading. FNA's BasicEffect selects a
// per-vertex-lit shader when PreferPerPixelLighting is false (its own default); this renderer
// renders per-pixel unconditionally regardless of that flag, matching every established CNA
// renderer except D3D9 (see GpuDrawParams::preferPerPixelLighting's own doc).

@module cna

@vs lit3d_vs
layout(binding = 0) uniform lit3d_vs_params {
    mat4 mvp;
    mat4 world;
    // Transpose(Invert(world)), computed CPU-side so a non-uniform scale still transforms the
    // normal correctly; only the upper-left 3x3 is used.
    mat4 normalMatrix;
    // x = VertexColorEnabled (0 or 1); y, z, w unused.
    vec4 flags;
    // x=refVal, y=tolerance, z=passWeight, w=failWeight -- see GpuDrawParams::alphaTest's doc.
    vec4 alphaTest;
    // REMED-GFX-010 (GpuDrawParams::fogVector's own doc): dotted with OBJECT-space position, not
    // world-space -- the vector already bakes World*View's own contribution.
    vec4 fogVector;
};

in vec3 position;
in vec3 normal;
in vec2 texcoord0;
in vec4 color0;

out vec3 worldNormal;
out vec3 worldPos;
out vec2 uv;
out vec4 tint;
out float fogFactor;
out vec4 alphaTestOut;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    worldPos = (world * vec4(position, 1.0)).xyz;
    worldNormal = mat3(normalMatrix) * normal;
    uv = texcoord0;
    tint = mix(vec4(1.0), color0, flags.x);
    // "keep" convention: 1 - saturate(dot(pos, fogVector)) -- all-zero fogVector (fog disabled)
    // gives dot=0 -> keep=1, a true no-op.
    fogFactor = 1.0 - clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0);
    alphaTestOut = alphaTest;
}
@end

@fs lit3d_fs
layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;

layout(binding = 1) uniform lit3d_fs_params {
    vec4 diffuse;
    vec4 ambient;      // xyz used
    vec4 light0Dir;    // xyz used
    vec4 light0Diffuse;
    vec4 light0Specular;
    vec4 light1Dir;
    vec4 light1Diffuse;
    vec4 light1Specular;
    vec4 light2Dir;
    vec4 light2Diffuse;
    vec4 light2Specular;
    vec4 specularColorAndPower; // xyz=SpecularColor, w=SpecularPower
    vec4 eyePosition;  // xyz used
    vec4 emissiveColor; // xyz used
    vec4 fogColor;      // xyz used
    // REMED-GFX-147: 1 when `tex` is a RenderTarget2D's colour attachment, 0 for a plain texture
    // (including the 1x1 white fallback) -- see IsRenderTargetSourceEXT's own doc comment. x used,
    // yzw unused.
    vec4 rtFlipV;
};

in vec3 worldNormal;
in vec3 worldPos;
in vec2 uv;
in vec4 tint;
in float fogFactor;
in vec4 alphaTestOut;

out vec4 frag_color;

void main() {
    vec3 n = normalize(worldNormal);
    vec3 e = normalize(eyePosition.xyz - worldPos);

    float dotL0 = dot(n, -light0Dir.xyz);
    float zeroL0 = step(0.0, dotL0);
    float ndotL0 = max(dotL0, 0.0);
    float dotL1 = dot(n, -light1Dir.xyz);
    float zeroL1 = step(0.0, dotL1);
    float ndotL1 = max(dotL1, 0.0);
    float dotL2 = dot(n, -light2Dir.xyz);
    float zeroL2 = step(0.0, dotL2);
    float ndotL2 = max(dotL2, 0.0);

    vec3 lightSum = ambient.xyz
        + light0Diffuse.xyz * ndotL0
        + light1Diffuse.xyz * ndotL1
        + light2Diffuse.xyz * ndotL2;
    vec3 litRGB = lightSum * diffuse.xyz * tint.rgb + emissiveColor.xyz;

    float specularPower = specularColorAndPower.w;
    vec3 h0 = normalize(e - light0Dir.xyz);
    float spec0 = pow(max(dot(h0, n), 0.0) * zeroL0, specularPower);
    vec3 h1 = normalize(e - light1Dir.xyz);
    float spec1 = pow(max(dot(h1, n), 0.0) * zeroL1, specularPower);
    vec3 h2 = normalize(e - light2Dir.xyz);
    float spec2 = pow(max(dot(h2, n), 0.0) * zeroL2, specularPower);
    vec3 specularRGB =
        (spec0 * light0Specular.xyz + spec1 * light1Specular.xyz + spec2 * light2Specular.xyz)
        * specularColorAndPower.xyz;

    vec2 sampleUv = vec2(uv.x, mix(uv.y, 1.0 - uv.y, rtFlipV.x));
    vec4 texel = texture(sampler2D(tex, smp), sampleUv);
    vec4 c = texel * vec4(litRGB, diffuse.a * tint.a);
    c.rgb += specularRGB * c.a;

    float pass = (alphaTestOut.y > 0.0)
        ? ((abs(c.a - alphaTestOut.x) < alphaTestOut.y) ? alphaTestOut.z : alphaTestOut.w)
        : ((c.a < alphaTestOut.x) ? alphaTestOut.z : alphaTestOut.w);
    if (pass < 0.0) discard;

    c.rgb = mix(fogColor.xyz, c.rgb, fogFactor);
    frag_color = c;
}
@end

@program lit3d lit3d_vs lit3d_fs
