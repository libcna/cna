#version 450

// Task 899: dedicated fragment shader (previously Instanced3D reused colored3d.frag.glsl's
// compiled SPIR-V directly). colored3d.frag.glsl now declares a 2nd descriptor binding (a fog
// UBO, set=0 binding=1) as part of the shared colored3d/textured3d/colored_textured3d fog
// bundle -- Instanced3D still uses the original, unmodified single-binding descriptorSetLayout_/
// pipelineLayoutExt3D_ (shared with 2D SpriteBatch, must stay untouched), which is structurally
// incompatible with that 2nd binding. This file is a plain copy of colored3d.frag.glsl's
// pre-Task-899 content: no fog, no texture, just the flat instance diffuse color.

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
}
