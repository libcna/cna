// SPDX-License-Identifier: MS-PL
//
// plans/plan_sokol.md SOKOL-36: the Sokol renderer's instanced 3D program -- GraphicsDevice's
// DrawInstancedPrimitives, ported from VulkanRenderer's own instanced3d.{vert,frag}.glsl
// (the same "VP-only in the shared uniform, World reconstructed from 4 per-instance vec4 columns
// in the vertex stage" shape bgfx/WebGPU's own instanced3d shaders already use). A deliberate,
// established scope reduction, not a Sokol-specific gap: flat DiffuseColor only, no vertex
// colour, texturing or lighting -- Vulkan's own instanced3d.frag.glsl is a plain pass-through of
// exactly this, and its push-constant layout carries (but never reads) ambient/light/
// textureEnabled/vertexColorEnabled fields for structural reasons that don't apply here, so this
// shader's uniform block only declares what actually gets used.
//
// Per-vertex position comes from vertex-buffer slot 0 (step_func=SG_VERTEXSTEP_PER_VERTEX, the
// same slot every other 3D shader here uses); the per-instance world matrix comes from slot 1
// (step_func=SG_VERTEXSTEP_PER_INSTANCE) as 4 consecutive vec4 attributes -- GLSL/HLSL have no
// single per-vertex-attribute mat4 input, matching Vulkan/bgfx/WebGPU's identical decomposition.
// Both the per-vertex/per-instance step rate and the buffer-slot assignment are set in
// Get3DPipeline() (sg_vertex_attr_state.buffer_index, sg_vertex_buffer_layout_state.step_func) --
// sokol-shdc's own reflection has no way to express either from this GLSL source.

@module cna

@vs instanced3d_vs
layout(binding = 0) uniform instanced3d_vs_params {
    // view * projection; World is reconstructed per-instance below rather than baked in here.
    mat4 vp;
    vec4 diffuseColor;
};

in vec3 position;
in vec4 instCol0;
in vec4 instCol1;
in vec4 instCol2;
in vec4 instCol3;

out vec4 fragColor;

void main() {
    mat4 world = mat4(instCol0, instCol1, instCol2, instCol3);
    gl_Position = vp * world * vec4(position, 1.0);
    fragColor = diffuseColor;
}
@end

@fs instanced3d_fs
in vec4 fragColor;
out vec4 frag_color;

void main() {
    frag_color = fragColor;
}
@end

@program instanced3d instanced3d_vs instanced3d_fs
