#version 450

// Stride 24: VertexPositionColorTexture -- float3 pos + ubyte4 color + float2 uv.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;

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
    // Mix vertex color and diffuse based on vertexColorEnabled flag (matches colored3d.vert.glsl's
    // convention for the same flag).
    fragTint = (pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor : pc.diffuseColor;
}
