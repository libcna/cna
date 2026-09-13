#version 450

// Stride 32: VertexPositionNormalTexture -- float3 pos + float3 normal + float2 uv.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out vec3  fragNormal;
layout(location = 2) out vec4  fragTint;
layout(location = 3) out vec3  fragWorldPos;
layout(location = 4) out vec4 fragFog;    // REMED-GFX-009 xyz=FogColor, w=keep-factor
layout(location = 5) out vec3 fragVertexLit;
layout(location = 6) out vec3 fragVertexSpecular;

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

// plans/plan_sdlgpu.md: DirectionalLight1/DirectionalLight2 + EmissiveColor + specular + the World
// matrix (needed here to compute a world-space normal/position), forwarded via a second
// vertex-stage UBO (set 1, binding 1) since the primary 128-byte UBO above is already fully
// packed -- mirrors VulkanRenderer/WebGPURenderer's own lit_textured3d second-UBO
// precedent (field names kept identical to those renderers' for easier cross-reference).
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

// REMED-GFX-009: fog forwarded to the fragment stage as a varying (the shared PC block is fully
// packed, no spare bytes). Keep-factor computed from raw object-space Z, matching
// VulkanRenderer's FogParams shape byte-for-byte.
layout(set = 1, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;        // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} fog;

vec3 safeNormalize(vec3 v) {
    float len2 = dot(v, v);
    return len2 > 0.0 ? v * inversesqrt(len2) : vec3(0.0);
}

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragUV = inUV;
    // GLSL has a built-in inverse(), unlike WGSL -- no need for WebGPU's CPU-precomputed normal
    // matrix workaround; this mirrors VulkanRenderer's own lit_textured3d.vert.glsl exactly.
    mat3 normalMatrix = transpose(inverse(mat3(lp.world)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragWorldPos = (lp.world * vec4(inPos, 1.0)).xyz;
    fragTint = (pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor : pc.diffuseColor;
    // XNA's default PreferPerPixelLighting=false evaluates the same Blinn-Phong expression once
    // per vertex and clamps its COLOR outputs before interpolation. The fragment stage selects
    // these values when emissiveColor_pad.w is zero; the existing per-pixel path remains selected
    // when it is one.
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 E = safeNormalize(lp.eyePos_pad.xyz - fragWorldPos);
    vec3 nL0 = safeNormalize(pc.light0Dir);
    vec3 nL1 = safeNormalize(lp.light1Dir_pad.xyz);
    vec3 nL2 = safeNormalize(lp.light2Dir_pad.xyz);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    vec3 lightSum = pc.ambientColor + NdotL0 * pc.light0Diffuse
                    + NdotL1 * lp.light1Diffuse_pad.xyz + NdotL2 * lp.light2Diffuse_pad.xyz;
    vec3 vertexColor = (pc.vertexColorEnabled > 0.5) ? inColor.rgb : vec3(1.0);
    fragVertexLit = clamp((lightSum * pc.diffuseColor.rgb + lp.emissiveColor_pad.xyz)
                          * vertexColor, 0.0, 1.0);
    vec3 h0 = safeNormalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, lp.specularColorPower.w);
    vec3 h1 = safeNormalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, lp.specularColorPower.w);
    vec3 h2 = safeNormalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, lp.specularColorPower.w);
    fragVertexSpecular = clamp(
        (spec0 * lp.light0Specular_pad.xyz + spec1 * lp.light1Specular_pad.xyz
         + spec2 * lp.light2Specular_pad.xyz) * lp.specularColorPower.xyz,
        0.0, 1.0);
    // REMED-GFX-009: keep-factor from raw object-space Z (GFX-005 corrected form
    // (z+FogEnd)/(FogEnd-FogStart)); FogStart==FogEnd -> fully fogged (FNA SetFogVector). keep=1 ->
    // no fog, keep=0 -> full FogColor. Skinned shaders use the PRE-skin inPos.z (matches Vulkan).
    float fogKeep = 1.0 - clamp(dot(vec4(inPos, 1.0), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
    fragFog = vec4(fog.fogColorEnabled.xyz, fogKeep);
}
