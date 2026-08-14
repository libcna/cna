// SPDX-License-Identifier: MS-PL
// PbrEffect vertex shader, Vulkan flavour (SPIR-V).
//
// Like EnvironmentMapEffect/SkinnedEffect (see env_map3d.vert.glsl/skinned3d.vert.glsl), this does
// NOT share the common `Transform` uniform block -- PbrEffect's own field set (base colour factor
// kept independent from alpha per glTF, raw AmbientLightColor rather than pre-folded emissive,
// metallic/roughness factors, alpha coverage) doesn't fit it, and this vertex/fragment pair is
// never linked with any other shader here.
//
// Needs a `Tangent` vertex element (see MapVertexUsage's own new case) alongside position/normal/
// texCoord -- the tangent-space TBN basis the fragment stage builds for normal mapping is a
// per-vertex-attribute concern no other vertex shader in this renderer has.

#version 450

layout(std140, binding = 1) uniform PbrParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;         // rgb = base colour factor, a = alpha (kept independent, not premultiplied)
    vec4 ambientColorPad;      // xyz = AmbientLightColor, w = decode base colour
    vec4 emissiveMetallic;     // xyz = EmissiveFactor, w = MetallicFactor
    vec4 roughnessWeightsPad;  // x=RoughnessFactor, y=WeightsPerVertex (unused), z=NormalScale, w=OcclusionStrength
    vec4 light0DirPad;         // xyz = direction, w = encode output
    vec4 light0DiffusePad;
    vec4 light1DirPad;
    vec4 light1DiffusePad;
    vec4 light2DirPad;
    vec4 light2DiffusePad;
    vec4 eyePositionWorldPad;  // xyz = eye position, w = decode emissive
    vec4 fogColor;             // xyz = FogColor, w = fogEnabled (0/1)
    vec4 fogVector;
    vec4 alphaTest;            // reference, tolerance, pass weight, fail weight
    vec4 dielectricFresnel;    // xyz = dielectric F0, w = dielectric F90
};

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 normal;
layout(location = 6) in vec4 tangent;

layout(location = 0) out vec2  vTexCoord;
layout(location = 1) out vec3  vNormal;
layout(location = 2) out vec3  vTangent;
layout(location = 3) out float vBitangentSign;
layout(location = 4) out vec3  vWorldPos;
layout(location = 5) out float vFogFactor;

out gl_PerVertex
{
    vec4 gl_Position;
};

float cnaDirectionHandedness(mat3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

void main()
{
    gl_Position = mvpMatrix * vec4(position, 1.0);
    vTexCoord   = texCoord;

    // Normals take the world matrix's inverse-transpose so a non-uniform scale does not skew
    // them, matching every other lit shader here.
    mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));
    vNormal        = normalize(normalMatrix * normal);
    // Tangent transforms as a plain direction under mat3(worldMatrix) (not the normal's inverse-
    // transpose) -- correct for uniform-scale World transforms, matching the Vulkan renderer's own
    // pbr3d.vert.glsl and its documented simplification.
    vTangent       = mat3(worldMatrix) * tangent.xyz;
    vBitangentSign = tangent.w * cnaDirectionHandedness(mat3(worldMatrix));
    vWorldPos      = (worldMatrix * vec4(position, 1.0)).xyz;
    vFogFactor     = clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0) * fogColor.a;
}
