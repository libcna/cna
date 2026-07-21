$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_normal, v_eyeDir, v_fogFactor

#include <bgfx_shader.sh>

uniform mat4 u_wvp;
uniform mat4 u_world;
uniform mat3 u_normalMatrix;
uniform vec4 u_eyePos;
uniform vec4 u_fogParams;
uniform vec4 u_depthBias;

void main()
{
    gl_Position   = mul(u_wvp, vec4(a_position, 1.0));
    // Task 767: RasterizerState.DepthBias emulation (see vs_colored3d.sc for the full comment).
    gl_Position.z += u_depthBias.x * gl_Position.w;
    vec3 worldPos = mul(u_world, vec4(a_position, 1.0)).xyz;
    // Task 398 fix: transform by the precomputed inverse-transpose of World's upper-left 3x3
    // (cofactor/det, computed on the CPU side), not World directly -- a direct World multiply
    // is only correct under rotation/uniform-scale/translation, and skews the normal under
    // non-uniform scale.
    v_normal      = mul(u_normalMatrix, a_normal);
    v_eyeDir      = u_eyePos.xyz - worldPos;
    v_texcoord0   = a_texcoord0;
    // REMED-GFX-005: fog factor from raw object-space Z, corrected to EasyGL's Task-1111 form
    // (the prior Task-899 (FogEnd-z) form mirrored lit_textured3d's own mirrored fog, NOT the
    // correct formula). u_fogParams = (fogEnabled, fogStart, fogEnd, unused). 1.0 = no fog, 0.0 = full.
    // REMED-GFX-005: corrected to FNA/EasyGL Task-1111 form (z+FogEnd)/(FogEnd-FogStart); the
    // prior Task 888/899 (FogEnd-z) formula was the mirror image and wrong. Zero-length range
    // (FogStart==FogEnd) -> fully fogged (factor 0), matching FNA SetFogVector.
    v_fogFactor = (u_fogParams.x > 0.5)
        ? ((abs(u_fogParams.z - u_fogParams.y) < 1e-6)
            ? 0.0
            : clamp((a_position.z + u_fogParams.z) / (u_fogParams.z - u_fogParams.y), 0.0, 1.0))
        : 1.0;
}
