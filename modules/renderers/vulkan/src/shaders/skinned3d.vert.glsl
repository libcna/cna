#version 450

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aUV;
layout(location = 3) in vec4  aBoneWeights;
// plans/plan_vulkan.md VULKAN-151: `vec4`, not `uvec4`. XNA lets a content processor spell
// BLENDINDICES as Byte4 or as Vector4 (plans/plan_fx.md FX-127, and CustomModelAnimation's own
// SkinnedModelProcessor writes Vector4), and a Vulkan shader input cannot take both an integer and
// a float attribute. Taking the indices as floats lets ONE shader serve both spellings: Vector4
// binds natively, and Byte4 binds through VK_FORMAT_R8G8B8A8_USCALED -- integer values converted
// to float without normalisation, which is exactly what EasyGL's own
// glVertexAttribPointer(..., GL_UNSIGNED_BYTE, GL_FALSE, ...) does. A bone palette is a handful of
// entries, far inside float's exact-integer range, so nothing is lost in the conversion.
layout(location = 4) in vec4 aBoneIndices;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out float vFogFactor;
layout(location = 3) out vec3 vWorldPos;

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
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
    // Task 893: DirectionalLight1/DirectionalLight2 diffuse forwarding.
    vec4 light1Dir_pad;
    vec4 light1Diff_pad;
    vec4 light2Dir_pad;
    vec4 light2Diff_pad;
    // Task 894: World matrix + EyePosition + specular (World doesn't fit in the already-128-byte
    // push constant, so it lives here instead).
    mat4 world;
    vec4 eyePos_pad; // w = WeightsPerVertex (Task 895, packed into otherwise-unused padding)
    vec4 specularColor_power;
    vec4 light0Spec_pad;
    vec4 light1Spec_pad;
    vec4 light2Spec_pad;
    vec4 emissiveColor;   // REMED-GFX-008: pre-folded (emissive + ambient*diffuse)*alpha
} fog;

void main() {
    // Task 895: FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs -- matches XNA's own validated property range, so >=2/>=4 gating suffices.
    float weightsPerVertex = fog.eyePos_pad.w;
    mat4 skinMat = bb.bones[int(aBoneIndices.x)] * aBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bb.bones[int(aBoneIndices.y)] * aBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bb.bones[int(aBoneIndices.z)] * aBoneWeights.z
                                          + bb.bones[int(aBoneIndices.w)] * aBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(aPos, 1.0);
    gl_Position = pc.mvp * skinnedPos;
    gl_Position.y = -gl_Position.y; // Vulkan NDC Y is inverted vs OpenGL (matches textured3d.vert.glsl)
    gl_PointSize = 1.0;
    // REMED-GFX-006: FNA composes the bone-skin 3x3 with the outer world normal matrix
    // (SkinnedEffect.fx Skin() then Lighting.fxh's mul(normal, WorldInverseTranspose)).
    // The world factor was missing entirely, so any rotated or non-uniformly-scaled
    // skinned model was lit as if World were identity.
    mat3 skinNormalMatrix = transpose(inverse(mat3(fog.world)));
    vNormal     = normalize(skinNormalMatrix * (mat3(skinMat) * aNormal));
    vUV         = aUV;
    vWorldPos   = (fog.world * skinnedPos).xyz;
    // Task 899: fog factor from the PRE-SKIN raw object-space Z. REMED-GFX-005: corrected to
    // FNA/EasyGL Task-1111 form (z+FogEnd)/(FogEnd-FogStart); prior (FogEnd-z) was the mirror
    // image and wrong. (Bgfx/D3DCommon are still mirrored until their own REMED-GFX-005 pass,
    // so this no longer claims parity with them.)
    vFogFactor = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
