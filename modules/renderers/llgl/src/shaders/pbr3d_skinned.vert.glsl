// SPDX-License-Identifier: MS-PL
// SkinnedPbrEffect vertex shader, Vulkan flavour (SPIR-V).
//
// Combines pbr3d.vert.glsl's TBN-basis output with skinned3d.vert.glsl's own weightsPerVertex-
// gated bone blend. Shares the EXACT SAME PbrParams uniform block (and binding number) as the
// unskinned pbr3d.vert.glsl/frag.glsl -- WeightsPerVertex was already reserved as
// roughnessWeightsPad.y ("unused by this shader" there) precisely so this variant needs no shape
// change. BoneBlock is placed at a HIGHER binding (16) than every texture/sampler pair (2-15)
// rather than shifting them, so primitivePbrFragmentShader_ -- compiled against those exact
// binding numbers -- can be reused verbatim for both the skinned and unskinned pipelines; only the
// vertex shader differs.

#version 450

layout(std140, binding = 1) uniform PbrParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    vec4 ambientColorPad;      // xyz = AmbientLightColor, w = decode base colour
    vec4 emissiveMetallic;
    vec4 roughnessWeightsPad;  // x=RoughnessFactor, y=WeightsPerVertex, z=NormalScale, w=OcclusionStrength
    vec4 light0DirPad;         // xyz = direction, w = encode output
    vec4 light0DiffusePad;
    vec4 light1DirPad;
    vec4 light1DiffusePad;
    vec4 light2DirPad;
    vec4 light2DiffusePad;
    vec4 eyePositionWorldPad;  // xyz = eye position, w = decode emissive
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
    vec4 dielectricFresnel;    // xyz = unclamped dielectric F0, w = specular factor
    vec4 textureTransformRows[10];
    vec4 specularState;        // x = TEXCOORD_1 selector mask, y = decode specular colour, z = VertexColorEnabled (GLTF-465)
    vec4 specularTextureTransformRows[4];
};

layout(std140, binding = 16) uniform BoneBlock
{
    mat4 bones[72];
};

layout(location = 0) in vec3  position;
layout(location = 2) in vec2  texCoord;
#ifdef CNA_PBR_DUAL_UV
layout(location = 7) in vec2  texCoord1;
#endif
layout(location = 3) in vec3  normal;
layout(location = 4) in vec4  aBoneWeights;
layout(location = 5) in uvec4 aBoneIndices;
layout(location = 6) in vec4  tangent;
// plan_gltf.md GLTF-465: glTF 2.0 3.9.2 makes COLOR_0 an additional linear multiplier on base
// colour. Location 1 is this renderer's colour slot -- the same one the pipeline used to strip
// from every PBR shader's attribute list. Declared only for the variants whose vertex format
// supplies it (strides 60 and 80); the others pass opaque white, the multiplier's identity.
#ifdef CNA_PBR_VERTEX_COLOR
layout(location = 1) in vec4 color;
#endif

layout(location = 0) out vec2  vTexCoord;
layout(location = 1) out vec3  vNormal;
layout(location = 2) out vec3  vTangent;
layout(location = 3) out float vBitangentSign;
layout(location = 4) out vec3  vWorldPos;
layout(location = 5) out float vFogFactor;
layout(location = 6) out vec2  vTexCoord1;
layout(location = 7) out vec4  vColor;

vec3 cnaSkinNormal(mat3 m, vec3 n)
{
    vec3 c0 = m[0], c1 = m[1], c2 = m[2];
    vec3 co0 = cross(c1, c2), co1 = cross(c2, c0), co2 = cross(c0, c1);
    float det = dot(c0, co0);
    vec3 transformed = mat3(co0, co1, co2) * n;
    return abs(det) > 1e-6 ? transformed * sign(det) : m * n;
}

float cnaDirectionHandedness(mat3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    // FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs -- matches skinned3d.vert.glsl's own established gating.
    float weightsPerVertex = roughnessWeightsPad.y;
    mat4 skinMat = bones[aBoneIndices.x] * aBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bones[aBoneIndices.y] * aBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bones[aBoneIndices.z] * aBoneWeights.z
                                          + bones[aBoneIndices.w] * aBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(position, 1.0);
    gl_Position = mvpMatrix * skinnedPos;
    vTexCoord   = texCoord;
    #ifdef CNA_PBR_VERTEX_COLOR
    vColor = color;
    #else
    vColor = vec4(1.0);
    #endif
#ifdef CNA_PBR_DUAL_UV
    vTexCoord1  = texCoord1;
#else
    vTexCoord1  = texCoord;
#endif

    // Bone-skin 3x3 composed with the outer world inverse-transpose normal matrix, mirroring
    // skinned3d.vert.glsl's own REMED-GFX-006 composition order.
    mat3 skinNormalMat = mat3(skinMat);
    mat3 worldNormalMat = transpose(inverse(mat3(worldMatrix)));
    vNormal        = normalize(worldNormalMat * cnaSkinNormal(skinNormalMat, normal));
    // Tangent: skin then raw world (directions transform differently from normals), matching
    // pbr3d.vert.glsl's own documented simplification for the unskinned case.
    vTangent       = mat3(worldMatrix) * (skinNormalMat * tangent.xyz);
    vBitangentSign = tangent.w * cnaDirectionHandedness(mat3(worldMatrix))
                               * cnaDirectionHandedness(skinNormalMat);
    vWorldPos      = (worldMatrix * skinnedPos).xyz;
    // Fog factor from the POST-skin position, matching skinned3d.vert.glsl's own convention.
    vFogFactor     = clamp(dot(skinnedPos, fogVector), 0.0, 1.0) * fogColor.a;
}
