#version 450

// DualTextureEffect vertex shader, stride 20 (VertexPositionTexture). Reuses the exact same
// 128-byte PC layout as textured3d.vert.glsl/colored3d.vert.glsl (FillExtUniforms) -- no
// dedicated uniform shape needed. Stride 24 uses dual_texture_colored3d.vert.glsl instead.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;

layout(set = 1, binding = 0) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    vec3  ambientColor;
    float lightingEnabled;
    vec3  light0Dir;
    float textureEnabled;
    vec3  light0Diffuse;
    float vertexColorEnabled;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragUV = inUV;
    fragTint = pc.diffuseColor;
}
