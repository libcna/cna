// SPDX-License-Identifier: MS-PL
// PbrEffect vertex shader, OpenGL flavour. See pbr3d.vert.glsl for the Vulkan flavour and the
// reasoning behind this shader's own dedicated (not shared) uniform block.

#version 450 core

layout(std140, binding = 1) uniform PbrParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    vec4 ambientColorPad;      // xyz = AmbientLightColor, w = decode base colour
    vec4 emissiveMetallic;
    vec4 roughnessWeightsPad;  // x=roughness, y=skin weights, z=normal scale, w=occlusion strength
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

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoord;
#ifdef CNA_PBR_DUAL_UV
layout(location = 7) in vec2 texCoord1;
#endif
layout(location = 3) in vec3 normal;
layout(location = 6) in vec4 tangent;
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

float cnaDirectionHandedness(mat3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

void main()
{
    gl_Position = mvpMatrix * vec4(position, 1.0);
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

    mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));
    vNormal        = normalize(normalMatrix * normal);
    vTangent       = mat3(worldMatrix) * tangent.xyz;
    vBitangentSign = tangent.w * cnaDirectionHandedness(mat3(worldMatrix));
    vWorldPos      = (worldMatrix * vec4(position, 1.0)).xyz;
    vFogFactor     = clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0) * fogColor.a;
}
