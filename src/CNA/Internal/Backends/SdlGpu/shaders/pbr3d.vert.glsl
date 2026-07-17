#version 450

// Stride 48: VertexPositionNormalTangentTexture -- float3 pos + float3 normal + float4 tangent
// (xyz + bitangent handedness sign in w, glTF's own TANGENT accessor convention) + float2 uv.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out vec3  fragNormal;
layout(location = 2) out vec3  fragTangent;
layout(location = 3) out float fragBitangentSign;
layout(location = 4) out vec3  fragWorldPos;

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

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragUV = inUV;
    // GLSL has a built-in inverse(), matching lit_textured3d.vert.glsl's own normal-matrix
    // convention exactly (no CPU-precomputed normal matrix needed, unlike WebGPU's WGSL path).
    mat3 normalMatrix = transpose(inverse(mat3(lp.world)));
    fragNormal = normalize(normalMatrix * inNormal);
    // Tangent transforms as a plain direction under mat3(World) (not the inverse-transpose
    // normalMatrix used for the normal) -- correct for uniform-scale World transforms, mirrors
    // EasyGLGraphicsBackend::EnsurePbrProgram()'s own documented simplification exactly.
    fragTangent = mat3(lp.world) * inTangent.xyz;
    fragBitangentSign = inTangent.w;
    fragWorldPos = (lp.world * vec4(inPos, 1.0)).xyz;
}
