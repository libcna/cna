#version 450

// Task 1103 (plan_graphics.md Phase 80 / plan_dx9.md Divergence 1): per-vertex-lit sibling of
// skinned3d.vert/frag.glsl, mirroring lit_textured3d_vertexlit.vert.glsl's own approach for
// BasicEffect. Skinning math is unchanged; only WHERE lighting is evaluated moves, from the
// fragment stage into this vertex stage (after skinning), Gouraud-interpolated across the
// triangle. Only ever bound when lightingEnabled is true (see the C++ dispatch).

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aUV;
layout(location = 3) in vec4  aBoneWeights;
layout(location = 4) in uvec4 aBoneIndices;

layout(location = 0) out vec2  vUV;
layout(location = 1) out float vFogFactor;
layout(location = 2) out vec3  vLitRGB;
layout(location = 3) out vec3  vSpecularRGB;

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

// Same UBO layout as skinned3d.vert/frag.glsl (set=0, binding=2) — this shader reads more of its
// already-declared fields (the lighting ones the pixel-lit fragment shader used).
layout(set = 0, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
    vec4 light1Dir_pad;
    vec4 light1Diff_pad;
    vec4 light2Dir_pad;
    vec4 light2Diff_pad;
    mat4 world;
    vec4 eyePos_pad; // w = WeightsPerVertex
    vec4 specularColor_power;
    vec4 light0Spec_pad;
    vec4 light1Spec_pad;
    vec4 light2Spec_pad;
    vec4 emissiveColor;   // REMED-GFX-008: pre-folded (emissive + ambient*diffuse)*alpha
} fog;

void main() {
    float weightsPerVertex = fog.eyePos_pad.w;
    mat4 skinMat = bb.bones[aBoneIndices.x] * aBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bb.bones[aBoneIndices.y] * aBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bb.bones[aBoneIndices.z] * aBoneWeights.z
                                          + bb.bones[aBoneIndices.w] * aBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(aPos, 1.0);
    gl_Position = pc.mvp * skinnedPos;
    gl_Position.y = -gl_Position.y; // Vulkan NDC Y is inverted vs OpenGL (matches textured3d.vert.glsl)
    gl_PointSize = 1.0;
    vUV = aUV;
    vec3 worldPos = (fog.world * skinnedPos).xyz;
    vFogFactor = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector

    // REMED-GFX-006: FNA composes the bone-skin 3x3 with the outer world normal matrix
    // (SkinnedEffect.fx Skin() then Lighting.fxh's mul(normal, WorldInverseTranspose)).
    // The world factor was missing entirely, so any rotated or non-uniformly-scaled
    // skinned model was lit as if World were identity.
    mat3 skinNormalMatrix = transpose(inverse(mat3(fog.world)));
    vec3 N = normalize(skinNormalMatrix * (mat3(skinMat) * aNormal));
    vec3 E = normalize(fog.eyePos_pad.xyz - worldPos);
    float dotL0 = dot(N, -normalize(pc.light0Dir));           float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -normalize(fog.light1Dir_pad.xyz));  float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -normalize(fog.light2Dir_pad.xyz));  float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    vec3 lightSum = pc.light0Diffuse * NdotL0
                   + fog.light1Diff_pad.xyz * NdotL1
                   + fog.light2Diff_pad.xyz * NdotL2;
    vLitRGB = lightSum * pc.diffuseColor.rgb + fog.emissiveColor.rgb;

    float specularPower = fog.specularColor_power.w;
    vec3 h0 = normalize(E - normalize(pc.light0Dir));          float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, specularPower);
    vec3 h1 = normalize(E - normalize(fog.light1Dir_pad.xyz)); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, specularPower);
    vec3 h2 = normalize(E - normalize(fog.light2Dir_pad.xyz)); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, specularPower);
    vSpecularRGB = (spec0 * fog.light0Spec_pad.xyz + spec1 * fog.light1Spec_pad.xyz
                   + spec2 * fog.light2Spec_pad.xyz) * fog.specularColor_power.xyz;
}
