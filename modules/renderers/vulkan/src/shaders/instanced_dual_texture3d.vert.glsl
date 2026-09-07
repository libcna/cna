#version 450

// plans/plan_vulkan.md VULKAN-225: the dual-texture family, made instanceable.
//
// A copy of dual_texture3d.vert.glsl with the four per-instance matrix columns added. Same push
// constant, same fog UBO, same outputs -- so the same fragment stage, pipeline layout and
// descriptor set, exactly as VULKAN-222 and VULKAN-224 did for the alpha-test and lit families.
//
// Measured before it existed: tex0 = blue and tex2 = quarter-grey, which FNA doubles to (0,0,128),
// read (0,0,128) non-instanced and (0,0,255) instanced here -- the second texture dropped -- where
// EasyGL read (0,0,128) both ways.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
// plans/plan_vulkan.md VULKAN-150: DualTextureEffect's second sampler is addressed by
// TEXCOORD1, not by TEXCOORD0. Before this input existed the fragment shader sampled both
// textures with fragUV, so a declaration carrying an independent second UV set could not be
// honoured -- and the stride guard refusing such a record was the only thing hiding it.
// A record that declares no TextureCoordinate1 has this input pointed at TextureCoordinate0's
// own element, which reproduces the previous behaviour exactly.
layout(location = 2) in vec2 inUV1;

// Per-instance world matrix (binding = 1, VK_VERTEX_INPUT_RATE_INSTANCE, stride = 64). The
// dual-texture family's per-vertex inputs stop at location 3 (the coloured variant's aColor), so
// 4..7 are free here as in every other instanced program.
layout(location = 4) in vec4 aInstCol0;
layout(location = 5) in vec4 aInstCol1;
layout(location = 6) in vec4 aInstCol2;
layout(location = 7) in vec4 aInstCol3;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;
layout(location = 2) out float fragFogFactor;
layout(location = 3) out vec2 fragUV1;

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
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} fog;

void main() {
    // VULKAN-219: the per-instance matrix applies inside the effect's own world transform, which
    // pc.mvp already carries; the fog term dots the instance-transformed position for the reason
    // VULKAN-222 records -- otherwise every instance is fogged as if it stood at the origin.
    mat4 instWorld = mat4(aInstCol0, aInstCol1, aInstCol2, aInstCol3);
    vec4 objPos = instWorld * vec4(inPos, 1.0);
    vec4 pos = pc.mvp * objPos;
    pos.y = -pos.y;
    gl_Position = pos;
    gl_PointSize = 1.0;
    fragUV   = inUV;
    fragUV1  = inUV1;
    // plan_vulkan.md VULKAN-197 (F-36). Direct3D 9 saturates a vertex shader's colour output
    // registers before interpolation, and FNA writes this value to `vout.Diffuse : COLOR0`.
    // `BasicEffect.DiffuseColor` and `.Alpha` have no clamp in their setters, so a game can hand
    // this shader a value above 1; measured on the real XNA 4.0 runtime, 2.0 and 3.0 both render
    // exactly as 1.0 does (spikes/xna-diffuse-color-clamp-spike/). Clamping at the vertex stage is
    // what oD0 does -- clamping per fragment would interpolate the raw value first and give a
    // different gradient, the distinction plans/plan_fx.md FX-122/FX-123 settled.
    fragTint = clamp(pc.diffuseColor, 0.0, 1.0);
    // Task 899: fog factor from raw object-space Z. REMED-GFX-005: corrected to FNA/EasyGL Task-1111
    // form (z+FogEnd)/(FogEnd-FogStart); the prior Task 888/899 (FogEnd-z) formula was the
    // mirror image and wrong. Zero-length range -> fully fogged, matching FNA SetFogVector.
    fragFogFactor = 1.0 - clamp(dot(objPos, fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
