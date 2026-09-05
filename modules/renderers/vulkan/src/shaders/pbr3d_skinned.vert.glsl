#version 450

// SkinnedPbrEffect vertex shader — stride 68 (VertexPositionNormalTangentTextureSkinned): the
// stride-48 PbrEffect layout (position+normal+tangent+uv) with the stride-52/56 skinning suffix
// (BlendWeight, BlendIndices) appended. Bone-palette skin transform (mirrors skinned3d.vert.glsl's
// own weightsPerVertex-gated sum) applied to position/normal/tangent before the TBN basis is
// built in the fragment stage — mirrors
// EasyGLRenderer::EnsurePbrSkinnedProgram()'s vertex stage.
layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec4  aTangent;
layout(location = 3) in vec2  aUV;
layout(location = 4) in vec4  aBoneWeights;
// plans/plan_vulkan.md VULKAN-151: `vec4`, not `uvec4`. XNA lets a content processor spell
// BLENDINDICES as Byte4 or as Vector4 (plans/plan_fx.md FX-127, and CustomModelAnimation's own
// SkinnedModelProcessor writes Vector4), and a Vulkan shader input cannot take both an integer and
// a float attribute. Taking the indices as floats lets ONE shader serve both spellings: Vector4
// binds natively, and Byte4 binds through VK_FORMAT_R8G8B8A8_USCALED -- integer values converted
// to float without normalisation, which is exactly what EasyGL's own
// glVertexAttribPointer(..., GL_UNSIGNED_BYTE, GL_FALSE, ...) does. A bone palette is a handful of
// entries, far inside float's exact-integer range, so nothing is lost in the conversion.
layout(location = 5) in vec4 aBoneIndices;
#ifdef CNA_PBR_DUAL_UV
layout(location = 6) in vec2  aUV1;
#endif
#ifdef CNA_PBR_VERTEX_COLOR
layout(location = 7) in vec4  aColor;
#endif

layout(location = 0) out vec3  vNormal;
layout(location = 1) out vec3  vTangent;
layout(location = 2) out float vBitangentSign;
layout(location = 3) out vec2  vUV;
layout(location = 4) out float vFogFactor;
layout(location = 5) out vec3  vWorldPos;
#ifdef CNA_PBR_DUAL_UV
layout(location = 6) out vec2  vUV1;
#endif
#ifdef CNA_PBR_VERTEX_COLOR
layout(location = 7) out vec4  vColor;
#endif

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
// fog-vector companion slot (mirrors skinned3d.vert.glsl's own fog.eyePos_pad.w packing trick).
layout(set = 0, binding = 6) uniform PbrParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    mat4 world;
    vec4 eyePos_metallic;        // xyz = EyePosition, w = MetallicFactor
    vec4 emissive_roughness;     // xyz = EmissiveFactor, w = RoughnessFactor
    vec4 fogColorEnabled;        // xyz = FogColor, w = WeightsPerVertex (REMED-GFX-010)
    vec4 fogVector;              // REMED-GFX-010: FNA fog vector (xyz + w)
    vec4 alphaTest;              // reference, tolerance, pass weight, fail weight
    vec4 pbrMapScales;           // x = normal scale, y = occlusion strength
    vec4 srgbFlags;              // x=decode base, y=decode emissive, z=encode output, w=decode spec colour
    vec4 specularFresnelInputs;  // xyz = unclamped dielectric F0, w = specular factor
    vec4 textureTransformRows[10];
    vec4 specularTextureTransformRows[4];
#ifdef CNA_PBR_DUAL_UV
    vec4 textureCoordinateSets;  // x = seven-bit per-map TEXCOORD_1 selector mask
#endif
} pbr;

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
    float weightsPerVertex = pbr.fogColorEnabled.w; // REMED-GFX-010: alongside the fog vector
    mat4 skinMat = bb.bones[int(aBoneIndices.x)] * aBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bb.bones[int(aBoneIndices.y)] * aBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bb.bones[int(aBoneIndices.z)] * aBoneWeights.z
                                          + bb.bones[int(aBoneIndices.w)] * aBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(aPos, 1.0);
    gl_Position = pc.mvp * skinnedPos;
    // REMED-GFX-011: matches skinned3d.vert.glsl, which does flip (the comment previously here
    // claimed it never does). Renderer-wide convention -- see pbr3d.vert.glsl.
    gl_Position.y = -gl_Position.y;
    gl_PointSize = 1.0;
    mat3 skinNormalMat = mat3(skinMat);
    // REMED-GFX-006 (Variant B): the normal takes the inverse-transpose of World, not raw World.
    // The previous comment justified raw mat3(pbr.world) as deliberate fidelity to
    // EasyGLRenderer::EnsurePbrSkinnedProgram(), but EasyGL has the same defect -- raw World
    // is only correct for rotation and uniform scale, and diverges from FNA's
    // mul(normal, WorldInverseTranspose) under non-uniform scale. It also contradicted this
    // renderer's own unskinned pbr3d.vert.glsl, which already uses the inverse transpose.
    mat3 worldNormalMat = transpose(inverse(mat3(pbr.world)));
    vNormal = normalize(worldNormalMat * cnaSkinNormal(skinNormalMat, aNormal));
    // Tangent stays on raw World: tangents transform as directions, not as normals (glTF
    // convention, and unchanged from the previous behaviour).
    vTangent = mat3(pbr.world) * (skinNormalMat * aTangent.xyz);
    vBitangentSign = aTangent.w * cnaDirectionHandedness(mat3(pbr.world))
                                * cnaDirectionHandedness(skinNormalMat);
    vUV = aUV;
#ifdef CNA_PBR_DUAL_UV
    vUV1 = aUV1;
#endif
#ifdef CNA_PBR_VERTEX_COLOR
    vColor = aColor;
#endif
    vWorldPos = (pbr.world * skinnedPos).xyz;
    vFogFactor = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), pbr.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
