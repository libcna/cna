#version 450

// plans/plan_vulkan.md VULKAN-217. Deliberately ONE descriptor binding, like instanced3d.frag.glsl:
// the instanced route uses pipelineLayoutExt3D_/descriptorSetLayout_, the single combined-image-
// sampler layout it shares with 2D SpriteBatch, and a second binding (the fog UBO the
// colored3d/textured3d bundle carries) is structurally incompatible with it. So this file samples
// and multiplies, and does no fog -- which is what instancing on this renderer supports today.

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

void main() {
    outColor = texture(uTexture, fragUV) * fragColor;
}
