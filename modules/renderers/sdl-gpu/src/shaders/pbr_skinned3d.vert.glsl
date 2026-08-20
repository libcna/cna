#version 450

// Stride 68: VertexPositionNormalTangentTextureSkinned -- the stride-48 PBR Position+Normal+
// Tangent+TextureCoordinate layout with the stride-52 skinning suffix (BlendWeight, BlendIndices)
// appended, matching skinned3d.vert.glsl's own "append rather than insert" convention.
layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec4  inTangent;
layout(location = 3) in vec2  inUV;
layout(location = 4) in vec4  inBoneWeights;
layout(location = 5) in uvec4 inBoneIndices;
// plans/plan_gltf.md GLTF-465: glTF 2.0 3.9.2 makes COLOR_0 an additional linear multiplier on base
// colour. Declared only in the variants whose pipeline supplies it (strides 60 and 80); the others
// pass opaque white, the multiplier's identity, so the shared fragment stage has one interface.
#ifdef CNA_PBR_VERTEX_COLOR
layout(location = 6) in vec4 inColor;
#endif

// Interface matches pbr3d.frag.glsl's inputs exactly (same locations/types) -- this shader's
// fragment stage IS pbr3d.frag.glsl, reused unchanged; SkinnedPbrEffect's BRDF is identical to
// PbrEffect's once the position/normal/tangent are skinned (SkinnedDrawCommand precedent).
layout(location = 0) out vec2  fragUV;
layout(location = 1) out vec3  fragNormal;
layout(location = 2) out vec3  fragTangent;
layout(location = 3) out float fragBitangentSign;
layout(location = 4) out vec3  fragWorldPos;
layout(location = 5) out vec4 fragFog;    // REMED-GFX-009 xyz=FogColor, w=keep-factor
layout(location = 6) out vec4  fragColor0;

// 72 * mat4 = 4608 bytes, a real storage buffer rather than a uniform push -- see
// skinned3d.vert.glsl's own doc comment for why (SDL_gpu's real ~4096-byte push-uniform cap on
// this Vulkan-backed environment).
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

// Mirrors skinned3d.vert.glsl's own SkinnedLightParams exactly (byte-identical to
// LitLightParams, WeightsPerVertex packed into eyePos_weightsPerVertex.w) -- pbr3d.frag.glsl
// reads this same block as its plain LitLightParams (eyePos_pad.w is simply never read there).
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

vec3 cnaSkinNormal(mat3 m, vec3 n) {
    vec3 c0 = m[0], c1 = m[1], c2 = m[2];
    vec3 co0 = cross(c1, c2), co1 = cross(c2, c0), co2 = cross(c0, c1);
    float det = dot(c0, co0);
    vec3 transformed = mat3(co0, co1, co2) * n;
    return abs(det) > 1e-6 ? transformed * sign(det) : m * n;
}

float cnaDirectionHandedness(mat3 m) {
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

void main() {
    // Matches skinned3d.vert.glsl: FNA's real Skin(vin, boneCount) only sums the first
    // WeightsPerVertex (1, 2, or 4) weight/index pairs.
    float weightsPerVertex = lp.eyePos_weightsPerVertex.w;
    mat4 skinMat = bb.bones[inBoneIndices.x] * inBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bb.bones[inBoneIndices.y] * inBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bb.bones[inBoneIndices.z] * inBoneWeights.z
                                          + bb.bones[inBoneIndices.w] * inBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(inPos, 1.0);
    gl_Position = pc.mvp * skinnedPos;
    fragUV = inUV;
    #ifdef CNA_PBR_VERTEX_COLOR
    fragColor0 = inColor;
    #else
    fragColor0 = vec4(1.0);
    #endif

    // REMED-GFX-006 (Variant B): the normal takes the inverse-transpose world matrix
    // transpose(inverse(mat3(world))), not raw mat3(lp.world). Raw World is correct only for
    // rotation and uniform scale and diverges from FNA's mul(normal, WorldInverseTranspose) under
    // non-uniform scale; it also contradicted this renderer's own unskinned pbr3d.vert.glsl and its
    // (now-fixed) Vulkan sibling pbr3d_skinned.vert.glsl. The tangent stays on raw World: tangents
    // transform as directions, not as normals (glTF convention, unchanged).
    mat3 skinNormalMat = mat3(skinMat);
    mat3 worldNormalMat = transpose(inverse(mat3(lp.world)));
    fragNormal = normalize(worldNormalMat * cnaSkinNormal(skinNormalMat, inNormal));
    fragTangent = mat3(lp.world) * (skinNormalMat * inTangent.xyz);
    fragBitangentSign = inTangent.w * cnaDirectionHandedness(mat3(lp.world))
                                   * cnaDirectionHandedness(skinNormalMat);
    fragWorldPos = (lp.world * skinnedPos).xyz;
    // REMED-GFX-009: keep-factor from raw object-space Z (GFX-005 corrected form
    // (z+FogEnd)/(FogEnd-FogStart)); FogStart==FogEnd -> fully fogged (FNA SetFogVector). keep=1 ->
    // no fog, keep=0 -> full FogColor. Skinned shaders use the PRE-skin inPos.z (matches Vulkan).
    float fogKeep = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
    fragFog = vec4(fog.fogColorEnabled.xyz, fogKeep);
}
