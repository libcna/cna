#version 450

// plan_vulkan.md VULKAN-199 (finding F-37): the untextured sibling of lit_textured3d.vert.glsl.
//
// XNA's Primitives3D sample declares Position+Normal and nothing else -- 24 bytes, the same stride
// as VertexPositionColorTexture -- and BasicEffect must light it. This renderer's lit family
// existed for exactly one layout, VertexPositionNormalTexture, so such a draw reached no lit
// program at all and REMED-GFX-DECL-GUARD refused it rather than reading the normal's twelve bytes
// as a packed colour and a UV.
//
// Only the VERTEX stage needed splitting. The fragment stage already handles the untextured case --
// it gates its sample on pc.textureEnabled -- so this shader pairs with the SAME fragment shader
// and differs from its textured sibling in exactly two things: no aUV input, and fragUV written as
// zero. Vulkan cannot default an absent vertex attribute, which is why a separate module is needed
// where OpenGL would simply leave the attribute unbound.

// Stride 32: VertexPositionNormalTexture — float3 pos + float3 normal + float2 uv

// plans/plan_vulkan.md VULKAN-227/VULKAN-228: compiled twice -- once plain and once with
// CNA_INSTANCED, which declares the four per-instance matrix columns at locations 12..15 and
// turns CNA_INSTANCE_POSITION()/CNA_INSTANCE_WORLD() from the identity into the per-instance
// transform. Without the define each call expands to exactly the text that was here before,
// so this shape's ordinary module is byte-identical SPIR-V. See compile_shaders.py.
//
// VULKAN-219: the per-instance matrix applies INSIDE the effect's own world transform, which
// pc.mvp and lp.world each already carry, so an instance is lit and fogged where it stands.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

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
    vec4 pos = pc.mvp * CNA_INSTANCE_POSITION(vec4(inPos, 1.0));
    pos.y = -pos.y;
    gl_Position = pos;
    gl_PointSize = 1.0;
    fragUV     = vec2(0.0);   // VULKAN-199: no UV input on this layout
    // Task 898 fix: transform by World's inverse-transpose upper-left 3x3, not the full MVP
    // (mirrors EnvironmentMapEffect's own already-correct env_map3d.vert.glsl pattern) -- an
    // MVP-based transform bakes View/Projection into the normal, wrong under any non-identity
    // camera, not just non-uniform World scale.
    mat3 normalMatrix = transpose(inverse(mat3(CNA_INSTANCE_WORLD(lp.world))));
    fragNormal   = normalize(normalMatrix * inNormal);
    fragWorldPos = (lp.world * CNA_INSTANCE_POSITION(vec4(inPos, 1.0))).xyz;
    fragTint     = pc.diffuseColor;
    // Task 888: fog factor from raw object-space Z. REMED-GFX-005: corrected to FNA/EasyGL
    // Task-1111 form (z+FogEnd)/(FogEnd-FogStart); prior (FogEnd-z) was the mirror image and
    // wrong. 1.0 = no fog, 0.0 = full fog. Zero-length range -> fully fogged (FNA parity).
    fragFogFactor = 1.0 - clamp(dot(CNA_INSTANCE_POSITION(vec4(inPos, 1.0)), lp.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
