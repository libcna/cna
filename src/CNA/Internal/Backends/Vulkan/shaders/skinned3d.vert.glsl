#version 450

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aUV;
layout(location = 3) in vec4  aBoneWeights;
layout(location = 4) in uvec4 aBoneIndices;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out float vFogFactor;

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

layout(set = 0, binding = 1) uniform BoneBlock {
    mat4 bones[72];
} bb;

// Task 899: fog -- BoneBlock has zero spare capacity, so fog gets its own dedicated dynamic UBO
// at binding=2.
layout(set = 0, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogStartEnd;      // x = fogStart, y = fogEnd, zw = unused
} fog;

void main() {
    mat4 skinMat = bb.bones[aBoneIndices.x] * aBoneWeights.x
                 + bb.bones[aBoneIndices.y] * aBoneWeights.y
                 + bb.bones[aBoneIndices.z] * aBoneWeights.z
                 + bb.bones[aBoneIndices.w] * aBoneWeights.w;
    gl_Position = pc.mvp * skinMat * vec4(aPos, 1.0);
    vNormal     = normalize(mat3(skinMat) * aNormal);
    vUV         = aUV;
    // Task 899: fog factor from the PRE-SKIN raw object-space Z (matches EasyGL/Bgfx's
    // established SkinnedEffect fog formula exactly -- Task 900/899 bonus scope).
    vFogFactor = (fog.fogColorEnabled.w > 0.5)
        ? clamp((fog.fogStartEnd.y - aPos.z) / max(fog.fogStartEnd.y - fog.fogStartEnd.x, 1e-6), 0.0, 1.0)
        : 1.0;
}
