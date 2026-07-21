#version 450

// Stride 56: the stride-52 SkinnedEffect vertex layout (VertexPositionNormalTextureSkinned) with
// a per-vertex Color (normalized ubyte4) appended at offset 52, matching
// EasyGLGraphicsBackend::ApplyLayout's stride==56 case exactly -- float3 pos + float3 normal +
// float2 uv + float4 blendWeight + ubyte4 blendIndices (non-normalized) + ubyte4 color (normalized).
layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in vec4  inBoneWeights;
layout(location = 4) in uvec4 inBoneIndices;
layout(location = 5) in vec4  inColor;

// Interface matches skinned_colored3d.frag.glsl's inputs exactly -- unlike the stride-52
// skinned3d.vert.glsl (which reuses lit_textured3d.frag.glsl unchanged), this dedicated stride-56
// pair carries an extra fragVertexColor varying so the fragment stage can multiply it into the
// FINAL combined diffuse+specular output (see that shader's own doc comment for why the multiply
// must happen there, not earlier).
layout(location = 0) out vec2  fragUV;
layout(location = 1) out vec3  fragNormal;
layout(location = 2) out vec4  fragTint;
layout(location = 3) out vec3  fragWorldPos;
layout(location = 4) out vec4  fragVertexColor;
layout(location = 5) out vec4 fragFog;    // REMED-GFX-009 xyz=FogColor, w=keep-factor

layout(std430, set = 0, binding = 0) readonly buffer BoneBlock {
    mat4 bones[72];
} bb;

layout(set = 1, binding = 0) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    vec3  ambientColor;
    float lightingEnabled;
    vec3  light0Dir;
    float textureEnabled;
    vec3  light0Diffuse;
    float vertexColorEnabled;
} pc;

layout(set = 1, binding = 1) uniform SkinnedLightParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    vec4 emissiveColor_pad;
    mat4 world;
    vec4 eyePos_weightsPerVertex;  // w = WeightsPerVertex
    vec4 light0Specular_pad;
    vec4 light1Specular_pad;
    vec4 light2Specular_pad;
    vec4 specularColorPower;
} lp;

// REMED-GFX-009: fog forwarded to the fragment stage as a varying (the shared PC block is fully
// packed, no spare bytes). Keep-factor computed from raw object-space Z, matching
// VulkanGraphicsBackend's FogParams shape byte-for-byte.
layout(set = 1, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogStartEnd;      // x = FogStart, y = FogEnd, zw = unused
} fog;

void main() {
    float weightsPerVertex = lp.eyePos_weightsPerVertex.w;
    mat4 skinMat = bb.bones[inBoneIndices.x] * inBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bb.bones[inBoneIndices.y] * inBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bb.bones[inBoneIndices.z] * inBoneWeights.z
                                          + bb.bones[inBoneIndices.w] * inBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(inPos, 1.0);
    gl_Position = pc.mvp * skinnedPos;
    fragUV = inUV;
    // REMED-GFX-006: matches skinned3d.vert.glsl -- compose the bone-skin 3x3 with the outer world
    // normal matrix transpose(inverse(mat3(world))). The world factor was missing entirely (audit
    // Variant A); see skinned3d.vert.glsl for the full rationale.
    mat3 skinNormalMatrix = transpose(inverse(mat3(lp.world)));
    fragNormal = normalize(skinNormalMatrix * (mat3(skinMat) * inNormal));
    fragWorldPos = (lp.world * skinnedPos).xyz;
    fragTint = pc.diffuseColor;
    fragVertexColor = inColor;
    // REMED-GFX-009: keep-factor from raw object-space Z (GFX-005 corrected form
    // (z+FogEnd)/(FogEnd-FogStart)); FogStart==FogEnd -> fully fogged (FNA SetFogVector). keep=1 ->
    // no fog, keep=0 -> full FogColor. Skinned shaders use the PRE-skin inPos.z (matches Vulkan).
    float fogKeep = (fog.fogColorEnabled.w > 0.5)
        ? ((abs(fog.fogStartEnd.y - fog.fogStartEnd.x) < 1e-6) ? 0.0
            : clamp((inPos.z + fog.fogStartEnd.y) / (fog.fogStartEnd.y - fog.fogStartEnd.x), 0.0, 1.0))
        : 1.0;
    fragFog = vec4(fog.fogColorEnabled.xyz, fogKeep);
}
