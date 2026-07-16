#version 450

// Stride 16: VertexPositionColor -- float3 pos + ubyte4 color.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

// plan_sdlgpu.md: vertex-stage uniform buffers live in set 1 (SDL_gpu's SPIR-V graphics-pipeline
// convention). This 128-byte layout mirrors VulkanGraphicsBackend::FillExtPushConst() byte-for-
// byte (pushed via SDL_PushGPUVertexUniformData, not a raw Vulkan push constant) so every 3D
// shader in this family shares one fill function and one uniform shape. No fog (deliberately
// deferred, same as this codebase's WebGPU backend's own initial 3D vertical slice).
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

void main() {
    // No Vulkan-style Y-flip here -- confirmed empirically via sprite2d.vert.glsl that SDL_gpu's
    // Vulkan driver already presents a D3D/OpenGL-style Y-up clip space to shaders, so XNA's own
    // Y-up-convention projection matrix needs no additional correction on this backend.
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragColor = (pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor : pc.diffuseColor;
}
