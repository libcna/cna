#version 450

// Stride 32: VertexPositionNormalTexture — float3 pos + float3 normal + float2 uv
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out vec3  fragNormal;  // world-space
layout(location = 2) out vec4  fragTint;
layout(location = 3) out vec3  fragWorldPos;
layout(location = 4) out float fragFogFactor;

// 128-byte push constant block (all 3D variants share this layout).
layout(push_constant) uniform PC {
    mat4  mvp;               // offset   0, 64 bytes
    vec4  diffuseColor;      // offset  64, 16 bytes
    vec3  ambientColor;      // offset  80
    float lightingEnabled;   // offset  92
    vec3  light0Dir;         // offset  96
    float textureEnabled;    // offset 108
    vec3  light0Diffuse;     // offset 112
    float vertexColorEnabled;// offset 124
} pc;                        // total: 128 bytes

// Task 897/886/898: DirectionalLight1/2 + EmissiveColor + specular data, forwarded via a small
// UBO since the 128-byte push constant above is already fully packed. `world` is here (not in
// the PC) purely so this vertex shader can compute a correct world-space position/normal.
layout(set = 0, binding = 1) uniform LitLightParams {
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
    // Task 888: fog, packed into the UBO's previously-unused trailing 32 bytes.
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} lp;

void main() {
    vec4 pos = pc.mvp * vec4(inPos, 1.0);
    pos.y = -pos.y;
    gl_Position = pos;
    gl_PointSize = 1.0;
    fragUV     = inUV;
    // Task 898 fix: transform by World's inverse-transpose upper-left 3x3, not the full MVP
    // (mirrors EnvironmentMapEffect's own already-correct env_map3d.vert.glsl pattern) -- an
    // MVP-based transform bakes View/Projection into the normal, wrong under any non-identity
    // camera, not just non-uniform World scale.
    mat3 normalMatrix = transpose(inverse(mat3(lp.world)));
    fragNormal   = normalize(normalMatrix * inNormal);
    fragWorldPos = (lp.world * vec4(inPos, 1.0)).xyz;
    fragTint     = pc.diffuseColor;
    // Task 888: fog factor from raw object-space Z. REMED-GFX-005: corrected to FNA/EasyGL
    // Task-1111 form (z+FogEnd)/(FogEnd-FogStart); prior (FogEnd-z) was the mirror image and
    // wrong. 1.0 = no fog, 0.0 = full fog. Zero-length range -> fully fogged (FNA parity).
    fragFogFactor = 1.0 - clamp(dot(vec4(inPos, 1.0), lp.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
