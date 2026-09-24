#version 450

// Texture-array unit 0: set 1, binding 16 -- the descriptor contract Vulkan and WebGPU share
// (docs/webgpu-renderer.md, VulkanEffectRenderer::BindTexture2DArrayEXT).
layout(set = 1, binding = 16) uniform sampler2DArray uArray;

layout(location = 0) in float vLayer;
layout(location = 0) out vec4 FragColor;

void main()
{
    // Every layer is one solid colour, so the sample point inside it does not matter -- and nor
    // does any renderer's clip-space Y convention.
    FragColor = texture(uArray, vec3(0.5, 0.5, vLayer));
}
