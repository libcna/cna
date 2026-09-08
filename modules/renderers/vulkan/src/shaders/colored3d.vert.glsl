#version 450
//
// plans/plan_vulkan.md VULKAN-227/VULKAN-233: compiled twice -- once plain and once with
// CNA_INSTANCED, which declares the four per-instance matrix columns at locations 12..15 and
// turns CNA_INSTANCE_POSITION() from the identity into the per-instance transform. Without
// the define each call expands to exactly the text that was here before, so the ordinary
// module's SPIR-V is byte-identical. See compile_shaders.py.
//
// VULKAN-233: an instanced BasicEffect draw takes THIS program rather than a fog-less
// instanced family of its own, which is how instancing gained fog on this renderer. The fog
// term dots the instance-transformed object-space position, so each instance is fogged where
// it stands.

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
    vec4 pos = pc.mvp * CNA_INSTANCE_POSITION(vec4(inPos, 1.0));
    pos.y = -pos.y;                      // Vulkan NDC Y is inverted vs OpenGL
    // Z already in [0,+w] from XNA DirectX-convention projection — no remap needed.
    gl_Position = pos;
    gl_PointSize = 1.0;
    // Mix vertex color and diffuse based on vertexColorEnabled flag (matches
    // colored_textured3d.vert.glsl's convention for the same flag).
    // plan_vulkan.md VULKAN-197 (F-36). Direct3D 9 saturates a vertex shader's colour output
    // registers before interpolation, and FNA writes this value to `vout.Diffuse : COLOR0`.
    // `BasicEffect.DiffuseColor` and `.Alpha` have no clamp in their setters, so a game can hand
    // this shader a value above 1; measured on the real XNA 4.0 runtime, 2.0 and 3.0 both render
    // exactly as 1.0 does (spikes/xna-diffuse-color-clamp-spike/). Clamping at the vertex stage is
    // what oD0 does -- clamping per fragment would interpolate the raw value first and give a
    // different gradient, the distinction plans/plan_fx.md FX-122/FX-123 settled.
    fragColor = clamp((pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor
                                                    : pc.diffuseColor, 0.0, 1.0);
    // Task 899: fog factor from raw object-space Z. REMED-GFX-005: corrected to FNA/EasyGL Task-1111
    // form (z+FogEnd)/(FogEnd-FogStart); the prior Task 888/899 (FogEnd-z) formula was the
    // mirror image and wrong. Zero-length range -> fully fogged, matching FNA SetFogVector.
    fragFogFactor = 1.0 - clamp(dot(CNA_INSTANCE_POSITION(vec4(inPos, 1.0)), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
