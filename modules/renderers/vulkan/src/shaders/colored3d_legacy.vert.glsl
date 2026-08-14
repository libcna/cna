#version 450

// Task 899: dedicated vertex shader for the legacy, no-GpuDrawParams DrawColoredPrimitives()/
// DrawIndexedColoredPrimitives() path (GetOrCreatePipeline3D / pipelineLayout3D_, which has
// ZERO descriptor sets and a VERTEX_BIT-only 128-byte push constant). colored3d.vert.glsl now
// declares a fog UBO at binding=1 as part of the shared colored3d/textured3d/colored_textured3d
// fog bundle, which pipelineLayout3D_ does not provide (that legacy path carries no
// GpuDrawParams/fog data at all) -- so this pipeline needs its own copy of the pre-Task-899
// colored3d.vert.glsl content, with no fog.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

// Push-constant layout intentionally mirrors FillExtPushConst()'s 32-float/128-byte layout
// byte-for-byte (Task 364), even though this pipeline only reads a subset of it.
layout(push_constant) uniform PC {
    mat4  mvp;                  // [0..15]  bytes 0..63
    vec4  diffuseColor;         // [16..19] bytes 64..79
    vec4  _unusedAmbientLight;  // [20..23] bytes 80..95  (ambientColor + lightingEnabled)
    vec4  _unusedLight0DirTex;  // [24..27] bytes 96..111 (light0Dir + textureEnabled)
    vec3  _unusedLight0Diffuse; // [28..30] bytes 112..123
    float vertexColorEnabled;   // [31]     bytes 124..127
} pc;

void main() {
    vec4 pos = pc.mvp * vec4(inPos, 1.0);
    pos.y = -pos.y;                      // Vulkan NDC Y is inverted vs OpenGL
    gl_Position = pos;
    gl_PointSize = 1.0;
    fragColor = (pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor : pc.diffuseColor;
}
