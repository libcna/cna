#version 450
//
// plans/plan_vulkan.md VULKAN-227/VULKAN-231: compiled twice -- once plain and once with
// CNA_INSTANCED, which declares the four per-instance matrix columns at locations 12..15 and
// turns CNA_INSTANCE_POSITION()/CNA_INSTANCE_WORLD() from the identity into the per-instance
// transform. Without the define each call expands to exactly the text that was here before,
// so the ordinary module's SPIR-V is byte-identical. See compile_shaders.py.
//
// The per-instance matrix applies AFTER the bone skin and BEFORE World/View/Projection --
// the bone poses the mesh in its own object space and the instance places that posed mesh,
// which is the composition EasyGL's own skinned program uses (cnaInstancePosition(skinnedPos)).

// Stride-56 (SkinnedVertex+Color) companion to skinned3d_vertexlit.vert.glsl (the
// PreferPerPixelLighting=false, per-vertex/Gouraud-lit sibling of skinned3d.vert.glsl) — same
// skinning/lighting math, plus the aColor attribute (location 5) and its vColor varying, passed
// straight through to the fragment stage (mirrors skinned3d_color.vert.glsl's identical
// addition to the per-pixel-lit shader).
layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aUV;
layout(location = 3) in vec4  aBoneWeights;
// plans/plan_vulkan.md VULKAN-151: `vec4`, not `uvec4`. XNA lets a content processor spell
// BLENDINDICES as Byte4 or as Vector4 (plans/plan_fx.md FX-127, and CustomModelAnimation's own
// SkinnedModelProcessor writes Vector4), and a Vulkan shader input cannot take both an integer and
// a float attribute. Taking the indices as floats lets ONE shader serve both spellings: Vector4
// binds natively, and Byte4 binds through VK_FORMAT_R8G8B8A8_USCALED -- integer values converted
// to float without normalisation, which is exactly what EasyGL's own
// glVertexAttribPointer(..., GL_UNSIGNED_BYTE, GL_FALSE, ...) does. A bone palette is a handful of
// entries, far inside float's exact-integer range, so nothing is lost in the conversion.
layout(location = 4) in vec4 aBoneIndices;
layout(location = 5) in vec4  aColor;

layout(location = 0) out vec2  vUV;
layout(location = 1) out float vFogFactor;
layout(location = 2) out vec3  vLitRGB;
layout(location = 3) out vec3  vSpecularRGB;
layout(location = 4) out vec4  vColor;

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

layout(set = 0, binding = 1) uniform BoneBlock {
    mat4 bones[72];
} bb;

layout(set = 0, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;
    vec4 fogVector;
    vec4 light1Dir_pad;
    vec4 light1Diff_pad;
    vec4 light2Dir_pad;
    vec4 light2Diff_pad;
    mat4 world;
    vec4 eyePos_pad; // w = WeightsPerVertex
    vec4 specularColor_power;
    vec4 light0Spec_pad;
    vec4 light1Spec_pad;
    vec4 light2Spec_pad;
    vec4 emissiveColor;   // REMED-GFX-008: pre-folded (emissive + ambient*diffuse)*alpha
} fog;

void main() {
    float weightsPerVertex = fog.eyePos_pad.w;
    mat4 skinMat = bb.bones[int(aBoneIndices.x)] * aBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bb.bones[int(aBoneIndices.y)] * aBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bb.bones[int(aBoneIndices.z)] * aBoneWeights.z
                                          + bb.bones[int(aBoneIndices.w)] * aBoneWeights.w;
    vec4 skinnedPos = CNA_INSTANCE_POSITION(skinMat * vec4(aPos, 1.0));
    gl_Position = pc.mvp * skinnedPos;
    gl_Position.y = -gl_Position.y; // Vulkan NDC Y is inverted vs OpenGL (matches textured3d.vert.glsl)
    gl_PointSize = 1.0;
    vUV = aUV;
    vColor = aColor;
    vec3 worldPos = (fog.world * skinnedPos).xyz;
    vFogFactor = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector

    // REMED-GFX-006: FNA composes the bone-skin 3x3 with the outer world normal matrix
    // (SkinnedEffect.fx Skin() then Lighting.fxh's mul(normal, WorldInverseTranspose)).
    // The world factor was missing entirely, so any rotated or non-uniformly-scaled
    // skinned model was lit as if World were identity.
    mat3 skinNormalMatrix = transpose(inverse(mat3(CNA_INSTANCE_WORLD(fog.world))));
    vec3 N = normalize(skinNormalMatrix * (mat3(skinMat) * aNormal));
    vec3 E = normalize(fog.eyePos_pad.xyz - worldPos);
    float dotL0 = dot(N, -normalize(pc.light0Dir));           float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -normalize(fog.light1Dir_pad.xyz));  float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -normalize(fog.light2Dir_pad.xyz));  float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    vec3 lightSum = pc.light0Diffuse * NdotL0
                   + fog.light1Diff_pad.xyz * NdotL1
                   + fog.light2Diff_pad.xyz * NdotL2;
    // plan_vulkan.md VULKAN-188 (finding F-35): Direct3D 9 saturates a vertex shader's colour
    // output registers (oD0/oD1) to [0,1] BEFORE the rasterizer interpolates them, so real XNA
    // hands a clamped colour to the rasterizer even though Lighting.fxh never writes a
    // saturate(). These are plain varyings, which nothing clamps -- so an unclamped per-vertex
    // sum interpolated between two vertices and the triangle came out brighter than D3D9's,
    // with a different GRADIENT rather than a rounding difference. plans/plan_fx.md FX-122 is
    // the same semantic in MojoShader's path and FX-123 is EasyGL's; this is Vulkan's.
    // plan_vulkan.md VULKAN-205 (F-39): the vertex colour multiplies HERE, inside the
    // saturate, not in the fragment stage afterwards. Direct3D 9 clamps oD0 with the colour
    // already folded in -- FNA's Vc variants do `vout.Diffuse *= vin.Color` ahead of it
    // (FX-125), and VULKAN-200 wrote exactly this for BasicEffect's own colour variant.
    // Clamping first and scaling afterwards is a different picture: a lit sum of 1.8 with a
    // colour of 0.5 gives 0.9 this way and 0.5 the other, measured at 230 against 128.
    vec4 vc = (pc.vertexColorEnabled > 0.5) ? aColor : vec4(1.0);
    vLitRGB = clamp((lightSum * pc.diffuseColor.rgb + fog.emissiveColor.rgb) * vc.rgb,
                    0.0, 1.0);

    float specularPower = fog.specularColor_power.w;
    vec3 h0 = normalize(E - normalize(pc.light0Dir));          float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, specularPower);
    vec3 h1 = normalize(E - normalize(fog.light1Dir_pad.xyz)); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, specularPower);
    vec3 h2 = normalize(E - normalize(fog.light2Dir_pad.xyz)); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, specularPower);
    vSpecularRGB = clamp((spec0 * fog.light0Spec_pad.xyz + spec1 * fog.light1Spec_pad.xyz
                   + spec2 * fog.light2Spec_pad.xyz) * fog.specularColor_power.xyz, 0.0, 1.0);
}
