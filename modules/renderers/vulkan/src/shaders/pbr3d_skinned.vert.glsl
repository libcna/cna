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
} pbr;

vec3 cnaSkinNormal(mat3 m, vec3 n) {
    vec3 c0 = m[0], c1 = m[1], c2 = m[2];
    vec3 co0 = cross(c1, c2), co1 = cross(c2, c0), co2 = cross(c0, c1);
    float det = dot(c0, co0);
    vec3 transformed = mat3(co0, co1, co2) * n;
    return abs(det) > 1e-6 ? transformed * sign(det) : m * n;
}

void main() {
    float weightsPerVertex = pbr.fogColorEnabled.w; // REMED-GFX-010: alongside the fog vector
    mat4 skinMat = bb.bones[aBoneIndices.x] * aBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bb.bones[aBoneIndices.y] * aBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bb.bones[aBoneIndices.z] * aBoneWeights.z
                                          + bb.bones[aBoneIndices.w] * aBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(aPos, 1.0);
    gl_Position = pc.mvp * skinnedPos;
    // REMED-GFX-011: matches skinned3d.vert.glsl, which does flip (the comment previously here
    // claimed it never does). Renderer-wide convention -- see pbr3d.vert.glsl.
    gl_Position.y = -gl_Position.y;
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
    vBitangentSign = aTangent.w;
    vUV = aUV;
    vWorldPos = (pbr.world * skinnedPos).xyz;
    vFogFactor = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), pbr.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
