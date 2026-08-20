#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

// plans/plan_sdlgpu.md SDLGPU-13: fragment-stage sampled textures live in set 2 (set 3 is reserved for
// fragment-stage uniform buffers, unused here) per SDL_gpu's SPIR-V graphics-pipeline convention.
layout(set = 2, binding = 0) uniform sampler2D texSampler;

void main() {
    outColor = texture(texSampler, fragUV) * fragColor;
}
