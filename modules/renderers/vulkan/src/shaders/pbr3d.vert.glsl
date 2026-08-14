#version 450

// PbrEffect vertex shader — stride 48 (VertexPositionNormalTangentTexture): float3 position +
// float3 normal + float4 tangent (xyz + bitangent handedness in w, glTF convention) + float2 uv.
// Mirrors EasyGLRenderer::EnsurePbrProgram()'s vertex stage.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;

layout(location = 0) out vec3  vNormal;
layout(location = 1) out vec3  vTangent;
layout(location = 2) out float vBitangentSign;
layout(location = 3) out vec2  vUV;
layout(location = 4) out float vFogFactor;
layout(location = 5) out vec3  vWorldPos;

// 128-byte push constant block (shared with every other 3D variant — see FillExtPushConst).
// diffuseColor -> PBR base color factor; ambientColor -> PBR ambient; light0Dir/light0Diffuse ->
// PBR's own DirectionalLight0. lightingEnabled/textureEnabled/vertexColorEnabled are unused here
// (PbrEffect::FillGpuDrawParams always sets lightingEnabled=textureEnabled=true, and PbrEffect has
// no vertex-color concept).
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

// DirectionalLight1/2 + World + EyePosition + PBR factors + fog, forwarded via a small dynamic
// UBO since the 128-byte PC above is already fully packed (mirrors lit_textured3d.vert.glsl's
// LitLightParams/skinned3d.vert.glsl's FogParams own precedent).
layout(set = 0, binding = 5) uniform PbrParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    mat4 world;
    vec4 eyePos_metallic;       // xyz = EyePosition, w = MetallicFactor
    vec4 emissive_roughness;    // xyz = EmissiveFactor, w = RoughnessFactor
    vec4 fogColorEnabled;       // xyz = FogColor, w = WeightsPerVertex (REMED-GFX-010; skinned only)
    vec4 fogVector;             // REMED-GFX-010: FNA fog vector (xyz + w)
    vec4 alphaTest;             // reference, tolerance, pass weight, fail weight
    vec4 pbrMapScales;          // x = normal scale, y = occlusion strength
} pbr;

void main() {
    gl_Position = pc.mvp * vec4(aPos, 1.0);
    // REMED-GFX-011: Vulkan NDC Y is inverted vs OpenGL and the C++ side supplies no correction,
    // so every 3D vertex shader in this renderer flips here. This is a renderer-wide convention,
    // not a per-family choice -- omitting it renders PbrEffect vertically mirrored relative to
    // every other effect in the same frame.
    gl_Position.y = -gl_Position.y;
    // World's inverse-transpose upper-left 3x3 (mirrors lit_textured3d.vert.glsl's Task 898 fix
    // and EnvironmentMapEffect's own already-correct env_map3d.vert.glsl pattern).
    mat3 normalMatrix = transpose(inverse(mat3(pbr.world)));
    vNormal = normalize(normalMatrix * aNormal);
    // Tangent transforms as a plain direction under mat3(world) (not the normal's inverse-
    // transpose) — correct for uniform-scale World transforms, matching
    // EasyGLRenderer::EnsurePbrProgram()'s own documented simplification.
    vTangent = mat3(pbr.world) * aTangent.xyz;
    vBitangentSign = aTangent.w;
    vUV = aUV;
    vWorldPos = (pbr.world * vec4(aPos, 1.0)).xyz;
    vFogFactor = 1.0 - clamp(dot(vec4(aPos, 1.0), pbr.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
