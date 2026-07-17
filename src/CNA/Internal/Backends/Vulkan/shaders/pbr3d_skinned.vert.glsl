#version 450

// SkinnedPbrEffect vertex shader — stride 68 (VertexPositionNormalTangentTextureSkinned): the
// stride-48 PbrEffect layout (position+normal+tangent+uv) with the stride-52/56 skinning suffix
// (BlendWeight, BlendIndices) appended. Bone-palette skin transform (mirrors skinned3d.vert.glsl's
// own weightsPerVertex-gated sum) applied to position/normal/tangent before the TBN basis is
// built in the fragment stage — mirrors
// EasyGLGraphicsBackend::EnsurePbrSkinnedProgram()'s vertex stage.
layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec4  aTangent;
layout(location = 3) in vec2  aUV;
layout(location = 4) in vec4  aBoneWeights;
layout(location = 5) in uvec4 aBoneIndices;

layout(location = 0) out vec3  vNormal;
layout(location = 1) out vec3  vTangent;
layout(location = 2) out float vBitangentSign;
layout(location = 3) out vec2  vUV;
layout(location = 4) out float vFogFactor;
layout(location = 5) out vec3  vWorldPos;

layout(push_constant) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    vec3  ambientColor;
    float lightingEnabled;
    vec3  light0Dir;
    float textureEnabled;
    vec3  light0Diffuse;
    float vertexColorEnabled;
} pc;

layout(set = 0, binding = 5) uniform BoneBlock {
    mat4 bones[72];
} bb;

// Same field shapes as pbr3d.vert.glsl's PbrParams, plus WeightsPerVertex packed into
// fogStartEnd_weights.z (mirrors skinned3d.vert.glsl's own fog.eyePos_pad.w packing trick).
layout(set = 0, binding = 6) uniform PbrParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    mat4 world;
    vec4 eyePos_metallic;        // xyz = EyePosition, w = MetallicFactor
    vec4 emissive_roughness;     // xyz = EmissiveFactor, w = RoughnessFactor
    vec4 fogColorEnabled;        // xyz = FogColor, w = fogEnabled
    vec4 fogStartEnd_weights;    // x = fogStart, y = fogEnd, z = WeightsPerVertex, w = unused
} pbr;

void main() {
    // Task 899-family precedent: skinned3d.vert.glsl never Y-flips (unlike lit_textured3d.vert.glsl
    // et al.) -- this shader is a direct extension of that exact skinning transform, so it mirrors
    // that convention exactly rather than introducing a mismatched Y-flip here.
    float weightsPerVertex = pbr.fogStartEnd_weights.z;
    mat4 skinMat = bb.bones[aBoneIndices.x] * aBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bb.bones[aBoneIndices.y] * aBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bb.bones[aBoneIndices.z] * aBoneWeights.z
                                          + bb.bones[aBoneIndices.w] * aBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(aPos, 1.0);
    gl_Position = pc.mvp * skinnedPos;
    // EasyGLGraphicsBackend::EnsurePbrSkinnedProgram() uses plain mat3(uWorld) here (NOT the
    // inverse-transpose uNormalMatrix the unskinned PbrEffect vertex shader uses) -- preserved
    // exactly rather than "corrected", per this port's own behavior-fidelity requirement.
    mat3 skinNormalMat = mat3(skinMat);
    vNormal = normalize(mat3(pbr.world) * (skinNormalMat * aNormal));
    vTangent = mat3(pbr.world) * (skinNormalMat * aTangent.xyz);
    vBitangentSign = aTangent.w;
    vUV = aUV;
    vWorldPos = (pbr.world * skinnedPos).xyz;
    vFogFactor = (pbr.fogColorEnabled.w > 0.5)
        ? clamp((pbr.fogStartEnd_weights.y - aPos.z) / max(pbr.fogStartEnd_weights.y - pbr.fogStartEnd_weights.x, 1e-6), 0.0, 1.0)
        : 1.0;
}
