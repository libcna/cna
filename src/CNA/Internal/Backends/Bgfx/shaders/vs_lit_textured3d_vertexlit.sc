$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_color0, v_fogFactor, v_litRGB, v_specularRGB

#include <bgfx_shader.sh>

// Task 1104: real per-vertex-lit sibling of vs_lit_textured3d.sc/fs_lit_textured3d.sc, selected
// when GpuDrawParams::preferPerPixelLighting is false (XNA's real BasicEffect/SkinnedEffect
// default). Identical Blinn-Phong math (FNA's Lighting.fxh ComputeLights), evaluated here in the
// vertex stage instead of the fragment stage and Gouraud-interpolated across the triangle via
// v_litRGB/v_specularRGB, rather than re-evaluated per fragment from an interpolated normal.

uniform mat4 u_wvp;
uniform mat4 u_world;
uniform mat3 u_normalMatrix;
uniform vec4 u_diffuseColor;
uniform vec4 u_eyePos;
uniform vec4 u_fogParams;
uniform vec4 u_depthBias;
uniform vec4 u_ambientColor;
uniform vec4 u_light0Dir;
uniform vec4 u_light0Diffuse;
uniform vec4 u_lightingEnabled;
uniform vec4 u_light1Dir;
uniform vec4 u_light1Diffuse;
uniform vec4 u_light2Dir;
uniform vec4 u_light2Diffuse;
uniform vec4 u_light0Specular;
uniform vec4 u_light1Specular;
uniform vec4 u_light2Specular;
uniform vec4 u_specularColorPower; // xyz = material SpecularColor, w = SpecularPower

void main()
{
    gl_Position   = mul(u_wvp, vec4(a_position, 1.0));
    // Task 767: RasterizerState.DepthBias emulation (see vs_colored3d.sc for the full comment).
    gl_Position.z += u_depthBias.x * gl_Position.w;
    vec3 worldPos = mul(u_world, vec4(a_position, 1.0)).xyz;
    v_texcoord0   = a_texcoord0;
    v_color0      = u_diffuseColor;

    // Task 892's inverse-transpose normal matrix, unchanged from the per-pixel-lit sibling.
    vec3 N = normalize(mul(u_normalMatrix, a_normal));
    vec3 E = normalize(u_eyePos.xyz - worldPos);
    vec3 nL0 = normalize(u_light0Dir.xyz);
    vec3 nL1 = normalize(u_light1Dir.xyz);
    vec3 nL2 = normalize(u_light2Dir.xyz);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdL2 = max(dotL2, 0.0);
    vec3 lightSum = u_ambientColor.xyz + NdL0 * u_light0Diffuse.xyz
                    + NdL1 * u_light1Diffuse.xyz + NdL2 * u_light2Diffuse.xyz;
    v_litRGB = mix(vec3(1.0, 1.0, 1.0), lightSum, u_lightingEnabled.x);

    // Half-vector Blinn-Phong specular, identical to fs_lit_textured3d.sc's own formula.
    vec3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, u_specularColorPower.w);
    vec3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, u_specularColorPower.w);
    vec3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, u_specularColorPower.w);
    v_specularRGB = (spec0 * u_light0Specular.xyz + spec1 * u_light1Specular.xyz
                     + spec2 * u_light2Specular.xyz) * u_specularColorPower.xyz;

    // REMED-GFX-005: fog factor from raw object-space Z, corrected to EasyGL's Task-1111 form
    // (the prior Task-888 (FogEnd-z) form was the mirror of EasyGL's since-fixed formula, NOT
    // a match to it). u_fogParams = (fogEnabled, fogStart, fogEnd, unused). 1.0 = no fog, 0.0 = full.
    // REMED-GFX-005: corrected to FNA/EasyGL Task-1111 form (z+FogEnd)/(FogEnd-FogStart); the
    // prior Task 888/899 (FogEnd-z) formula was the mirror image and wrong. Zero-length range
    // (FogStart==FogEnd) -> fully fogged (factor 0), matching FNA SetFogVector.
    v_fogFactor = (u_fogParams.x > 0.5)
        ? ((abs(u_fogParams.z - u_fogParams.y) < 1e-6)
            ? 0.0
            : clamp((a_position.z + u_fogParams.z) / (u_fogParams.z - u_fogParams.y), 0.0, 1.0))
        : 1.0;
}
