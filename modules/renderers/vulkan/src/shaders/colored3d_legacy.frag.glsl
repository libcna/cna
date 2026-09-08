#version 450

// The fragment stage of the legacy no-GpuDrawParams DrawColoredPrimitives()/
// DrawIndexedColoredPrimitives() path, paired with colored3d_legacy.vert.glsl.
//
// Task 899 added a fog UBO (set=0 binding=1) to colored3d.frag.glsl as part of the shared
// colored3d/textured3d/colored_textured3d bundle, which is structurally incompatible with
// pipelineLayout3D_'s zero-descriptor-set layout -- so this file is that shader's pre-Task-899
// content: no fog, no texture, just the flat diffuse colour. It has no fog data to forward
// because that path has none to give it.
//
// plans/plan_vulkan.md VULKAN-234: it was called `instanced3d.frag.glsl` until instancing stopped
// having a program family of its own. GetOrCreatePipeline3D was always its other caller, and is
// now its only one, so the file is named for what it actually serves.

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
}
