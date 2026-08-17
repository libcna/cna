#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out float fragFogFactor;

// XNA row-major MVP: apply as (pos * mvp) which equals mvp^T * pos in column-major
//
// Push-constant layout intentionally mirrors FillExtPushConst()'s 32-float/128-byte
// layout byte-for-byte (Task 364), even though this pipeline only reads a subset of it:
// callers (DrawPrimitivesEx for stride==16 BasicEffect draws) already fill the whole
// buffer via FillExtPushConst(), so no separate fill path is needed for this shader.
layout(push_constant) uniform PC {
    mat4  mvp;                  // [0..15]  bytes 0..63
    vec4  diffuseColor;         // [16..19] bytes 64..79
    vec4  _unusedAmbientLight;  // [20..23] bytes 80..95  (ambientColor + lightingEnabled)
    vec4  _unusedLight0DirTex;  // [24..27] bytes 96..111 (light0Dir + textureEnabled)
    vec3  _unusedLight0Diffuse; // [28..30] bytes 112..123
    float vertexColorEnabled;   // [31]     bytes 124..127
} pc;

// Task 899: fog forwarded via the shared colored3d/textured3d/colored_textured3d bundle's
// dynamic UBO (set=0, binding=1) -- the 128-byte push constant above has zero spare bytes.
layout(set = 0, binding = 1) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} fog;

void main() {
    vec4 pos = pc.mvp * vec4(inPos, 1.0);
    pos.y = -pos.y;                      // Vulkan NDC Y is inverted vs OpenGL
    // Z already in [0,+w] from XNA DirectX-convention projection — no remap needed.
    gl_Position = pos;
    gl_PointSize = 1.0;
    // Mix vertex color and diffuse based on vertexColorEnabled flag (matches
    // colored_textured3d.vert.glsl's convention for the same flag).
    fragColor = (pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor : pc.diffuseColor;
    // Task 899: fog factor from raw object-space Z. REMED-GFX-005: corrected to FNA/EasyGL Task-1111
    // form (z+FogEnd)/(FogEnd-FogStart); the prior Task 888/899 (FogEnd-z) formula was the
    // mirror image and wrong. Zero-length range -> fully fogged, matching FNA SetFogVector.
    fragFogFactor = 1.0 - clamp(dot(vec4(inPos, 1.0), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
