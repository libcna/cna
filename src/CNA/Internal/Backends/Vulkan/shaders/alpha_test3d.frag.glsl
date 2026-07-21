#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragTint;
layout(location = 2) in float fragFogFactor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform PC {
    mat4  mvp;                 // offset  0
    vec4  diffuseColor;        // offset 64
    float alphaRef;            // offset 80 — reference alpha value
    float alphaTol;            // offset 84 — tolerance (>0 = equality test, 0 = comparison)
    float alphaPassW;          // offset 88 — weight when test passes  (< 0 = discard)
    float alphaFailW;          // offset 92 — weight when test fails   (< 0 = discard)
    float vertexColorEnabled;  // offset 96 — unused here (read only in the VS)
    // Task 888: fog, packed into what was previously unused padding.
    // REMED-GFX-010: FogColor as 3 floats + FNA fog vector vec4 (offset 112). Layout must match the VS.
    float fogColorR;           // offset 100
    float fogColorG;           // offset 104
    float fogColorB;           // offset 108
    vec4  fogVector;           // offset 112
} pc;

// Alpha test uses the same encoding as EasyGL:
//   if (alphaTol > 0) → pass = |alpha - alphaRef| < alphaTol
//   else              → pass = alpha < alphaRef
//   weight = pass ? alphaPassW : alphaFailW
//   if weight < 0 → discard
// Default {0,0,1,1} = always pass (never discard).
void main() {
    outColor = texture(uTexture, fragUV) * fragTint;

    float alpha = outColor.a;
    bool  passTest;
    if (pc.alphaTol > 0.0) {
        passTest = abs(alpha - pc.alphaRef) < pc.alphaTol;
    } else {
        passTest = alpha < pc.alphaRef;
    }
    float w = passTest ? pc.alphaPassW : pc.alphaFailW;
    if (w < 0.0) discard;

    // Task 888: mix toward FogColor as fragFogFactor -> 0 (matches EasyGL's established formula).
    outColor.rgb = mix(vec3(pc.fogColorR, pc.fogColorG, pc.fogColorB), outColor.rgb, fragFogFactor);
}
