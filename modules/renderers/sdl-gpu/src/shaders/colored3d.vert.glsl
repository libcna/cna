#version 450

// Stride 16: VertexPositionColor -- float3 pos + ubyte4 color.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragFog;    // REMED-GFX-009 xyz=FogColor, w=keep-factor

// plans/plan_sdlgpu.md: vertex-stage uniform buffers live in set 1 (SDL_gpu's SPIR-V graphics-pipeline
// convention). This 128-byte layout mirrors VulkanRenderer::FillExtPushConst() byte-for-
// byte (pushed via SDL_PushGPUVertexUniformData, not a raw Vulkan push constant) so every 3D
// shader in this family shares one fill function and one uniform shape. Fog (REMED-GFX-009) is
// supplied separately via the FogParams block below, since this PC block is fully packed.
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

// REMED-GFX-009: fog forwarded to the fragment stage as a varying (the shared PC block is fully
// packed, no spare bytes). Keep-factor computed from raw object-space Z, matching
// VulkanRenderer's FogParams shape byte-for-byte.
layout(set = 1, binding = 1) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;        // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} fog;

void main() {
    // No Vulkan-style Y-flip here -- confirmed empirically via sprite2d.vert.glsl that SDL_gpu's
    // Vulkan driver already presents a D3D/OpenGL-style Y-up clip space to shaders, so XNA's own
    // Y-up-convention projection matrix needs no additional correction on this renderer.
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragColor = (pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor : pc.diffuseColor;
    // REMED-GFX-009: keep-factor from raw object-space Z (GFX-005 corrected form
    // (z+FogEnd)/(FogEnd-FogStart)); FogStart==FogEnd -> fully fogged (FNA SetFogVector). keep=1 ->
    // no fog, keep=0 -> full FogColor. Skinned shaders use the PRE-skin inPos.z (matches Vulkan).
    float fogKeep = 1.0 - clamp(dot(vec4(inPos, 1.0), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
    fragFog = vec4(fog.fogColorEnabled.xyz, fogKeep);
}
