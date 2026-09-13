#version 450

// Stride 52: VertexPositionNormalTextureSkinned -- float3 pos + float3 normal + float2 uv +
// float4 blendWeight + ubyte4 blendIndices (non-normalized, read as uvec4).
layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in vec4  inBoneWeights;
layout(location = 4) in uvec4 inBoneIndices;

// Interface matches lit_textured3d.frag.glsl's inputs exactly (same locations/types) -- this
// shader's fragment stage IS lit_textured3d's fragment shader, reused unchanged; SkinnedEffect's
// lighting math is identical to BasicEffect's lit path once the position/normal are skinned.
layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragTint;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec4 fragFog;    // REMED-GFX-009 xyz=FogColor, w=keep-factor
layout(location = 5) out vec3 fragVertexLit;
layout(location = 6) out vec3 fragVertexSpecular;

// 72 * mat4 = 4608 bytes -- too large for SDL_gpu's measured 4096-byte pushed-uniform limit.
// A 288x1 RGBA32F texture stores the same column-major palette as four adjacent texels per matrix.
// Unlike the former graphics-storage buffer route, this is accepted by SDL_gpu's D3D12 backend as
// well as Vulkan and is expressible on Metal. The 4608-byte row is also naturally 256-byte aligned,
// avoiding D3D12's slow unaligned texture-upload fallback.
layout(set = 0, binding = 0) uniform sampler2D bonePalette;

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

// Mirrors lit_textured3d.vert.glsl's own LitLightParams shape exactly (byte-identical layout),
// which is exactly why the fragment shader can be reused unchanged -- the only addition is
// WeightsPerVertex, packed into eyePos_weightsPerVertex.w (mirrors VulkanRenderer's own
// skinned3d.vert.glsl packing this into otherwise-unused padding).
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
// VulkanRenderer's FogParams shape byte-for-byte.
layout(set = 1, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;        // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} fog;

vec3 safeNormalize(vec3 v) {
    float len2 = dot(v, v);
    return len2 > 0.0 ? v * inversesqrt(len2) : vec3(0.0);
}

mat4 loadBone(uint index) {
    int firstTexel = int(index) * 4;
    return mat4(
        texelFetch(bonePalette, ivec2(firstTexel + 0, 0), 0),
        texelFetch(bonePalette, ivec2(firstTexel + 1, 0), 0),
        texelFetch(bonePalette, ivec2(firstTexel + 2, 0), 0),
        texelFetch(bonePalette, ivec2(firstTexel + 3, 0), 0));
}

void main() {
    // Matches VulkanRenderer's own skinned3d.vert.glsl: FNA's real Skin(vin, boneCount)
    // only sums the first WeightsPerVertex (1, 2, or 4) weight/index pairs.
    float weightsPerVertex = lp.eyePos_weightsPerVertex.w;
    mat4 skinMat = loadBone(inBoneIndices.x) * inBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += loadBone(inBoneIndices.y) * inBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += loadBone(inBoneIndices.z) * inBoneWeights.z
                                          + loadBone(inBoneIndices.w) * inBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(inPos, 1.0);
    gl_Position = pc.mvp * skinnedPos;
    fragUV = inUV;
    // REMED-GFX-006: FNA composes the bone-skin 3x3 with the outer world normal matrix
    // (SkinnedEffect.fx Skin() then Lighting.fxh's mul(normal, WorldInverseTranspose)). The world
    // factor was missing entirely (audit Variant A), so any rotated or non-uniformly-scaled skinned
    // model was lit as if World were identity. Computed in-shader as transpose(inverse(mat3(world)))
    // because lp.world is already present and this renderer mirrors VulkanRenderer's own
    // (now-fixed) skinned3d.vert.glsl.
    mat3 skinNormalMatrix = transpose(inverse(mat3(lp.world)));
    fragNormal = normalize(skinNormalMatrix * (mat3(skinMat) * inNormal));
    fragWorldPos = (lp.world * skinnedPos).xyz;
    fragTint = pc.diffuseColor;
    vec3 N = fragNormal;
    vec3 E = safeNormalize(lp.eyePos_weightsPerVertex.xyz - fragWorldPos);
    vec3 nL0 = safeNormalize(pc.light0Dir);
    vec3 nL1 = safeNormalize(lp.light1Dir_pad.xyz);
    vec3 nL2 = safeNormalize(lp.light2Dir_pad.xyz);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    vec3 lightSum = pc.ambientColor + NdotL0 * pc.light0Diffuse
                    + NdotL1 * lp.light1Diffuse_pad.xyz + NdotL2 * lp.light2Diffuse_pad.xyz;
    fragVertexLit = clamp(lightSum * pc.diffuseColor.rgb + lp.emissiveColor_pad.xyz,
                          0.0, 1.0);
    vec3 h0 = safeNormalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, lp.specularColorPower.w);
    vec3 h1 = safeNormalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, lp.specularColorPower.w);
    vec3 h2 = safeNormalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, lp.specularColorPower.w);
    fragVertexSpecular = clamp(
        (spec0 * lp.light0Specular_pad.xyz + spec1 * lp.light1Specular_pad.xyz
         + spec2 * lp.light2Specular_pad.xyz) * lp.specularColorPower.xyz,
        0.0, 1.0);
    // REMED-GFX-009: keep-factor from raw object-space Z (GFX-005 corrected form
    // (z+FogEnd)/(FogEnd-FogStart)); FogStart==FogEnd -> fully fogged (FNA SetFogVector). keep=1 ->
    // no fog, keep=0 -> full FogColor. Skinned shaders use the PRE-skin inPos.z (matches Vulkan).
    float fogKeep = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
    fragFog = vec4(fog.fogColorEnabled.xyz, fogKeep);
}
