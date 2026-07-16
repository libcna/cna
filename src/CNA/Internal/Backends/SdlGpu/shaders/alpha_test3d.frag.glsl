#version 450

// Shared by alpha_test3d.vert.glsl (strides 20/32) and alpha_test_colored3d.vert.glsl (stride 24).
layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragTint;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uTexture;

layout(set = 3, binding = 0) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    float alphaRef;
    float alphaTol;
    float alphaPassW;
    float alphaFailW;
    float vertexColorEnabled;
} pc;

// Same encoding as EasyGL/Vulkan/WebGPU:
//   if (alphaTol > 0) -> pass = |alpha - alphaRef| < alphaTol
//   else              -> pass = alpha < alphaRef
//   weight = pass ? alphaPassW : alphaFailW; weight < 0 -> discard.
// Default {0,0,1,1} = always pass (never discard).
void main() {
    outColor = texture(uTexture, fragUV) * fragTint;
    float alpha = outColor.a;
    bool passTest = (pc.alphaTol > 0.0) ? (abs(alpha - pc.alphaRef) < pc.alphaTol) : (alpha < pc.alphaRef);
    float w = passTest ? pc.alphaPassW : pc.alphaFailW;
    if (w < 0.0) discard;
}
