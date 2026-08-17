#version 450

// REMED-GFX-212: the position+colour sibling of instanced3d.vert.glsl, selected when the geometry
// stream's packed layout carries a COLOR0 element (strides 16 and 24 -- VertexPositionColor and
// VertexPositionColorTexture). BasicEffect's shader index has no instancing term, so an instanced
// draw owes the same VertexColorEnabled calculation the ordinary route already performs; the
// mixing expression below is byte-for-byte colored3d.vert.glsl's own.
//
// A separate shader rather than one with an optionally-bound attribute: Vulkan requires every
// user-defined vertex shader input to be backed by a VkVertexInputAttributeDescription, so a
// position-only geometry stride and a position+colour one need different modules -- exactly the
// split the ordinary route already makes between colored3d and textured3d.

// Per-vertex attributes (binding = 0, VK_VERTEX_INPUT_RATE_VERTEX).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;   // normalized UNORM R8G8B8A8 at the stride's colour offset

// Per-instance world matrix (binding = 1, VK_VERTEX_INPUT_RATE_INSTANCE, stride = 64)
// Column-major mat4: one column per vec4.
layout(location = 4) in vec4 aInstCol0;
layout(location = 5) in vec4 aInstCol1;
layout(location = 6) in vec4 aInstCol2;
layout(location = 7) in vec4 aInstCol3;

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
    mat4 world = mat4(aInstCol0, aInstCol1, aInstCol2, aInstCol3);
    gl_Position = pc.vp * world * vec4(aPos, 1.0);
    // REMED-GFX-011: renderer-wide Vulkan NDC Y-flip -- see pbr3d.vert.glsl.
    gl_Position.y = -gl_Position.y;
    gl_PointSize = 1.0;
    fragColor   = (pc.vertexColorEnabled > 0.5) ? aColor * pc.diffuseColor : pc.diffuseColor;
}
