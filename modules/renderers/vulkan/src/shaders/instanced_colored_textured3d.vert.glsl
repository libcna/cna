#version 450

// plans/plan_vulkan.md VULKAN-220: the instanced route's colour-AND-texture shape.
//
// VULKAN-217 added the textured instanced program and made `textured` win over the colour shape,
// because there was no program that could do both -- so a declaration carrying a Colour and a
// TextureCoordinate had its colour dropped. Measured: a grey(128) vertex colour over a blue
// texture reads (0,0,128) non-instanced and read (0,0,255) instanced, where EasyGL reads
// (0,0,128) both ways.
//
// Per-vertex attributes (binding = 0). Locations are the index in
// StockInputs::kInstancedColoredTextured, which is what BuildVulkanVertexInputLayoutEXT assigns,
// and the order matches colored_textured3d.vert.glsl's own.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;   // normalized UNORM R8G8B8A8
layout(location = 2) in vec2 aUV;

// Per-instance world matrix (binding = 1, VK_VERTEX_INPUT_RATE_INSTANCE, stride = 64)
layout(location = 4) in vec4 aInstCol0;
layout(location = 5) in vec4 aInstCol1;
layout(location = 6) in vec4 aInstCol2;
layout(location = 7) in vec4 aInstCol3;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;

layout(push_constant) uniform PC {
    mat4  wvp;
    vec4  diffuseColor;
    vec3  ambientColor;   float lightingEnabled;
    vec3  light0Dir;      float textureEnabled;
    vec3  light0Diffuse;  float vertexColorEnabled;
} pc;

void main() {
    mat4 instWorld = mat4(aInstCol0, aInstCol1, aInstCol2, aInstCol3);
    // VULKAN-219: the instance matrix multiplies INSIDE the effect's own world transform, which
    // pc.wvp already carries.
    gl_Position = pc.wvp * instWorld * vec4(aPos, 1.0);
    // REMED-GFX-011: renderer-wide Vulkan NDC Y-flip -- see pbr3d.vert.glsl.
    gl_Position.y = -gl_Position.y;
    gl_PointSize = 1.0;
    // plan_vulkan.md VULKAN-197 (F-36) and the same expression colored_textured3d.vert.glsl uses:
    // Direct3D 9 saturates oD0 before interpolation, and the vertex colour multiplies the material
    // colour at the vertex stage, inside that clamp.
    fragColor = clamp((pc.vertexColorEnabled > 0.5) ? aColor * pc.diffuseColor
                                                    : pc.diffuseColor, 0.0, 1.0);
    fragUV    = aUV;
}
