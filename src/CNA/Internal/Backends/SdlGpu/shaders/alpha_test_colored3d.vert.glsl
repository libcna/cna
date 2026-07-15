#version 450

// AlphaTestEffect vertex shader, stride-24 (VertexPositionColorTexture) variant. Unlike
// alpha_test3d.vert.glsl (strides 20/32, no color attribute), this variant reads the vertex
// color and gates its multiply by VertexColorEnabled, mirroring colored_textured3d.vert.glsl's
// convention. Shares alpha_test3d.frag.glsl (identical fragment logic).
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;

layout(set = 1, binding = 0) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    float alphaRef;
    float alphaTol;
    float alphaPassW;
    float alphaFailW;
    float vertexColorEnabled;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragUV = inUV;
    fragTint = (pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor : pc.diffuseColor;
}
