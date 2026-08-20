#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

// plans/plan_sdlgpu.md SDLGPU-13/16: SDL_gpu's SPIR-V graphics-pipeline convention reserves set 1 for
// vertex-stage uniform buffers (set 0 is vertex-stage textures/storage, unused here) --
// SDL_PushGPUVertexUniformData() delivers data into this binding, not a raw Vulkan push constant.
layout(set = 1, binding = 0) uniform UBO {
    vec2 viewportSize;
} ubo;

void main() {
    // pixel-space -> NDC: (0,0) top-left, Y-down to match XNA. SDL_gpu's Vulkan driver flips
    // clip-space Y internally so shaders present a consistent cross-renderer NDC convention
    // (confirmed empirically: without this negation, a texture's top row renders at the bottom
    // of the sprite) -- negate Y here to cancel that out and keep pixel-space Y-down semantics.
    vec2 ndc = (inPos / ubo.viewportSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    fragUV = inUV;
    fragColor = inColor;
}
