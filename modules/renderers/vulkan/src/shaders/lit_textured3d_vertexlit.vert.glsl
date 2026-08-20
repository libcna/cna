#version 450

// Task 1103 (plans/plan_graphics.md Phase 80 / plans/plan_dx9.md Divergence 1): real XNA's BasicEffect
// defaults PreferPerPixelLighting=false, which selects a per-vertex-lit shader family
// (VSBasicVertexLighting*) -- lighting is computed ONCE per vertex and Gouraud-interpolated
// across the triangle, not re-evaluated per fragment. lit_textured3d.vert/frag.glsl is the
// PreferPerPixelLighting=true family; this is its per-vertex-lit sibling, selected by
// VulkanRenderer when the flag is false (XNA's own default). Identical Blinn-Phong math to
// lit_textured3d.frag.glsl (FNA's own Lighting.fxh ComputeLights(), same formula, same inputs) --
// only the STAGE it runs in changes. Only ever bound when lightingEnabled is true (see the C++
// dispatch), so unlike lit_textured3d.vert/frag.glsl this shader pair does not need its own
// unlit fallback branch.

// Stride 32: VertexPositionNormalTexture — float3 pos + float3 normal + float2 uv
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out float fragFogFactor;
layout(location = 2) out vec3  fragLitRGB;
layout(location = 3) out vec3  fragSpecularRGB;
layout(location = 4) out float fragAlpha;

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

// Same UBO layout as lit_textured3d.vert/frag.glsl (set=0, binding=1) — this shader just reads
// more of its already-declared fields (the lighting ones the pixel-lit fragment shader used).
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
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} lp;

void main() {
    vec4 pos = pc.mvp * vec4(inPos, 1.0);
    pos.y = -pos.y;
    gl_Position = pos;
    gl_PointSize = 1.0;
    fragUV = inUV;
    fragFogFactor = 1.0 - clamp(dot(vec4(inPos, 1.0), lp.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector

    mat3 normalMatrix = transpose(inverse(mat3(lp.world)));
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 worldPos = (lp.world * vec4(inPos, 1.0)).xyz;
    vec3 E = normalize(lp.eyePos_pad.xyz - worldPos);

    vec3 nL0 = normalize(pc.light0Dir);
    vec3 nL1 = normalize(lp.light1Dir_pad.xyz);
    vec3 nL2 = normalize(lp.light2Dir_pad.xyz);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    vec3 lightSum = pc.ambientColor + NdotL0 * pc.light0Diffuse
                    + NdotL1 * lp.light1Diffuse_pad.xyz + NdotL2 * lp.light2Diffuse_pad.xyz;
    // EmissiveColor is added after the light-sum*DiffuseColor multiply, not scaled by it
    // (matches FNA's Lighting.fxh: result.Diffuse = sum*DiffuseColor + EmissiveColor).
    fragLitRGB = lightSum * pc.diffuseColor.rgb + lp.emissiveColor_pad.xyz;
    fragAlpha  = pc.diffuseColor.a;

    vec3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, lp.specularColorPower.w);
    vec3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, lp.specularColorPower.w);
    vec3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, lp.specularColorPower.w);
    fragSpecularRGB = (spec0 * lp.light0Specular_pad.xyz + spec1 * lp.light1Specular_pad.xyz
                        + spec2 * lp.light2Specular_pad.xyz) * lp.specularColorPower.xyz;
}
