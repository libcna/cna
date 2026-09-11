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

// Stride 24: VertexPositionColorTexture — float3 pos + ubyte4 color + float2 uv
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

// Task 899: fog forwarded via the shared colored3d/textured3d/colored_textured3d bundle's
// dynamic UBO (set=0, binding=1) -- the 128-byte push constant above has zero spare bytes.
layout(set = 0, binding = 1) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} fog;

void main() {
    vec4 pos = pc.mvp * CNA_INSTANCE_POSITION(vec4(inPos, 1.0));
    pos.y = -pos.y;
    gl_Position = pos;
    gl_PointSize = 1.0;
    fragUV = inUV;
    // Mix vertex color and diffuse based on vertexColorEnabled flag.
    // plan_vulkan.md VULKAN-197 (F-36). Direct3D 9 saturates a vertex shader's colour output
    // registers before interpolation, and FNA writes this value to `vout.Diffuse : COLOR0`.
    // `BasicEffect.DiffuseColor` and `.Alpha` have no clamp in their setters, so a game can hand
    // this shader a value above 1; measured on the real XNA 4.0 runtime, 2.0 and 3.0 both render
    // exactly as 1.0 does (spikes/xna-diffuse-color-clamp-spike/). Clamping at the vertex stage is
    // what oD0 does -- clamping per fragment would interpolate the raw value first and give a
    // different gradient, the distinction plans/plan_fx.md FX-122/FX-123 settled.
    fragTint = clamp((pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor
                                                  : pc.diffuseColor, 0.0, 1.0);
    // Task 899: fog factor from raw object-space Z. REMED-GFX-005: corrected to FNA/EasyGL Task-1111
    // form (z+FogEnd)/(FogEnd-FogStart); the prior Task 888/899 (FogEnd-z) formula was the
    // mirror image and wrong. Zero-length range -> fully fogged, matching FNA SetFogVector.
    fragFogFactor = 1.0 - clamp(dot(CNA_INSTANCE_POSITION(vec4(inPos, 1.0)), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
