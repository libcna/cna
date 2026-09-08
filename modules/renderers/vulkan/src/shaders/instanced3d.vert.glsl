#version 450

// Per-vertex attributes (binding = 0, VK_VERTEX_INPUT_RATE_VERTEX)
// Only position is consumed by this shader; the per-vertex stride may be 16/20/24/32.
layout(location = 0) in vec3 aPos;

// plans/plan_vulkan.md VULKAN-227: the four per-instance matrix columns are no longer
// declared here. compile_shaders.py injects them (locations 12..15) into every stock vertex
// shader compiled with CNA_INSTANCED, and CNA_INSTANCE_POSITION() is the identity without it.

layout(location = 0) out vec4 fragColor;

// 128-byte push constant identical to the Ext3D layout except [0..15] holds VP (not WVP).
// The per-instance world matrix is applied here; the combined transform is VP * World * pos.
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
    // plan_vulkan.md VULKAN-197 (F-36). Direct3D 9 saturates a vertex shader's colour output
    // registers before interpolation, and FNA writes this value to `vout.Diffuse : COLOR0`.
    // `BasicEffect.DiffuseColor` and `.Alpha` have no clamp in their setters, so a game can hand
    // this shader a value above 1; measured on the real XNA 4.0 runtime, 2.0 and 3.0 both render
    // exactly as 1.0 does (spikes/xna-diffuse-color-clamp-spike/). Clamping at the vertex stage is
    // what oD0 does -- clamping per fragment would interpolate the raw value first and give a
    // different gradient, the distinction plans/plan_fx.md FX-122/FX-123 settled.
    fragColor   = clamp(pc.diffuseColor, 0.0, 1.0);
}
