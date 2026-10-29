#version 450

// Stride 48: VertexPositionNormalTangentTexture -- float3 pos + float3 normal + float4 tangent
// (xyz + bitangent handedness sign in w, glTF's own TANGENT accessor convention) + float2 uv.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;
// GLTF-465: glTF 2.0 3.9.2 makes COLOR_0 an additional linear multiplier on base
// colour. Declared only in the variants whose pipeline supplies it (strides 60 and 80); the others
// pass opaque white, the multiplier's identity, so the shared fragment stage has one interface.
#ifdef CNA_PBR_VERTEX_COLOR
layout(location = 4) in vec4 inColor;
// GLTF-182: the stride-60 record carries TEXCOORD_1 ahead of its colour.
layout(location = 7) in vec2 inUV1;
#endif

layout(location = 0) out vec2  fragUV;
layout(location = 1) out vec3  fragNormal;
layout(location = 2) out vec3  fragTangent;
layout(location = 3) out float fragBitangentSign;
layout(location = 4) out vec3  fragWorldPos;
layout(location = 5) out vec4 fragFog;    // xyz=FogColor, w=keep-factor
layout(location = 6) out vec4  fragColor0;
layout(location = 7) out vec2  fragUV1;

// Reuses lit_textured3d.vert.glsl's exact PC layout (FillExtUniforms) -- PbrEffect's
// DiffuseColor/AmbientColor/DirectionalLight0 all land in the same slots that already exist for
// BasicEffect, so no new primary uniform block is needed.
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

// Reuses lit_textured3d.vert.glsl's exact LitLightParams layout (FillLitLightUniforms) --
// DirectionalLight1/2, EmissiveColor, World and EyePosition all land in the same slots.
layout(set = 1, binding = 1) uniform LitLightParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    vec4 emissiveColor_pad;
    mat4 world;
    vec4 eyePos_pad;
    vec4 light0Specular_pad;
    vec4 light1Specular_pad;
    vec4 light2Specular_pad;
    vec4 specularColorPower;
} lp;

// Fog forwarded to the fragment stage as a varying (the shared PC block is fully
// packed, no spare bytes). Keep-factor computed from raw object-space Z, matching
// the retired VulkanRenderer's FogParams shape byte-for-byte.
layout(set = 1, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;        // FNA fog vector (dot with object/skin pos)
} fog;

float cnaDirectionHandedness(mat3 m) {
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

// transpose(inverse(m)) by its cofactors over the determinant, and zero for a singular World
// (a node scaled flat), as EasyGL computes it: inverse() of a singular matrix is undefined, and the
// fragment stage substitutes a fixed normal for a zero one.
mat3 cnaNormalMatrix(mat3 m) {
    vec3 c0 = cross(m[1], m[2]);
    vec3 c1 = cross(m[2], m[0]);
    vec3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    return det != 0.0 ? mat3(c0, c1, c2) * (1.0 / det) : mat3(0.0);
}

// normalize() that leaves a zero vector zero instead of NaN; dividing by the largest component
// first keeps a World scaled by 1e5 from underflowing to one.
vec3 cnaSafeDirection(vec3 v) {
    float scale = max(max(abs(v.x), abs(v.y)), abs(v.z));
    return scale > 0.0 ? normalize(v / scale) : vec3(0.0);
}

void main() {
    gl_Position = pc.mvp * CNA_INSTANCE_POSITION(vec4(inPos, 1.0));
    gl_PointSize = 1.0;  // Apple M4 AM4-181: PointListEXT draws one pixel per point
    fragUV = inUV;
    #ifdef CNA_PBR_VERTEX_COLOR
    fragUV1 = inUV1;
    #else
    fragUV1 = inUV;
    #endif
    #ifdef CNA_PBR_VERTEX_COLOR
    fragColor0 = inColor;
    #else
    fragColor0 = vec4(1.0);
    #endif
    // GLSL has a built-in inverse(), matching lit_textured3d.vert.glsl's own normal-matrix
    // convention exactly (no CPU-precomputed normal matrix needed, unlike WebGPU's WGSL path).
    mat3 normalMatrix = cnaNormalMatrix(mat3(CNA_INSTANCE_WORLD(lp.world)));
    fragNormal = cnaSafeDirection(normalMatrix * inNormal);
    // Tangent transforms as a plain direction under mat3(World) (not the inverse-transpose
    // normalMatrix used for the normal) -- correct for uniform-scale World transforms, mirrors
    // EasyGLRenderer::EnsurePbrProgram()'s own documented simplification exactly.
    fragTangent = mat3(CNA_INSTANCE_WORLD(lp.world)) * inTangent.xyz;
    fragBitangentSign = inTangent.w * cnaDirectionHandedness(mat3(CNA_INSTANCE_WORLD(lp.world)));
    fragWorldPos = (lp.world * CNA_INSTANCE_POSITION(vec4(inPos, 1.0))).xyz;
    // Keep-factor from raw object-space Z (GFX-005 corrected form
    // (z+FogEnd)/(FogEnd-FogStart)); FogStart==FogEnd -> fully fogged (FNA SetFogVector). keep=1 ->
    // no fog, keep=0 -> full FogColor. Skinned shaders use the PRE-skin inPos.z (as Vulkan did).
    float fogKeep = 1.0 - clamp(dot(CNA_INSTANCE_POSITION(vec4(inPos, 1.0)), fog.fogVector), 0.0, 1.0); // FNA view-space fog vector
    fragFog = vec4(fog.fogColorEnabled.xyz, fogKeep);
}
