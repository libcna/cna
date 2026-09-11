#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

// plans/plan_sdlgpu.md SDLGPU-13: fragment-stage sampled textures live in set 2 and fragment-stage
// uniform buffers in set 3 per SDL_gpu's SPIR-V graphics-pipeline convention.
layout(set = 2, binding = 0) uniform sampler2D texSampler;

// SDLGPU-72: Direct3D 9/XNA samples absent texture channels as one. Vulkan exposes zero for the
// absent G/B channels of R/RG float render targets, so reproduce the EasyGL/XNA channel expansion
// per queued sprite. Fragment uniform buffers live in set 3 under SDL_gpu's SPIR-V convention.
layout(set = 3, binding = 0) uniform ChannelExpansion {
    vec4 mask;
    vec4 fill;
} channels;

// SDLGPU-121: SDL_gpu's Metal driver deliberately ignores sampler mip_lod_bias. Keep stock
// sampling backend-independent by carrying the first eight fragment-sampler biases in a
// renderer-owned block. SDLGPU-130 keeps the ordinary zero-bias lookup implicit: a zero-valued
// SPIR-V Bias operand measurably changed fixed-function linear-sampling rounding according to an
// otherwise irrelevant minification filter on Vulkan.
layout(set = 3, binding = 1) uniform SamplerLodBias {
    vec4 slots0To3;
    vec4 slots4To7;
} samplerLodBias;

void main() {
    float lodBias = samplerLodBias.slots0To3.x;
    vec4 sampled = lodBias == 0.0
        ? texture(texSampler, fragUV)
        : texture(texSampler, fragUV, lodBias);
    outColor = (sampled * channels.mask + channels.fill) * fragColor;
}
