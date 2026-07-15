#version 450

// Stride 32: VertexPositionNormalTexture -- same layout lit_textured3d.vert.glsl (BasicEffect's
// lit path) uses, since EnvironmentMapEffect requires the same Position+Normal+TextureCoordinate
// vertex declaration.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormal;    // world-space
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragTint;

layout(set = 1, binding = 0) uniform PC {
    mat4 mvp;
    vec4 diffuseColor;
    vec4 emissiveAmount;  // xyz = emissive+ambient*diffuse (pre-summed by the XNA layer), w = envMapAmount
} pc;

// Mirrors VulkanGraphicsBackend's env_map3d.vert.glsl EnvMapParams field-for-field (minus fog,
// deliberately deferred here the same way lit_textured3d.glsl already defers it for this backend).
layout(set = 1, binding = 1) uniform EnvMapParams {
    mat4 world;
    vec4 eyePos_fresnelEnabled;    // xyz = eye world pos, w = fresnelEnabled (0/1)
    vec4 light0Dir_fresnelFactor;  // xyz = light0 dir, w = fresnelFactor
    vec4 light0Diffuse_pad;
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    vec4 envMapSpecular_pad;
} ep;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragUV = inUV;
    mat3 normalMatrix = transpose(inverse(mat3(ep.world)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragWorldPos = (ep.world * vec4(inPos, 1.0)).xyz;
    fragTint = pc.diffuseColor;
}
