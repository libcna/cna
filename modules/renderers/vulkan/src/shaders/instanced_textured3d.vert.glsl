#version 450

// plans/plan_vulkan.md VULKAN-217: the instanced route's textured sibling.
//
// Instancing on this renderer is a separate program family rather than -- as on EasyGL -- an
// optional per-instance matrix added to every stock program, so until this file existed a
// `DrawInstancedPrimitives` draw with `BasicEffect.TextureEnabled` bound a default 1x1 white image
// and drew the material colour. Measured on both renderers by `instanced_textured_draw_test.cpp`.
//
// Per-vertex attributes (binding = 0, VK_VERTEX_INPUT_RATE_VERTEX). Locations are the index in
// StockInputs::kInstancedTextured, which is what BuildVulkanVertexInputLayoutEXT assigns.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

// plans/plan_vulkan.md VULKAN-227: the four per-instance matrix columns are no longer
// declared here. compile_shaders.py injects them (locations 12..15) into every stock vertex
// shader compiled with CNA_INSTANCED, and CNA_INSTANCE_POSITION() is the identity without it.

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;

// 128-byte push constant identical to the Ext3D layout except [0..15] holds VP (not WVP).
layout(push_constant) uniform PC {
    mat4  vp;
    vec4  diffuseColor;
    vec3  ambientColor;   float lightingEnabled;
    vec3  light0Dir;      float textureEnabled;
    vec3  light0Diffuse;  float vertexColorEnabled;
} pc;

void main() {
    gl_Position = pc.vp * CNA_INSTANCE_POSITION(vec4(aPos, 1.0));
    // REMED-GFX-011: renderer-wide Vulkan NDC Y-flip -- see pbr3d.vert.glsl.
    gl_Position.y = -gl_Position.y;
    gl_PointSize = 1.0;
    // plan_vulkan.md VULKAN-197 (F-36): Direct3D 9 saturates oD0 before interpolation, and
    // BasicEffect.DiffuseColor has no clamp in its setter. Same rule as instanced3d.vert.glsl.
    fragColor = clamp(pc.diffuseColor, 0.0, 1.0);
    fragUV    = aUV;
}
