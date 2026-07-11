#version 450

// DualTextureEffect vertex shader, stride-24 (VertexPositionColorTexture) variant.
// Task 889: unlike dual_texture3d.vert.glsl (stride 20, no color attribute), this variant
// reads the vertex color and gates its multiply by VertexColorEnabled, mirroring
// alpha_test_colored3d.vert.glsl's/colored_textured3d.vert.glsl's already-correct formula.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;   // normalized UNORM R8G8B8A8
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;
layout(location = 2) out float fragFogFactor;

layout(push_constant) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    vec3  ambientColor;
    float lightingEnabled;
    vec3  light0Dir;
    float textureEnabled;
    vec3  light0Diffuse;
    float vertexColorEnabled;
} pc;

layout(set = 0, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogStartEnd;      // x = fogStart, y = fogEnd, zw = unused
} fog;

void main() {
    vec4 pos = pc.mvp * vec4(inPos, 1.0);
    pos.y = -pos.y;
    gl_Position = pos;
    fragUV   = inUV;
    fragTint = (pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor : pc.diffuseColor;
    // Task 899: fog factor from raw object-space Z (matches the established Task 888 formula).
    fragFogFactor = (fog.fogColorEnabled.w > 0.5)
        ? clamp((fog.fogStartEnd.y - inPos.z) / max(fog.fogStartEnd.y - fog.fogStartEnd.x, 1e-6), 0.0, 1.0)
        : 1.0;
}
