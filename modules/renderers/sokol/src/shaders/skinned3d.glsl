// SPDX-License-Identifier: MS-PL
//
// plans/plan_sokol.md SOKOL-35: the Sokol renderer's skinned 3D program -- SkinnedEffect, always
// textured and lit (ambient + up to 3 directional lights, Blinn-Phong specular, emissive, fog,
// alpha test), matching FNA's SkinnedEffect.fx exactly (it always samples a texture and always
// lights, unlike BasicEffect's toggles). The fragment stage is a byte-for-byte copy of
// lit3d.glsl's own math -- FNA's SkinnedEffect.fx #includes the identical Lighting.fxh helpers
// BasicEffect.fx does, applied to the post-skin normal/position rather than the raw ones, so the
// two genuinely share the same lighting formula rather than approximating it twice.
//
// The vertex stage blends up to 4 bone matrices per vertex (GpuDrawParams::weightsPerVertex,
// gated 1/2/4 to match FNA's real Skin(vin, boneCount) shader behavior of only summing the first
// N weight/index pairs -- Task 895) and transforms Position (affine) and Normal (via the
// blended matrix's rotation/scale part only) with the result, mirroring
// EasyGLRenderer::EnsureSkinnedProgram()'s own shape: same stride-52 attribute layout
// (position/normal/texcoord0/blendweight0/blendindices0), same mat4[72] bone array, same
// >=2/>=4 weight-count gating, same post-skin fog dot.
//
// blendindices0 arrives as an unnormalized UBYTE4 (VertexElementFormat::Byte4) -- sokol_gfx's own
// vertex-format reflection (_sg_vertexformat_basetype) requires an unsigned-integer GLSL attribute
// for this format, delivered via a genuine integer vertex fetch rather than the float conversion
// every other attribute here uses, so this is the one `uvec4` input in this shader.

@module cna

@vs skinned3d_vs
layout(binding = 0) uniform skinned3d_vs_params {
    mat4 mvp;
    mat4 world;
    // Transpose(Invert(world)), computed CPU-side so a non-uniform scale still transforms the
    // normal correctly; only the upper-left 3x3 is used.
    mat4 normalMatrix;
    // x = VertexColorEnabled (0 or 1); y = weightsPerVertex (1.0, 2.0 or 4.0); z, w unused.
    vec4 flags;
    // x=refVal, y=tolerance, z=passWeight, w=failWeight -- see GpuDrawParams::alphaTest's doc.
    vec4 alphaTest;
    // REMED-GFX-010 (GpuDrawParams::fogVector's own doc): dotted with the POST-SKIN object-space
    // position, matching FNA's Skin() mutating vin.Position before ComputeFogFactor runs.
    vec4 fogVector;
    // SkinnedEffect.fx's Bones[72] palette (SkinnedEffect::MaxBones). Zero-filled entries beyond
    // GpuDrawParams::boneCount are never read: weightsPerVertex/blendweight0 both gate how many
    // of the four indices actually contribute.
    mat4 bones[72];
};

// color0 is declared LAST (not after normal/texcoord0 like lit3d.glsl's own order) because it is
// the only optional attribute of this kind's five -- sokol_gfx requires every valid attribute
// slot to be a continuous prefix with no gap, so the one slot that may legitimately be left
// unbound (no Color element in the declaration) has to be the trailing one, after the two
// always-required skin attributes rather than between them and texcoord0.
in vec3 position;
in vec3 normal;
in vec2 texcoord0;
in vec4 blendweight0;
in uvec4 blendindices0;
in vec4 color0;

out vec3 worldNormal;
out vec3 worldPos;
out vec2 uv;
out vec4 tint;
out float fogFactor;
out vec4 alphaTestOut;

void main() {
    int i0 = int(blendindices0.x);
    int i1 = int(blendindices0.y);
    int i2 = int(blendindices0.z);
    int i3 = int(blendindices0.w);

    mat4 skin = bones[i0] * blendweight0.x;
    if (flags.y >= 1.5) skin += bones[i1] * blendweight0.y;
    if (flags.y >= 3.5) skin += bones[i2] * blendweight0.z + bones[i3] * blendweight0.w;

    vec4 skinnedPos = skin * vec4(position, 1.0);
    vec3 skinnedNormal = mat3(skin) * normal;

    gl_Position = mvp * skinnedPos;
    worldPos = (world * skinnedPos).xyz;
    worldNormal = mat3(normalMatrix) * skinnedNormal;
    uv = texcoord0;
    tint = mix(vec4(1.0), color0, flags.x);
    // "keep" convention: 1 - saturate(dot(pos, fogVector)) -- all-zero fogVector (fog disabled)
    // gives dot=0 -> keep=1, a true no-op.
    fogFactor = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), fogVector), 0.0, 1.0);
    alphaTestOut = alphaTest;
}
@end

@fs skinned3d_fs
layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;

layout(binding = 1) uniform skinned3d_fs_params {
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

@program skinned3d skinned3d_vs skinned3d_fs
