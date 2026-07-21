$input a_position, a_normal, a_texcoord0, a_weight, a_indices, a_color0
$output v_texcoord0, v_color0, v_fogFactor, v_litRGB, v_specularRGB, v_vertexColor0

#include <bgfx_shader.sh>

// Task 1104: real per-vertex-lit sibling of vs_skinned3d.sc/fs_skinned3d.sc, selected when
// GpuDrawParams::preferPerPixelLighting is false (XNA's real SkinnedEffect default). Skinning
// math is unchanged; lighting (previously computed in fs_skinned3d.sc from v_worldPos/u_eyePos)
// is evaluated here, right after skinning, and Gouraud-interpolated via v_litRGB/v_specularRGB.

uniform mat4 u_wvp;
uniform mat4 u_world;
uniform mat3 u_normalMatrix;
uniform vec4 u_diffuseColor;
uniform mat4 u_bones[72];
uniform vec4 u_fogParams;
uniform vec4 u_depthBias;
uniform vec4 u_weightsPerVertex;
uniform vec4 u_ambientColor;
uniform vec4 u_light0Dir;
uniform vec4 u_light0Diffuse;
uniform vec4 u_light1Dir;
uniform vec4 u_light1Diffuse;
uniform vec4 u_light2Dir;
uniform vec4 u_light2Diffuse;
uniform vec4 u_lightingEnabled;
uniform vec4 u_emissiveColor;
uniform vec4 u_eyePos;
uniform vec4 u_light0Specular;
uniform vec4 u_light1Specular;
uniform vec4 u_light2Specular;
uniform vec4 u_specularColorPower; // xyz = material SpecularColor, w = SpecularPower

void main()
{
    // Task 895: FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs -- unchanged from vs_skinned3d.sc.
    float weightsPerVertex = u_weightsPerVertex.x;
    mat4 skinMat = u_bones[int(a_indices.x)] * a_weight.x;
    if (weightsPerVertex >= 2.0) skinMat += u_bones[int(a_indices.y)] * a_weight.y;
    if (weightsPerVertex >= 4.0) skinMat += u_bones[int(a_indices.z)] * a_weight.z
                                          + u_bones[int(a_indices.w)] * a_weight.w;
    vec4 skinnedPos = mul(skinMat, vec4(a_position, 1.0));
    gl_Position  = mul(u_wvp, skinnedPos);
    // Task 767: RasterizerState.DepthBias emulation (see vs_colored3d.sc for the full comment).
    gl_Position.z += u_depthBias.x * gl_Position.w;
    // REMED-GFX-006: apply the World inverse-transpose (CPU-supplied u_normalMatrix) after the
    // bone 3x3 (audit Variant A fix; see vs_skinned3d.sc). The per-vertex lighting below reads this
    // world-space normal, so the fix must land here too, not only in the per-pixel sibling.
    vec3 skinnedNormal = skinMat[0].xyz * a_normal.x
                       + skinMat[1].xyz * a_normal.y
                       + skinMat[2].xyz * a_normal.z;
    vec3 N = normalize(mul(u_normalMatrix, skinnedNormal));
    v_texcoord0  = a_texcoord0;
    v_color0     = u_diffuseColor;
    // CNB-67 (Phase 13C) Bgfx port: see vs_skinned3d.sc's identical comment.
    v_vertexColor0 = a_color0;
    vec3 worldPos = mul(u_world, skinnedPos).xyz;

    vec3 E = normalize(u_eyePos.xyz - worldPos);
    vec3 nL0 = normalize(u_light0Dir.xyz);
    vec3 nL1 = normalize(u_light1Dir.xyz);
    vec3 nL2 = normalize(u_light2Dir.xyz);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdL2 = max(dotL2, 0.0);
    vec3 lightSum = NdL0 * u_light0Diffuse.xyz + NdL1 * u_light1Diffuse.xyz + NdL2 * u_light2Diffuse.xyz;
    // Matches fs_skinned3d.sc's own formula exactly: emissive+ambient+lightSum, mixed by
    // lightingEnabled (EmissiveColor is pre-folded with AmbientLightColor*DiffuseColor at the
    // C++ FillGpuDrawParams() level, same as the per-pixel-lit sibling).
    vec3 lit = u_emissiveColor.xyz + u_ambientColor.xyz + lightSum;
    v_litRGB = mix(vec3(1.0, 1.0, 1.0), lit, u_lightingEnabled.x);

    vec3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, u_specularColorPower.w);
    vec3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, u_specularColorPower.w);
    vec3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, u_specularColorPower.w);
    v_specularRGB = (spec0 * u_light0Specular.xyz + spec1 * u_light1Specular.xyz
                     + spec2 * u_light2Specular.xyz) * u_specularColorPower.xyz;

    // Task 899: fog factor from raw PRE-SKIN object-space Z, unchanged from vs_skinned3d.sc.
    // REMED-GFX-005: corrected to FNA/EasyGL Task-1111 form (z+FogEnd)/(FogEnd-FogStart); the
    // prior Task 888/899 (FogEnd-z) formula was the mirror image and wrong. Zero-length range
    // (FogStart==FogEnd) -> fully fogged (factor 0), matching FNA SetFogVector.
    v_fogFactor = 1.0 - clamp(dot(vec4(skinnedPos.xyz, 1.0), u_fogParams), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
